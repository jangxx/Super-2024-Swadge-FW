//==============================================================================
// Includes
//==============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <esp_timer.h>

#include "fmTuner.h"
#include "swadge2024.h"
#include "hdw-btn.h"
#include "font.h"
#include "wsg.h"
#include "TEA5767.h"
#include "hdw-imu.h"

//==============================================================================
// Defines
//==============================================================================

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

	bool mute;
	bool searchMode;
	bool searchUp;
	// search stop level
	bool highSideInjection;
	bool forceMono;
	bool standBy;
	bool japaneseBands;
	bool softMute;
	bool highCutControl;
	bool stereoNoiseCancelling;

	float frequency;

	int64_t lastUpdate;
} fmTunerMode_t;

//==============================================================================
// Function Prototypes
//==============================================================================

static void fmTunerEnterMode(void);
static void fmTunerExitMode(void);
static void fmTunerMainLoop(int64_t elapsedUs);
static void fmTunerUpdateSendBuffer();
static void fmTunerWriteSendBuffer();
static void fmTunerReadRecvBuffer();

//==============================================================================
// Strings
//==============================================================================

static const char tunerName[] = "FM Tuner";

// static const char startStr[] = "Start";
static const char titleStr[] = "FM Tuner";
// static const char ledModeStr[] = "LED Mode";


//==============================================================================
// Variables
//==============================================================================

swadgeMode_t fmTunerMode = {
	.modeName                 = tunerName,
	.wifiMode                 = NO_WIFI,
	.overrideUsb              = false,
	.usesAccelerometer        = false,
	.usesI2C				  = true,
	.usesThermometer          = false,
	.overrideSelectBtn        = false,
	.fnEnterMode              = fmTunerEnterMode,
	.fnExitMode               = fmTunerExitMode,
	.fnMainLoop               = fmTunerMainLoop,
	.fnAudioCallback          = NULL,
	.fnBackgroundDrawCallback = NULL,
	.fnEspNowRecvCb           = NULL,
	.fnEspNowSendCb           = NULL,
	.fnAdvancedUSB            = NULL,
};

static fmTunerMode_t* tunerData = NULL;

static char sendBuffer[5] = {0};
static char recvBuffer[5] = {0};

//==============================================================================
// Functions
//==============================================================================

static void fmTunerEnterMode(void)
{
	tunerData = calloc(1, sizeof(fmTunerMode_t));

	loadFont("ibm_vga8.font", &tunerData->textFont, false);
	loadFont("seven_segment.font", &tunerData->numberFont, false);
	loadWsg("button_up.wsg", &tunerData->dpadWsg, false);
	loadWsg("button_a.wsg", &tunerData->aWsg, false);
	loadWsg("button_b.wsg", &tunerData->bWsg, false);

	// 30 FPS
	setFrameRateUs(1000000 / 25);

	tunerData->mute = false;
	tunerData->searchMode = false;
	tunerData->searchUp = false;
	tunerData->highSideInjection = true;
	tunerData->forceMono = true;
	tunerData->standBy = false;
	tunerData->japaneseBands = false;
	tunerData->softMute = false;
	tunerData->highCutControl = false;
	tunerData->stereoNoiseCancelling = true;

	tunerData->frequency = 100.0;

	fmTunerUpdateSendBuffer();
	fmTunerWriteSendBuffer();

	tunerData->lastUpdate = esp_timer_get_time();

	fmTunerReadRecvBuffer();
}

static void fmTunerExitMode(void)
{
	freeFont(&tunerData->textFont);
	freeFont(&tunerData->numberFont);

	freeWsg(&tunerData->dpadWsg);
	freeWsg(&tunerData->aWsg);
	freeWsg(&tunerData->bWsg);

	free(tunerData);
	tunerData = NULL;
}

static uint16_t calcPLL() {
	if (tunerData->highSideInjection) {
		return (4 * (tunerData->frequency * 1000000 + 225000) / 32768);
	} else {
		return (4 * (tunerData->frequency * 1000000 - 225000) / 32768);
	}
}

static void fmTunerUpdateSendBuffer() {
	// adapted from https://github.com/andykarpov/TEA5767/blob/master/TEA5767.cpp

	memset(sendBuffer, 0, sizeof(sendBuffer));

	uint16_t pll = calcPLL();

	sendBuffer[0] = (pll >> 8) & 0x3F;
	sendBuffer[1] = pll & 0xFF;

	if (tunerData->mute) {
		sendBuffer[0] |= TEA5767_MUTE;
	}
	if (tunerData->searchMode) {
		sendBuffer[0] |= TEA5767_SEARCH;

		if (tunerData->searchUp) {
			sendBuffer[2] |= TEA5767_SEARCH_UP;
		}
	}
	if (tunerData->highSideInjection) {
		sendBuffer[2] |= TEA5767_HIGH_LO_INJECT;
	}
	if (tunerData->forceMono) {
		sendBuffer[2] |= TEA5767_MONO;
	}
	if (tunerData->standBy) {
		sendBuffer[3] |= TEA5767_STDBY;
	}
	if (tunerData->japaneseBands) {
		sendBuffer[3] |= TEA5767_JAPAN_BAND;
	}
	if (tunerData->softMute) {
		sendBuffer[3] |= TEA5767_SOFT_MUTE;
	}
	if (tunerData->highCutControl) {
		sendBuffer[3] |= TEA5767_HIGH_CUT_CTRL;
	}
	if (tunerData->stereoNoiseCancelling) {
		sendBuffer[3] |= TEA5767_ST_NOISE_CTL;
	}
}

