//==============================================================================
// Includes
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <esp_timer.h>

#include "cheersTimer.h"
#include "swadge2024.h"
#include "hdw-btn.h"
#include "font.h"
#include "wsg.h"

//==============================================================================
// Enums
//==============================================================================

typedef enum
{
	STOPPED = 0,
	INITIAL,
	RUNNING,
} timerState_t;

typedef enum
{
	LED_MODE_FULL = 0,
	LED_MODE_SINGLE,
	LED_MODE_MAX
} ledMode_t;

//==============================================================================
// Structs
//==============================================================================

typedef struct
{
	font_t textFont;
	font_t numberFont;
	wsg_t dpadWsg;
	wsg_t aWsg;
	wsg_t bWsg;
	song_t coinFX;

	/// @brief Current timer state
	timerState_t timerState;

	/// @brief The actual time the timer was started
	int64_t startTime;

	ledMode_t ledMode;
} timerMode_t;

//==============================================================================
// Function Prototypes
//==============================================================================

static void cheersTimerEnterMode(void);
static void cheersTimerExitMode(void);
static void cheersTimerMainLoop(int64_t elapsedUs);
static void setLedsCheers();
static void setLedsOff();
static void cheersTimerEspNowRecvCb(const esp_now_recv_info_t *esp_now_info, const uint8_t* data, uint8_t len, int8_t rssi);
static void cheersTimerEspNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status);
static void cheersTimerSetState(int64_t now, timerState_t state);

static void ledModeFull(uint8_t elapsedSecs, uint16_t elapsedMillis);
static void ledModeSingle(uint8_t elapsedSecs, uint16_t elapsedMillis);

//==============================================================================
// Strings
//==============================================================================

static const char timerName[]              = "Cheers Timer";
static const char minutesSecondsFmt[]      = "%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16;
static const char hoursMinutesSecondsFmt[] = "%" PRIu64 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16;

static const char startStr[] = "Start";
static const char cheersStr[] = "Cheers";
static const char titleStr[] = "Time since last Cheers";
static const char ledModeStr[] = "LED Mode";

static const char cheersPacket[] = "CHEERS";
static const char resetPacket[]  = "SREEHC";

static led_t cheersLeds[CONFIG_NUM_LEDS] = {{0}};

//==============================================================================
// Variables
//==============================================================================

swadgeMode_t cheersTimerMode = {
	.modeName                 = timerName,
	.wifiMode                 = ESP_NOW,
	.overrideUsb              = false,
	.usesAccelerometer        = false,
	.usesThermometer          = false,
	.overrideSelectBtn        = false,
	.fnEnterMode              = cheersTimerEnterMode,
	.fnExitMode               = cheersTimerExitMode,
	.fnMainLoop               = cheersTimerMainLoop,
	.fnAudioCallback          = NULL,
	.fnBackgroundDrawCallback = NULL,
	.fnEspNowRecvCb           = cheersTimerEspNowRecvCb,
	.fnEspNowSendCb           = cheersTimerEspNowSendCb,
	.fnAdvancedUSB            = NULL,
};

static timerMode_t* timerData = NULL;

//==============================================================================
// Functions
//==============================================================================

static void cheersTimerEnterMode(void)
{
	timerData = calloc(1, sizeof(timerMode_t));

	loadFont("ibm_vga8.font", &timerData->textFont, false);
	loadFont("seven_segment.font", &timerData->numberFont, false);
	loadWsg("button_up.wsg", &timerData->dpadWsg, false);
	loadWsg("button_a.wsg", &timerData->aWsg, false);
	loadWsg("button_b.wsg", &timerData->bWsg, false);
	loadSong("coin.sng", &timerData->coinFX, false);

	// 30 FPS
	setFrameRateUs(1000000 / 30);

	timerData->timerState = INITIAL;
	timerData->ledMode = LED_MODE_FULL;

	// theoretically we would only have to do this once ever
	for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++) {
		cheersLeds[i].r = 255;
		cheersLeds[i].g = 255;
		cheersLeds[i].b = 0;
	}

	setLedsCheers();
}

static void cheersTimerExitMode(void)
{
	freeFont(&timerData->textFont);
	freeFont(&timerData->numberFont);

	freeWsg(&timerData->dpadWsg);
	freeWsg(&timerData->aWsg);
	freeWsg(&timerData->bWsg);

	freeSong(&timerData->coinFX);

	free(timerData);
	timerData = NULL;
}

