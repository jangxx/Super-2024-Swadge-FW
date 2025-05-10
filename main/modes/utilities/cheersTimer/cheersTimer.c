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
// Defines
//==============================================================================

#define PAUSE_FLASH_SPEED  1000000
#define PAUSE_FLASH_SHOW   600000
#define EXPIRE_FLASH_SPEED 500000
#define EXPIRE_FLASH_SHOW  250000
#define REPEAT_DELAY       500000
#define REPEAT_TIME        150000

//==============================================================================
// Enums
//==============================================================================

typedef enum
{
  STOPPED = 0,
  RUNNING
} timerState_t;

typedef enum
{
  LED_PATTERN_SINGLE = 0,
  LED_PATTERN_ALL
} ledPattern_t;

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
  midiFile_t sound1Up;

  /// @brief Current timer state
  timerState_t timerState;
  /// @brief Current LED pattern
  ledPattern_t ledPattern;

  /// @brief The actual time the timer was started
  int64_t startTime;
} timerMode_t;

//==============================================================================
// Function Prototypes
//==============================================================================

static void cheersTimerEnterMode(void);
static void cheersTimerExitMode(void);
static void cheersTimerMainLoop(int64_t elapsedUs);
static void setLedsCheers();
static void setLedsOff();
static void cheersTimerEspNowRecvCb(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, uint8_t len,
                                    int8_t rssi);
static void cheersTimerSetState(int64_t now, timerState_t state);

//==============================================================================
// Strings
//==============================================================================

static const char timerName[]              = "Cheers Timer";
static const char hoursMinutesSecondsFmt[] = "%" PRIu64 ":%02" PRIu8 ":%02" PRIu8;

static const char startStr[]      = "Start";
static const char cheersStr[]     = "Cheers!";
static const char titleStr[]      = "Time since last \"Cheers!\"";
static const char ledPatternStr[] = "LED Pattern";

static const char cheersPacket[] = "CHEERS";
static const char resetPacket[]  = "SREEHC";

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
  .fnEspNowSendCb           = NULL,
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
  loadMidiFile("snd1up.mid", &timerData->sound1Up, true);

  // Don't need a high FPS, save some battery
  setFrameRateUs(1000000 / 20);

  timerData->timerState = STOPPED;
  timerData->ledPattern = LED_PATTERN_SINGLE;

  setLedsCheers();
}

static void cheersTimerExitMode(void)
{
  freeFont(&timerData->textFont);
  freeFont(&timerData->numberFont);

  freeWsg(&timerData->dpadWsg);
  freeWsg(&timerData->aWsg);
  freeWsg(&timerData->bWsg);

  unloadMidiFile(&timerData->sound1Up);

  free(timerData);
  timerData = NULL;
}

static void cheersTimerEspNowRecvCb(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, uint8_t len,
                                    int8_t rssi)
{
  int64_t now = esp_timer_get_time();

  // Both start and stop packets are 7 bytes long. (6 chars + null terminator)
  if (len == 7)
  {
    uint8_t buffer[len];
    memcpy(buffer, data, len);
    buffer[len - 1] = '\0'; // Ensure null termination

    if (strcmp((char*)buffer, cheersPacket) == 0)
    {
      cheersTimerSetState(now, STOPPED);
    }
    if (strcmp((char*)buffer, resetPacket) == 0)
    {
      cheersTimerSetState(now, RUNNING);
    }
  }
}