static void fmTunerWriteSendBuffer() {
	SendI2CBuffer(0b1100000, sendBuffer, sizeof(sendBuffer));
}

static void fmTunerReadRecvBuffer() {
	ReadI2CBuffer(0b1100000, recvBuffer, sizeof(recvBuffer));
}

static void fmTunerMainLoop(int64_t elapsedUs)
{
	int64_t now = esp_timer_get_time();

	if (now - tunerData->lastUpdate > 1000000) {
		fmTunerReadRecvBuffer();
		tunerData->lastUpdate = now;
	}

	buttonEvt_t evt;
	bool changes = false;
	while (checkButtonQueueWrapper(&evt))
	{
		if (evt.down)
		{
			if (evt.button == PB_A || evt.button == PB_B)
			{

			}
			else if (evt.button == PB_UP)
			{
				tunerData->frequency += 0.1;
				changes = true;
			}
			else if (evt.button == PB_DOWN)
			{
				tunerData->frequency -= 0.1;
				changes = true;
			}
		}
	}

	if (changes) {
		fmTunerUpdateSendBuffer();
		fmTunerWriteSendBuffer();
	}

	clearPxTft();

	// int16_t wsgOffset = (tunerData->aWsg.h - tunerData->textFont.height) / 2;

	uint16_t titleWidth = textWidth(&tunerData->textFont, titleStr);
	// uint16_t controlsOffset = TFT_HEIGHT - 30;

	drawText(&tunerData->textFont, c555, titleStr, TFT_WIDTH/2 - titleWidth/2, 5);

	char buffer[64];

	snprintf(buffer, sizeof(buffer), "send: %02X %02X %02X %02X %02X", sendBuffer[0], sendBuffer[1], sendBuffer[2], sendBuffer[3], sendBuffer[4]);

	drawText(&tunerData->textFont, c444, buffer, 0, TFT_HEIGHT / 2);

	snprintf(buffer, sizeof(buffer), "recv: %02X %02X %02X %02X %02X", recvBuffer[0], recvBuffer[1], recvBuffer[2], recvBuffer[3], recvBuffer[4]);

	drawText(&tunerData->textFont, c444, buffer, 0, TFT_HEIGHT / 2 + 20);

	snprintf(buffer, sizeof(buffer), "freq: %f", tunerData->frequency);

	drawText(&tunerData->textFont, c444, buffer, 0, TFT_HEIGHT / 2 + 40);

	// drawWsgSimple(&tunerData->aWsg, 20, controlsOffset);
	// drawWsgSimple(&tunerData->bWsg, 35, controlsOffset);

	// drawText(&tunerData->textFont, c444, startStr, 55, controlsOffset + wsgOffset);

	// drawWsg(&tunerData->dpadWsg, TFT_WIDTH - 150, controlsOffset, false, false, 0);
	// drawWsg(&tunerData->dpadWsg, TFT_WIDTH - 135, controlsOffset, false, true, 0);

	// drawText(&tunerData->textFont, c444, ledModeStr, TFT_WIDTH - 115, controlsOffset + wsgOffset);

	// int64_t elapsed = now - tunerData->startTime;
	// int64_t previousElapsed = elapsed - elapsedUs;

	// uint16_t elapsedMillis = (elapsed / 1000) % 1000;
	// uint8_t elapsedSecs    = (elapsed / 1000000) % 60;
	// uint8_t previousElapsedSecs = (previousElapsed / 1000000) % 60;
	// uint8_t elapsedMins    = (elapsed / (60 * 1000000)) % 60;
	// uint64_t elapsedHrs = elapsed / 3600000000;

	// char buffer[64];
	// if (elapsedHrs > 0)
	// {
	// 	snprintf(buffer, sizeof(buffer), hoursMinutesSecondsFmt, elapsedHrs, elapsedMins, elapsedSecs, elapsedMillis);
	// }
	// else
	// {
	// 	snprintf(buffer, sizeof(buffer), minutesSecondsFmt, elapsedMins, elapsedSecs, elapsedMillis);
	// }

	// uint16_t textX;
	// uint16_t textY = (TFT_HEIGHT - tunerData->numberFont.height) / 2;

	// if (tunerData->timerState == RUNNING) {
	// 	textX = (TFT_WIDTH - textWidth(&tunerData->numberFont, buffer)) / 2;
	// 	textX = drawText(&tunerData->numberFont, c050, buffer, textX, textY);

	// 	switch(tunerData->ledMode) {
	// 		case LED_MODE_FULL:
	// 			ledModeFull(elapsedSecs, elapsedMillis);
	// 			break;
	// 		case LED_MODE_SINGLE:
	// 			ledModeSingle(elapsedSecs, elapsedMillis);
	// 			break;
	// 		default:
	// 			break;
	// 	}
	// } else if (tunerData->timerState == STOPPED) {
	// 	if (elapsedSecs % 2 == 0) {
	// 		if (previousElapsedSecs != elapsedSecs) {
	// 			setLedsOff();
	// 		}

	// 		textX = (TFT_WIDTH - textWidth(&tunerData->numberFont, cheersStr)) / 2;
	// 		textX = drawText(&tunerData->numberFont, c050, cheersStr, textX, textY);
	// 	} else {
	// 		if (previousElapsedSecs != elapsedSecs) {
	// 			setLedsCheers();
	// 		}
	// 	}
	// } else if (tunerData->timerState == INITIAL) {
	// 	textX = (TFT_WIDTH - textWidth(&tunerData->numberFont, cheersStr)) / 2;
	// 	textX = drawText(&tunerData->numberFont, c050, cheersStr, textX, textY);
	// }
}