static void cheersTimerEspNowRecvCb(const esp_now_recv_info_t *esp_now_info, const uint8_t* data, uint8_t len, int8_t rssi)
{
	int64_t now = esp_timer_get_time();

	// Both start and stop packets are 7 bytes long. (6 chars + null terminator)
	if (len == 7)
	{
		uint8_t buffer[len];
		memcpy(buffer, data, len);
		buffer[len - 1] = '\0'; // Ensure null termination

		if (strcmp((char*)buffer, cheersPacket) == 0) {
			cheersTimerSetState(now, STOPPED);
		} else if (strcmp((char*)buffer, resetPacket) == 0) {
			cheersTimerSetState(now, RUNNING);
		}
	}
}

static void cheersTimerEspNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status)
{
    // static led_t leds[CONFIG_NUM_LEDS] = {{0}};

	// for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++) {
	// 	leds[i].r = (status == ESP_NOW_SEND_FAIL) ? 255 : 0;
	// 	leds[i].g = (status == ESP_NOW_SEND_SUCCESS) ? 255 : 0;
	// 	leds[i].b = 0;
	// }

	// setLeds(leds, CONFIG_NUM_LEDS);

	// Do nothing
}


static void cheersTimerSetState(int64_t now, timerState_t state)
{
	static led_t leds[CONFIG_NUM_LEDS] = {{0}};

	timerData->timerState = state;

	switch (state)
	{
		case RUNNING:
		{
			timerData->startTime = now;

			for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++) {
				leds[i].r = 255;
				leds[i].g = 0;
				leds[i].b = 0;
			}

			setLeds(leds, CONFIG_NUM_LEDS);
			break;
		}
		case STOPPED:
		{	
			timerData->startTime = now;

			for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++) {
				leds[i].r = 0;
				leds[i].g = 0;
				leds[i].b = 0;
			}

			setLeds(leds, CONFIG_NUM_LEDS);

			bzrPlaySfx(&timerData->coinFX, BZR_STEREO);
			break;
		}
		default:
			break;
	}
}