static void cheersTimerSetState(int64_t now, timerState_t state)
{
  switch (state)
  {
    case RUNNING:
      timerData->startTime  = now;
      timerData->timerState = RUNNING;

      setLedsOff();
      break;
    case STOPPED:
      timerData->timerState = STOPPED;

      setLedsCheers();
      soundPlaySfx(&timerData->sound1Up, MIDI_SFX);
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
            espNowSend(resetPacket, ARRAY_SIZE(resetPacket));
            cheersTimerSetState(now, RUNNING);
            break;
          case RUNNING:
            espNowSend(cheersPacket, ARRAY_SIZE(cheersPacket));
            cheersTimerSetState(now, STOPPED);
            break;
        }
      }
      else if (evt.button == PB_UP)
			{
        timerData->ledPattern = (timerData->ledPattern + 1) % (LED_PATTERN_ALL + 1);
			}
			else if (evt.button == PB_DOWN)
			{
				timerData->ledPattern = (timerData->ledPattern - 1) % (LED_PATTERN_ALL + 1);
      }
    }
  }

  clearPxTft();

  int16_t wsgOffset = (timerData->aWsg.h - timerData->textFont.height) / 2;

  uint16_t titleWidth     = textWidth(&timerData->textFont, titleStr);
  uint16_t controlsOffset = TFT_HEIGHT - 30;

  drawText(&timerData->textFont, c555, titleStr, TFT_WIDTH / 2 - titleWidth / 2, 5);

  drawWsgSimple(&timerData->aWsg, 20, controlsOffset);
  drawWsgSimple(&timerData->bWsg, 35, controlsOffset);

  if (timerData->timerState == STOPPED)
  {
    drawText(&timerData->textFont, c444, startStr, 55, controlsOffset + wsgOffset);
  }
  else
  {
    drawText(&timerData->textFont, c444, cheersStr, 55, controlsOffset + wsgOffset);
  }

  drawWsg(&timerData->dpadWsg, TFT_WIDTH - 150, controlsOffset, false, false, 0);
	drawWsg(&timerData->dpadWsg, TFT_WIDTH - 135, controlsOffset, false, true, 0);
  drawText(&timerData->textFont, c444, ledPatternStr, TFT_WIDTH - 115, controlsOffset + wsgOffset);

  int64_t elapsed = now - timerData->startTime;

  uint16_t elapsedMillis = (elapsed / 1000) % 1000;
  uint8_t elapsedSecs    = (elapsed / 1000000) % 60;
  uint8_t elapsedMins    = (elapsed / (60 * 1000000)) % 60;
  uint64_t elapsedHrs    = elapsed / 3600000000;

  char buffer[64];
  snprintf(buffer, sizeof(buffer), hoursMinutesSecondsFmt, elapsedHrs, elapsedMins, elapsedSecs);

  uint16_t textX;
  uint16_t textY = (TFT_HEIGHT - timerData->numberFont.height) / 2;

  if (timerData->timerState == RUNNING)
  {
    led_t leds[CONFIG_NUM_LEDS] = {{0}};

    textX = (TFT_WIDTH - textWidth(&timerData->numberFont, buffer)) / 2;
    textX = drawText(&timerData->numberFont, c050, buffer, textX, textY);

    switch (timerData->ledPattern)
    {
      case LED_PATTERN_SINGLE:
        leds[elapsedSecs % CONFIG_NUM_LEDS].r = (elapsedSecs % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
        leds[elapsedSecs % CONFIG_NUM_LEDS].g = ((elapsedSecs + 1) % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
        leds[elapsedSecs % CONFIG_NUM_LEDS].b = ((elapsedSecs + 2) % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
        break;
      case LED_PATTERN_ALL:
        for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++)
        {
          leds[i].r = (elapsedSecs % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
          leds[i].g = ((elapsedSecs + 1) % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
          leds[i].b = ((elapsedSecs + 2) % 3 == 0) ? 255 - elapsedMillis / 4 : 0;
        }
        break;
    }

    setLeds(leds, CONFIG_NUM_LEDS);
  }
  else
  {
    textX = (TFT_WIDTH - textWidth(&timerData->numberFont, cheersStr)) / 2;
    textX = drawText(&timerData->numberFont, c050, cheersStr, textX, textY);
  }
}

static void setLedsCheers()
{
  led_t leds[CONFIG_NUM_LEDS] = {{0}};

  for (uint8_t i = 0; i < CONFIG_NUM_LEDS; i++)
  {
    leds[i].r = 251;
    leds[i].g = 177;
    leds[i].b = 23;
  }

  setLeds(leds, CONFIG_NUM_LEDS);
}

static void setLedsOff()
{
  printf("Turning off LEDs\n");
  led_t leds[CONFIG_NUM_LEDS] = {{0}};
  setLeds(leds, CONFIG_NUM_LEDS);
}