static void cheersTimerMainLoop(int64_t elapsedUs)
{
	int64_t now = esp_timer_get_time();

	buttonEvt_t evt;
	while (checkButtonQueueWrapper(&evt))
	{
		if (evt.down)
		{
			if (evt.button == PB_A || evt.button == PB_B)
			{
				switch (timerData->timerState)
				{
					case STOPPED:
					case INITIAL:
					{
						espNowSend(resetPacket, ARRAY_SIZE(resetPacket));
						cheersTimerSetState(now, RUNNING);
						break;
					}

					case RUNNING:
					{
						espNowSend(cheersPacket, ARRAY_SIZE(cheersPacket));
						cheersTimerSetState(now, STOPPED);
						break;
					}
				}
			}
			else if (evt.button == PB_UP)
			{
				timerData->ledMode = (timerData->ledMode + 1) % LED_MODE_MAX;
			}
			else if (evt.button == PB_DOWN)
			{
				timerData->ledMode = (timerData->ledMode + LED_MODE_MAX - 1) % LED_MODE_MAX;
			}
		}
	}

	clearPxTft();

	int16_t wsgOffset = (timerData->aWsg.h - timerData->textFont.height) / 2;

	uint16_t titleWidth = textWidth(&timerData->textFont, titleStr);
	uint16_t controlsOffset = TFT_HEIGHT - 30;

	drawText(&timerData->textFont, c555, titleStr, TFT_WIDTH/2 - titleWidth/2, 5);

	drawWsgSimple(&timerData->aWsg, 20, controlsOffset);
	drawWsgSimple(&timerData->bWsg, 35, controlsOffset);

	if (timerData->timerState == STOPPED || timerData->timerState == INITIAL) {
		drawText(&timerData->textFont, c444, startStr, 55, controlsOffset + wsgOffset);
	} else {
		drawText(&timerData->textFont, c444, cheersStr, 55, controlsOffset + wsgOffset);
	}

	drawWsg(&timerData->dpadWsg, TFT_WIDTH - 150, controlsOffset, false, false, 0);
	drawWsg(&timerData->dpadWsg, TFT_WIDTH - 135, controlsOffset, false, true, 0);

	drawText(&timerData->textFont, c444, ledModeStr, TFT_WIDTH - 115, controlsOffset + wsgOffset);

	int64_t elapsed = now - timerData->startTime;
	int64_t previousElapsed = elapsed - elapsedUs;

	uint16_t elapsedMillis = (elapsed / 1000) % 1000;
	uint16_t previousElapsedMillis = (previousElapsed / 1000) % 1000;
	uint8_t elapsedSecs    = (elapsed / 1000000) % 60;
	uint8_t previousElapsedSecs = (previousElapsed / 1000000) % 60;
	uint8_t elapsedMins    = (elapsed / (60 * 1000000)) % 60;
	uint64_t elapsedHrs = elapsed / 3600000000;

	char buffer[64];
	if (elapsedHrs > 0)
	{
		snprintf(buffer, sizeof(buffer), hoursMinutesSecondsFmt, elapsedHrs, elapsedMins, elapsedSecs, elapsedMillis);
	}
	else
	{
		snprintf(buffer, sizeof(buffer), minutesSecondsFmt, elapsedMins, elapsedSecs, elapsedMillis);
	}

	uint16_t textX;
	uint16_t textY = (TFT_HEIGHT - timerData->numberFont.height) / 2;

	if (timerData->timerState == RUNNING) {
		textX = (TFT_WIDTH - textWidth(&timerData->numberFont, buffer)) / 2;
		textX = drawText(&timerData->numberFont, c050, buffer, textX, textY);

		switch(timerData->ledMode) {
			case LED_MODE_FULL:
				ledModeFull(elapsedSecs, elapsedMillis);
				break;
			case LED_MODE_SINGLE:
				ledModeSingle(elapsedSecs, elapsedMillis);
				break;
			default:
				break;
		}
	} else if (timerData->timerState == STOPPED) {
		if (elapsedMillis < 500) {
			if (previousElapsedMillis >= 500) {
				setLedsOff();
			}

			textX = (TFT_WIDTH - textWidth(&timerData->numberFont, cheersStr)) / 2;
			textX = drawText(&timerData->numberFont, c050, cheersStr, textX, textY);
		} else {
			if (previousElapsedMillis < 500) {
				setLedsCheers();
			}
		}
	} else if (timerData->timerState == INITIAL) {
		textX = (TFT_WIDTH - textWidth(&timerData->numberFont, cheersStr)) / 2;
		textX = drawText(&timerData->numberFont, c050, cheersStr, textX, textY);
	}
}

static void setLedsCheers() {
	setLeds(cheersLeds, CONFIG_NUM_LEDS);
}

static void setLedsOff() {
	static led_t leds[CONFIG_NUM_LEDS] = {{0}};

	setLeds(leds, CONFIG_NUM_LEDS);
}

static void ledModeFull(uint8_t elapsedSecs, uint16_t elapsedMillis) {
	static led_t leds[CONFIG_NUM_LEDS] = {{0}};

	for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++) {
		leds[i].r = (elapsedSecs % 3 == 0) ? 255 - elapsedMillis/4 : 0;
		leds[i].g = ((elapsedSecs+1) % 3 == 0) ? 255 - elapsedMillis/4 : 0;
		leds[i].b = ((elapsedSecs+2) % 3 == 0) ? 255 - elapsedMillis/4 : 0;
	}

	setLeds(leds, CONFIG_NUM_LEDS);
}

static void ledModeSingle(uint8_t elapsedSecs, uint16_t elapsedMillis) {
	led_t leds[CONFIG_NUM_LEDS] = {{0}};

	leds[elapsedSecs % CONFIG_NUM_LEDS].r = (elapsedSecs % 3 == 0) ? 255 - elapsedMillis/4 : 0;
	leds[elapsedSecs % CONFIG_NUM_LEDS].g = ((elapsedSecs+1) % 3 == 0) ? 255 - elapsedMillis/4 : 0;
	leds[elapsedSecs % CONFIG_NUM_LEDS].b = ((elapsedSecs+2) % 3 == 0) ? 255 - elapsedMillis/4 : 0;

	setLeds(leds, CONFIG_NUM_LEDS);
}