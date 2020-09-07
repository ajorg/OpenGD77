/*
 * Copyright (C)2019 Roger Clark. VK3KYY / G4KYF
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <codeplug.h>
#include <HR-C6000.h>
#include <settings.h>
#include <trx.h>
#include <functions/ticks.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>

#define swap(x, y) do { typeof(x) t = x; x = y; y = t; } while(0)

enum VFO_SELECTED_FREQUENCY_INPUT  {VFO_SELECTED_FREQUENCY_INPUT_RX , VFO_SELECTED_FREQUENCY_INPUT_TX};
typedef enum vfoScreenOperationMode {VFO_SCREEN_OPERATION_NORMAL , VFO_SCREEN_OPERATION_SCAN} vfoScreenOperationMode_t;

static int selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

// internal prototypes
static void handleEvent(uiEvent_t *ev);
static void handleQuickMenuEvent(uiEvent_t *ev);
static void updateQuickMenuScreen(bool isFirstRun);
static void update_frequency(int frequency, bool announceImmediately);
static void stepFrequency(int increment);
static void loadContact(void);
static void toneScan(void);
static void scanning(void);
static void initScan(void);
static void uiVFOUpdateTrxID(void );
static void setCurrentFreqToScanLimits(void);
static void handleUpKey(uiEvent_t *ev);

static bool isDisplayingQSOData = false;

static int16_t newChannelIndex = 0;

bool scanToneActive = false;//tone scan active flag  (CTCSS/DCS)
static const int SCAN_TONE_INTERVAL = 200;//time between each tone for lowest tone. (higher tones take less time.)
static int32_t scanToneIndex = 0;
static CSSTypes_t scanToneType = CSS_CTCSS;
static uint16_t prevCSSTone = (CODEPLUG_CSS_NONE - 1);
static uint16_t toneScanCSS = TONE_SCAN_ALL;

static bool displayChannelSettings;
static bool reverseRepeater;
static int prevDisplayQSODataState;
static vfoScreenOperationMode_t screenOperationMode[2] = { VFO_SCREEN_OPERATION_NORMAL, VFO_SCREEN_OPERATION_NORMAL };// For VFO A and B

static menuStatus_t menuVFOExitStatus = MENU_STATUS_SUCCESS;
static menuStatus_t menuQuickVFOExitStatus = MENU_STATUS_SUCCESS;


#if defined(PLATFORM_RD5R)
const int RX_FREQ_Y_POS = 31;
const int TX_FREQ_Y_POS = 40;

const int CONTACT_TX_Y_POS = 28;
const int CONTACT_TX_FRAME_Y_POS = 26;
const int CONTACT_Y_POS_OFFSET = 2;
#else
const int RX_FREQ_Y_POS = 32;
const int TX_FREQ_Y_POS = 48;

const int CONTACT_TX_Y_POS = 34;
const int CONTACT_TX_FRAME_Y_POS = 34;
const int CONTACT_Y_POS_OFFSET = 0;
#endif


// Public interface
menuStatus_t uiVFOMode(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0, sqm = 0, curm = 0;

	if (isFirstRun)
	{
		freq_enter_idx = 0;

		isDisplayingQSOData = false;
		reverseRepeater = false;
		displaySquelch = false;
		settingsSet(nonVolatileSettings.initialMenuNumber, UI_VFO_MODE);
		prevDisplayQSODataState = QSO_DISPLAY_IDLE;
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];

		settingsCurrentChannelNumber = -1;// This is not a regular channel. Its the special VFO channel!
		displayChannelSettings = false;

		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);

		if (!inhibitInitialVoicePrompt)
		{
			inhibitInitialVoicePrompt = false;
			announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ,
					((menuControlData.stack[menuControlData.stackPosition + 1] == UI_TX_SCREEN) || (menuControlData.stack[menuControlData.stackPosition + 1] == UI_PRIVATE_CALL)) ? PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY : PROMPT_THRESHOLD_3);
			menuControlData.stack[menuControlData.stackPosition + 1] = 0;
		}

		//Need to load the Rx group if specified even if TG is currently overridden as we may need it later when the left or right button is pressed
		if (currentChannelData->rxGroupList != 0)
		{
			codeplugRxGroupGetDataForIndex(currentChannelData->rxGroupList, &currentRxGroupData);
		}

		if (currentChannelData->chMode == RADIO_MODE_ANALOG)
		{
			trxSetModeAndBandwidth(currentChannelData->chMode, ((currentChannelData->flag4 & 0x02) == 0x02));
			if (!scanToneActive)
			{
				trxSetRxCSS(currentChannelData->rxTone);
			}

			if (scanActive == false)
			{
				scanState = SCAN_SCANNING;
			}
		}
		else
		{
			trxSetDMRColourCode(currentChannelData->txColor);
			trxSetModeAndBandwidth(currentChannelData->chMode, false);

			if (nonVolatileSettings.overrideTG == 0)
			{
				if (currentChannelData->rxGroupList != 0)
				{
					loadContact();

					// Check whether the contact data seems valid
					if ((currentContactData.name[0] == 0) || (currentContactData.tgNumber == 0) || (currentContactData.tgNumber > 9999999))
					{
						settingsSet(nonVolatileSettings.overrideTG, 9);// If the VFO does not have an Rx Group list assigned to it. We can't get a TG from the codeplug. So use TG 9.
						trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
						trxSetDMRTimeSlot(((currentChannelData->flag2 & 0x40) != 0));
					}
					else
					{
						trxTalkGroupOrPcId = currentContactData.tgNumber;
						trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);
					}
				}
				else
				{
					settingsSet(nonVolatileSettings.overrideTG, 9);// If the VFO does not have an Rx Group list assigned to it. We can't get a TG from the codeplug. So use TG 9.
				}

			}
			else
			{
				trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
			}

			if (tsIsOverridden(((Channel_t)nonVolatileSettings.currentVFONumber)))
			{
				trxSetDMRTimeSlot((tsGetOverride(((Channel_t)nonVolatileSettings.currentVFONumber)) - 1));
			}
		}

		// We're in digital mode, RXing, and current talker is already at the top of last heard list,
		// hence immediately display complete contact/TG info on screen
		// This mostly happens when getting out of a menu.
		menuDisplayQSODataState = (isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);

		lastHeardClearLastID();
		reset_freq_enter_digits();
		displayLightTrigger();
		uiVFOModeUpdateScreen(0);
		settingsSetVFODirty();

		menuVFOExitStatus = MENU_STATUS_SUCCESS;
	}
	else
	{
		menuVFOExitStatus = MENU_STATUS_SUCCESS;

		if (ev->events == NO_EVENT)
		{
			// We are entering digits, so update the screen as we have a cursor to blink
			if ((freq_enter_idx > 0) && ((ev->time - curm) > 300))
			{
				curm = ev->time;
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN; // Redraw will happen just below
			}

			// is there an incoming DMR signal
			if (menuDisplayQSODataState != QSO_DISPLAY_IDLE)
			{
				uiVFOModeUpdateScreen(0);
			}
			else
			{
				// Clear squelch region
				if (displaySquelch && ((ev->time - sqm) > 1000))
				{
					displaySquelch = false;
#if defined(PLATFORM_RD5R)
					ucFillRect(0, SQUELCH_BAR_Y_POS, DISPLAY_SIZE_X, 9, true);
#else
					ucClearRows(2, 4, false);
#endif
					ucRenderRows(2, 4);
				}

				if ((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
				{
					m = ev->time;

					if (scanActive && (scanState == SCAN_PAUSED))
					{
#if defined(PLATFORM_RD5R)
						ucClearRows(0, 1, false);
#else
						ucClearRows(0, 2, false);
#endif
						menuUtilityRenderHeader();
					}
					else
					{
						drawRSSIBarGraph();
					}

					// Only render the second row which contains the bar graph, if we're not scanning,
					// as there is no need to redraw the rest of the screen
					ucRenderRows(((scanActive && (scanState == SCAN_PAUSED)) ? 0 : 1), 2);
				}

			}

			if (scanToneActive)
			{
				toneScan();
			}

			if (scanActive)
			{
				scanning();
			}
		}
		else
		{
			if (ev->hasEvent)
			{
				if ((currentChannelData->chMode == RADIO_MODE_ANALOG) &&
						(ev->events & KEY_EVENT) && ((ev->keys.key == KEY_LEFT) || (ev->keys.key == KEY_RIGHT)))
				{
					sqm = ev->time;
				}

				// Scanning barrier
				if (scanToneActive)
				{
#if defined(PLATFORM_RD5R) // virtual ORANGE button will be implemented later, this CPP will be removed then.
					if ((ev->keys.key != 0) && (ev->keys.event & KEY_MOD_UP))
#else
					// PTT key is already handled in main().
					if (((ev->events & BUTTON_EVENT) && BUTTONCHECK_DOWN(ev, BUTTON_ORANGE)) ||
							((ev->keys.key != 0) && (ev->keys.event & KEY_MOD_UP)))
#endif
					{
						uiVFOModeStopScanning();
					}

					return MENU_STATUS_SUCCESS;
				}

				handleEvent(ev);
			}

		}
	}
	return menuVFOExitStatus;
}

void uiVFOModeUpdateScreen(int txTimeSecs)
{
	static bool blink = false;
	static uint32_t blinkTime = 0;
	static const int bufferLen = 17;
	char buffer[bufferLen];

	// Only render the header, then wait for the next run
	// Otherwise the screen could remain blank if TG and PC are == 0
	// since menuDisplayQSODataState won't be set to QSO_DISPLAY_IDLE
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (HRC6000GetReceivedTgOrPcId() == 0) &&
			((menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA) || (menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
#if defined(PLATFORM_RD5R)
		ucClearRows(0, 1, false);
#else
		ucClearRows(0, 2, false);
#endif
		menuUtilityRenderHeader();
		ucRenderRows(0, 2);
		return;
	}

	// We're currently displaying details, and it shouldn't be overridden by QSO data
	if (displayChannelSettings && ((menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA)
			|| (menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		// We will not restore the previous QSO Data as a new caller just arose.
		prevDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	}

	ucClearBuf();
	menuUtilityRenderHeader();

	switch(menuDisplayQSODataState)
	{
		case QSO_DISPLAY_DEFAULT_SCREEN:
			prevDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			isDisplayingQSOData=false;
			menuUtilityReceivedPcId = 0x00;
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if (nonVolatileSettings.overrideTG != 0)
				{
					buildTgOrPCDisplayName(buffer,bufferLen);

					if (trxTransmissionEnabled)
					{
						ucDrawRect(0, CONTACT_TX_FRAME_Y_POS, DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT, true);
					}
					else
					{
						ucDrawRect(0, CONTACT_Y_POS, DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT, true);
					}
				}
				else
				{
					codeplugUtilConvertBufToString(currentContactData.name, buffer, 16);
				}

				buffer[bufferLen - 1] = 0;

				if (trxTransmissionEnabled)
				{
					ucPrintCentered(CONTACT_TX_Y_POS, buffer, FONT_SIZE_3);
				}
				else
				{
					ucPrintCentered(CONTACT_Y_POS + CONTACT_Y_POS_OFFSET, buffer, FONT_SIZE_3);
				}
			}
			else
			{
				// Display some channel settings
				if (displayChannelSettings)
				{
					printToneAndSquelch();
				}

				// Squelch will be cleared later, 1s after last change
				if(displaySquelch && !displayChannelSettings)
				{
					static const int xbar = 74; // 128 - (51 /* max squelch px */ + 3);

					strncpy(buffer, currentLanguage->squelch, 9);
					buffer[8] = 0; // Avoid overlap with bargraph
					// Center squelch word between col0 and bargraph, if possible.
					ucPrintAt(0 + ((strlen(buffer) * 8) < xbar - 2 ? (((xbar - 2) - (strlen(buffer) * 8)) >> 1) : 0), SQUELCH_BAR_Y_POS, buffer, FONT_SIZE_3);

					int bargraph = 1 + ((currentChannelData->sql - 1) * 5) /2;
					ucDrawRect(xbar - 2, SQUELCH_BAR_Y_POS, 55, SQUELCH_BAR_H + 4, true);
					ucFillRect(xbar, SQUELCH_BAR_Y_POS + 2, bargraph, SQUELCH_BAR_H, false);
				}

				// SK1 is pressed, we don't want to clear the first info row after 1s
				if (displayChannelSettings && displaySquelch)
				{
					displaySquelch = false;
				}

				if(scanToneActive)
				{
					switch (scanToneType)
					{
						case CSS_CTCSS:
							sprintf(buffer, "CTCSS %3d.%dHz", currentChannelData->rxTone / 10, currentChannelData->rxTone % 10);
							break;
						case CSS_DCS:
							sprintf(buffer, "DCS D%03oN", currentChannelData->rxTone & 0777);
							break;
						case CSS_DCS_INVERTED:
							sprintf(buffer, "DCS D%03oI", currentChannelData->rxTone & 0777);
							break;
						default:
							sprintf(buffer, "%s", "TONE ERROR");
							break;
					}

					ucPrintCentered(16, buffer, FONT_SIZE_3);
				}

			}

			if (freq_enter_idx == 0)
			{
				if (!trxTransmissionEnabled)
				{
					// if CC scan is active, Rx freq is moved down to the Tx location,
					// as Contact Info will be displayed here
					printFrequency(false, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_RX), RX_FREQ_Y_POS,
							(reverseRepeater ? currentChannelData->txFreq : currentChannelData->rxFreq), true, screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN);
				}
				else
				{
					// Squelch is displayed, PTT was pressed
					// Clear its region
					if (displaySquelch)
					{
						displaySquelch = false;
#if defined(PLATFORM_RD5R)
						ucFillRect(0, SQUELCH_BAR_Y_POS, DISPLAY_SIZE_X, 9, true);
#else
						ucClearRows(2, 4, false);
#endif
					}
					snprintf(buffer, bufferLen, " %d ", txTimeSecs);
					ucPrintCentered(TX_TIMER_Y_OFFSET, buffer, FONT_SIZE_4);
				}

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL || trxTransmissionEnabled)
				{
					printFrequency(true, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX || trxTransmissionEnabled), TX_FREQ_Y_POS,
							(reverseRepeater ? currentChannelData->rxFreq : currentChannelData->txFreq), true, false);
				}
				else
				{
					// Low/High scanning freqs
					snprintf(buffer, bufferLen, "%ld.%03ld", nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100);
					buffer[bufferLen - 1] = 0;

					ucPrintAt(2, TX_FREQ_Y_POS, buffer, FONT_SIZE_3);

					snprintf(buffer, bufferLen, "%ld.%03ld", nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100);
					buffer[bufferLen - 1] = 0;
					ucPrintAt(DISPLAY_SIZE_X - ((7 * 8) + 2), TX_FREQ_Y_POS, buffer, FONT_SIZE_3);
					// Scanning direction arrow
					static const int scanDirArrow[2][6] = {
							{ // Down
									59, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) - 1),
									67, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
									67, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1)
							}, // Up
							{
									59, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1),
									59, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
									67, (TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT / 2) - 1)
							}
					};

					ucFillTriangle(scanDirArrow[(scanDirection > 0)][0], scanDirArrow[(scanDirection > 0)][1],
								   scanDirArrow[(scanDirection > 0)][2], scanDirArrow[(scanDirection > 0)][3],
								   scanDirArrow[(scanDirection > 0)][4], scanDirArrow[(scanDirection > 0)][5], true);
				}
			}
			else // Entering digits
			{
				int8_t xCursor = -1;
				int8_t yCursor = -1;
				int labelsVOffset =
#if defined(PLATFORM_RD5R)
						4;
#else
						0;
#endif

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
#if defined(PLATFORM_RD5R)
					const char *FREQ_DISP_STR = "%c%c%c.%c%c%c%c%c";
#else
					const char *FREQ_DISP_STR = "%c%c%c.%c%c%c%c%c MHz";
#endif

					snprintf(buffer, bufferLen, FREQ_DISP_STR, freq_enter_digits[0], freq_enter_digits[1], freq_enter_digits[2],
							freq_enter_digits[3], freq_enter_digits[4], freq_enter_digits[5], freq_enter_digits[6], freq_enter_digits[7]);

					ucPrintCentered((selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX) ? TX_FREQ_Y_POS : RX_FREQ_Y_POS, buffer, FONT_SIZE_3);

					// Cursor
					if (freq_enter_idx < 8)
					{
						xCursor = ((DISPLAY_SIZE_X - (strlen(buffer) * 8)) >> 1) + ((freq_enter_idx + ((freq_enter_idx > 2) ? 1 : 0)) * 8);
						yCursor = ((selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX) ? TX_FREQ_Y_POS : RX_FREQ_Y_POS) + (FONT_SIZE_3_HEIGHT - 2);
					}
				}
				else
				{
					uint8_t hiX = DISPLAY_SIZE_X - ((7 * 8) + 2);
					ucPrintAt(5, RX_FREQ_Y_POS - labelsVOffset, "Low", FONT_SIZE_3);
					ucDrawFastVLine(0, RX_FREQ_Y_POS - labelsVOffset, ((FONT_SIZE_3_HEIGHT * 2) + labelsVOffset), true);
					ucDrawFastHLine(1, TX_FREQ_Y_POS - (labelsVOffset / 2), 57, true);

					sprintf(buffer, "%c%c%c.%c%c%c", freq_enter_digits[0], freq_enter_digits[1], freq_enter_digits[2],
													 freq_enter_digits[3], freq_enter_digits[4], freq_enter_digits[5]);

					ucPrintAt(2, TX_FREQ_Y_POS, buffer, FONT_SIZE_3);

					ucPrintAt(73, RX_FREQ_Y_POS - labelsVOffset, "High", FONT_SIZE_3);
					ucDrawFastVLine(68, RX_FREQ_Y_POS - labelsVOffset, ((FONT_SIZE_3_HEIGHT * 2) + labelsVOffset), true);
					ucDrawFastHLine(69, TX_FREQ_Y_POS - (labelsVOffset / 2), 57, true);

					sprintf(buffer, "%c%c%c.%c%c%c", freq_enter_digits[6], freq_enter_digits[7], freq_enter_digits[8],
													 freq_enter_digits[9], freq_enter_digits[10], freq_enter_digits[11]);

					ucPrintAt(hiX, TX_FREQ_Y_POS, buffer, FONT_SIZE_3);

					// Cursor
					if (freq_enter_idx < 12)
					{
						xCursor = ((freq_enter_idx < 6) ? 10 : hiX) // X start
								+ (((freq_enter_idx < 6) ? (freq_enter_idx - 1) : (freq_enter_idx - 7)) * 8) // Length
								+ ((freq_enter_idx > 2 ? (freq_enter_idx > 8 ? 2 : 1) : 0) * 8); // MHz/kHz separator(s)

						yCursor = TX_FREQ_Y_POS + (FONT_SIZE_3_HEIGHT - 2);
					}
				}

				if ((xCursor >= 0) && (yCursor >= 0))
				{
					ucDrawFastHLine(xCursor + 1, yCursor, 6, blink);

					if ((fw_millis() - blinkTime) > 500)
					{
						blinkTime = fw_millis();
						blink = !blink;
					}
				}

			}

			ucRender();
			break;

		case QSO_DISPLAY_CALLER_DATA:
			displayLightTrigger();
		case QSO_DISPLAY_CALLER_DATA_UPDATE:
			prevDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
			isDisplayingQSOData = true;
			displayChannelSettings = false;
			menuUtilityRenderQSOData();
			ucRender();
			break;
	}

	menuDisplayQSODataState = QSO_DISPLAY_IDLE;
}

void uiVFOModeStopScanning(void)
{
	if (scanToneActive)
	{
		if (prevCSSTone != (CODEPLUG_CSS_NONE - 1))
		{
			currentChannelData->rxTone = prevCSSTone;
			prevCSSTone = (CODEPLUG_CSS_NONE - 1);
		}

		trxSetRxCSS(currentChannelData->rxTone);
		scanToneActive = false;
	}
	scanActive = false;
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	uiVFOModeUpdateScreen(0); // Needs to redraw the screen now
	displayLightTrigger();
}

static void update_frequency(int frequency, bool announceImmediately)
{
	if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
	{
		if (trxGetBandFromFrequency(frequency) != -1)
		{
			currentChannelData->txFreq = frequency;
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
			soundSetMelody(MELODY_ACK_BEEP);
		}
	}
	else
	{
		int deltaFrequency = frequency - currentChannelData->rxFreq;
		if (trxGetBandFromFrequency(frequency) != -1)
		{
			currentChannelData->rxFreq = frequency;
			currentChannelData->txFreq = currentChannelData->txFreq + deltaFrequency;
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);

			if (trxGetBandFromFrequency(currentChannelData->txFreq) != -1)
			{
				soundSetMelody(MELODY_ACK_BEEP);
			}
			else
			{
				currentChannelData->txFreq = frequency;
				soundSetMelody(MELODY_ERROR_BEEP);
			}
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
	}
	announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, announceImmediately);

	menuClearPrivateCall();
	settingsSetVFODirty();
}

static void checkAndFixIndexInRxGroup(void)
{
	if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]
			> (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1))
	{
		settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 0);
	}
}

static void loadContact(void)
{
	// Check if this channel has an Rx Group
	if (currentRxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup)
	{
		codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]],&currentContactData);
	}
	else
	{
		codeplugContactGetDataForIndex(currentChannelData->contact,&currentContactData);
	}
}

static void handleEvent(uiEvent_t *ev)
{
	displayLightTrigger();

	if (scanActive && (ev->events & KEY_EVENT))
	{
		if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0)
		{
			// Right key sets the current frequency as a 'nuisance' frequency.
			if((scanState == SCAN_PAUSED) && (ev->keys.key == KEY_RIGHT))
			{
				nuisanceDelete[nuisanceDeleteIndex++] = currentChannelData->rxFreq;
				if(nuisanceDeleteIndex > (MAX_ZONE_SCAN_NUISANCE_CHANNELS - 1))
				{
					nuisanceDeleteIndex = 0; //rolling list of last MAX_NUISANCE_CHANNELS deletes.
				}
				scanTimer = SCAN_SKIP_CHANNEL_INTERVAL;//force scan to continue;
				scanState = SCAN_SCANNING;
				keyboardReset();
				return;
			}

			// Left key reverses the scan direction
			if ((scanState == SCAN_SCANNING) && (ev->keys.key == KEY_LEFT))
			{
				scanDirection *= -1;
				keyboardReset();
				return;
			}
		}

		// Stop the scan on any key except UP without Shift (allows scan to be manually continued)
		// or SK2 on its own (allows Backlight to be triggered)
		if (((ev->keys.key == KEY_UP) && BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) == false)
		{
			uiVFOModeStopScanning();
			keyboardReset();
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == START_SCANNING)
		{
			initScan();
			setCurrentFreqToScanLimits();
			scanActive = true;
			return;
		}
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
		{
			if (!voicePromptIsActive)
			{
				voicePromptsPlay();
			}
			else
			{
				voicePromptsTerminate();
			}
		}

#if ! defined(PLATFORM_RD5R)
		// Stop the scan if any button is pressed.
		if (scanActive && BUTTONCHECK_DOWN(ev, BUTTON_ORANGE))
		{
			uiVFOModeStopScanning();
			return;
		}
#endif

		uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);

		// If Blue button is pressed during reception it sets the Tx TG to the incoming TG
		if (isDisplayingQSOData && BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (trxGetMode() == RADIO_MODE_DIGITAL) &&
					((trxTalkGroupOrPcId != tg) ||
					((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot())) ||
					(trxGetDMRColourCode() != currentChannelData->txColor)))
		{
			lastHeardClearLastID();

			// Set TS to overriden TS
			if ((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot()))
			{
				trxSetDMRTimeSlot(dmrMonitorCapturedTS);
				tsSetOverride(((Channel_t)nonVolatileSettings.currentVFONumber), (dmrMonitorCapturedTS + 1));
			}

			if (trxTalkGroupOrPcId != tg)
			{
				trxTalkGroupOrPcId = tg;
				settingsSet(nonVolatileSettings.overrideTG, trxTalkGroupOrPcId);
			}

			currentChannelData->txColor = trxGetDMRColourCode();// Set the CC to the current CC, which may have been determined by the CC finding algorithm in C6000.c

			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			return;
		}

		if ((reverseRepeater == false) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) && BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
		{
			trxSetFrequency(currentChannelData->txFreq, currentChannelData->rxFreq, DMR_MODE_ACTIVE);// Swap Tx and Rx freqs but force DMR Active
			reverseRepeater = true;
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			return;
		}
		else if ((reverseRepeater == true) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
			reverseRepeater = false;

			// We are still displaying channel details (SK1 has been released), force to update the screen
			if (displayChannelSettings)
			{
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiVFOModeUpdateScreen(0);
			}

			return;
		}
		// Display channel settings (CTCSS, Squelch) while SK1 is pressed
		else if ((displayChannelSettings == false) && BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			int prevQSODisp = prevDisplayQSODataState;

			displayChannelSettings = true;
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			prevDisplayQSODataState = prevQSODisp;
			return;
		}
		else if ((displayChannelSettings == true) && BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0)
		{
			displayChannelSettings = false;
			menuDisplayQSODataState = prevDisplayQSODataState;

			// Maybe QSO State has been overridden, double check if we could now
			// display QSO Data
			if (menuDisplayQSODataState == QSO_DISPLAY_DEFAULT_SCREEN)
			{
				if (isQSODataAvailableForCurrentTalker())
				{
					menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
				}
			}

			// Leaving Channel Details disable reverse repeater feature
			if (reverseRepeater)
			{
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				reverseRepeater = false;
			}

			uiVFOModeUpdateScreen(0);
			return;
		}

#if !defined(PLATFORM_RD5R)
		if (BUTTONCHECK_DOWN(ev, BUTTON_ORANGE))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				announceItem(PROMPT_SEQUENCE_BATTERY, AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
			}
			else
			{
				menuSystemPushNewMenu(UI_VFO_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
			}

			return;
		}
#endif
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				menuSystemPushNewMenu(MENU_CHANNEL_DETAILS);
				reset_freq_enter_digits();
				return;
			}
			else
			{
				if (freq_enter_idx == 0)
				{
					menuSystemPushNewMenu(MENU_MAIN_MENU);
					return;
				}
			}
		}

		if (freq_enter_idx == 0)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						menuSystemPushNewMenu(MENU_CONTACT_QUICKLIST);
					}
					else
					{
						menuSystemPushNewMenu(MENU_NUMERICAL_ENTRY);
					}
				}
				return;
			}

			if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						currentChannelData->chMode = RADIO_MODE_DIGITAL;
						trxSetModeAndBandwidth(currentChannelData->chMode, false);
						checkAndFixIndexInRxGroup();
						// Check if the contact data for the VFO has previous been loaded
						if (currentContactData.name[0] == 0x00)
						{
							loadContact();
						}
						menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
					}
					else
					{
						currentChannelData->chMode = RADIO_MODE_ANALOG;
						trxSetModeAndBandwidth(currentChannelData->chMode, ((currentChannelData->flag4 & 0x02) == 0x02));
					}
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// Toggle TimeSlot
						trxSetDMRTimeSlot(1 - trxGetDMRTimeSlot());
						tsSetOverride(((Channel_t)nonVolatileSettings.currentVFONumber), (trxGetDMRTimeSlot() + 1));

						disableAudioAmp(AUDIO_AMP_MODE_RF);
						clearActiveDMRID();
						lastHeardClearLastID();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiVFOModeUpdateScreen(0);

						if (trxGetDMRTimeSlot() == 0)
						{
							menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
						}
					}
					else
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_STAR))
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					tsSetOverride(((Channel_t)nonVolatileSettings.currentVFONumber), TS_NO_OVERRIDE);
					// Check if this channel has an Rx Group
					if ((currentRxGroupData.name[0] != 0) &&
							(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
					{
						codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]], &currentContactData);
					}
					else
					{
						codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
					}

					trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);

					clearActiveDMRID();
					lastHeardClearLastID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
					uiVFOModeUpdateScreen(0);
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_DOWN))
			{
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					// Don't permit to switch from RX/TX while scanning
					if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN)
					{
						selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_TX;
					}
				}
				else
				{
					stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] * -1);
					uiVFOModeUpdateScreen(0);
					settingsSetVFODirty();
				}
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_DOWN))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{
					screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_NORMAL;
					uiVFOModeStopScanning();
					return;
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_UP))
			{
				handleUpKey(ev);
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_UP) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN)
				{
					initScan();
					return;
				}
				else
				{
					setCurrentFreqToScanLimits();
					if (!scanActive)
					{
						scanActive=true;
					}
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (menuUtilityTgBeforePcMode != 0))
				{
					settingsSet(nonVolatileSettings.overrideTG, menuUtilityTgBeforePcMode);
					uiVFOUpdateTrxID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
					menuClearPrivateCall();
					uiVFOModeUpdateScreen(0);
					return;// The event has been handled
				}

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)
				if ((trxGetMode() == RADIO_MODE_DIGITAL) && (getAudioAmpStatus() & AUDIO_AMP_MODE_RF))
				{
					clearActiveDMRID();
				}
				menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;// Audible signal that the Channel screen has been selected
				menuSystemSetCurrentMenu(UI_CHANNEL_MODE);
#endif
				return;
			}
#if defined(PLATFORM_DM1801) || defined(PLATFORM_RD5R)
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR))
			{
				if ((trxGetMode() == RADIO_MODE_DIGITAL) && (getAudioAmpStatus() & AUDIO_AMP_MODE_RF))
				{
					clearActiveDMRID();
				}
				menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;// Audible signal that the Channel screen has been selected
				menuSystemSetCurrentMenu(UI_CHANNEL_MODE);
				return;
			}
#endif
#if defined(PLATFORM_RD5R)
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_VFO_MR) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				announceItem(PROMPT_SEQUENCE_BATTERY, AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
			}
			else
			{
				menuSystemPushNewMenu(UI_VFO_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
			}

			return;
		}
#endif
#if defined(PLATFORM_DM1801)
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_A_B))
			{
				settingsSet(nonVolatileSettings.currentVFONumber, (1 - nonVolatileSettings.currentVFONumber));// Switch to other VFO
				currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuSystemPopAllAndDisplayRootMenu(); // Force to set all TX/RX settings.
				return;
			}
#endif
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RIGHT))
			{
				// Long press allows the 5W+ power setting to be selected immediately
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (nonVolatileSettings.txPowerLevel == (MAX_POWER_SETTING_NUM - 1))
					{
						increasePowerLevel();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiVFOModeUpdateScreen(0);
					}
				}
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (nonVolatileSettings.txPowerLevel < (MAX_POWER_SETTING_NUM - 1))
					{
						increasePowerLevel();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiVFOModeUpdateScreen(0);
					}
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (nonVolatileSettings.overrideTG == 0)
						{
							settingsIncrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 1);
							checkAndFixIndexInRxGroup();
						}

						if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] == 0)
						{
							menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
						}
						settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
						menuClearPrivateCall();
						uiVFOUpdateTrxID();
						// We're in digital mode, RXing, and current talker is already at the top of last heard list,
						// hence immediately display complete contact/TG info on screen
						menuDisplayQSODataState = (isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
						uiVFOModeUpdateScreen(0);
						announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
					}
					else
					{
						if(currentChannelData->sql == 0) //If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
						}
						else
						{
							if (currentChannelData->sql < CODEPLUG_MAX_VARIABLE_SQUELCH)

							{
								currentChannelData->sql++;
							}
						}

						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						displaySquelch = true;
						uiVFOModeUpdateScreen(0);
					}
				}
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (nonVolatileSettings.txPowerLevel > 0)
					{
						decreasePowerLevel();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiVFOModeUpdateScreen(0);
					}

					if (nonVolatileSettings.txPowerLevel == 0)
					{
						menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
					}
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// To Do change TG in on same channel freq
						if (nonVolatileSettings.overrideTG == 0)
						{
							settingsDecrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 1);
							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < 0)
							{
								settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber],
										(currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1));
							}

							if(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] == 0)
							{
								menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
							}
						}
						settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
						menuClearPrivateCall();
						uiVFOUpdateTrxID();
						// We're in digital mode, RXing, and current talker is already at the top of last heard list,
						// hence immediately display complete contact/TG info on screen
						menuDisplayQSODataState = (isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
						uiVFOModeUpdateScreen(0);
						announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
					}
					else
					{
						if(currentChannelData->sql == 0) //If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
						}
						else
						{
							if (currentChannelData->sql > CODEPLUG_MIN_VARIABLE_SQUELCH)
							{
								currentChannelData->sql--;
							}
						}
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						displaySquelch = true;
						uiVFOModeUpdateScreen(0);
					}
				}
			}
		}
		else // (freq_enter_idx == 0)
		{
			if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
			{
				freq_enter_idx--;
				freq_enter_digits[freq_enter_idx] = '-';
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				reset_freq_enter_digits();
				soundSetMelody(MELODY_NACK_BEEP);
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					int tmp_frequency = read_freq_enter_digits(0, 8);

					if (trxGetBandFromFrequency(tmp_frequency) != -1)
					{
						update_frequency(tmp_frequency, PROMPT_THRESHOLD_3);
						reset_freq_enter_digits();
					}
					else
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}

					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				else
				{
					if (freq_enter_idx != 0)
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}
			}
		}

		if (freq_enter_idx < ((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL) ? 8 : 12))
		{
			int keyval = menuGetKeypadKeyValue(ev, true);

			if (keyval != 99)
			{
				if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
				{
					voicePromptsInit();
					voicePromptsAppendPrompt(PROMPT_0 +  keyval);
					if (freq_enter_idx == 2)
					{
						voicePromptsAppendPrompt(PROMPT_POINT);
					}
					voicePromptsPlay();
				}

				freq_enter_digits[freq_enter_idx] = (char) keyval + '0';
				freq_enter_idx++;

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					if (freq_enter_idx == 8)
					{
						int tmp_frequency = read_freq_enter_digits(0, 8);

						if (trxGetBandFromFrequency(tmp_frequency) != -1)
						{
							update_frequency(tmp_frequency, AUDIO_PROMPT_MODE_BEEP);
							reset_freq_enter_digits();
							soundSetMelody(MELODY_ACK_BEEP);
						}
						else
						{
							soundSetMelody(MELODY_ERROR_BEEP);
						}
					}
				}
				else
				{
					if (freq_enter_idx == 12)
					{
						int fLower=read_freq_enter_digits(0, 6)  * 100;
						int fUpper=read_freq_enter_digits(6, 12) * 100;

						if (fLower > fUpper)
						{
							swap(fLower, fUpper);
						}

						if ((trxGetBandFromFrequency(fLower) != -1) && (trxGetBandFromFrequency(fUpper) != -1) && (fLower < fUpper))
						{
							settingsSet(nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber], fLower);
							settingsSet(nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber], fUpper);

							reset_freq_enter_digits();
							soundSetMelody(MELODY_ACK_BEEP);
							uiVFOModeUpdateScreen(0);
						}
						else
						{
							soundSetMelody(MELODY_ERROR_BEEP);
						}
					}
				}

				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}
	}
}


static void handleUpKey(uiEvent_t *ev)
{
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		// Don't permit to switch from RX/TX while scanning
		if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN)
		{
			selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;
		}
	}
	else
	{
		if (scanActive)
		{
			stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] * scanDirection);
		}
		else
		{
			stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]);
		}
		uiVFOModeUpdateScreen(0);
	}
	scanTimer = 500;
	scanState = SCAN_SCANNING;
	settingsSetVFODirty();
}

static void stepFrequency(int increment)
{
	int tmp_frequencyTx;
	int tmp_frequencyRx;

	if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
	{
		tmp_frequencyTx  = currentChannelData->txFreq + increment;
		tmp_frequencyRx  = currentChannelData->rxFreq;// Needed later for the band limited checking
	}
	else
	{
		tmp_frequencyRx  = currentChannelData->rxFreq + increment;
		tmp_frequencyTx  = currentChannelData->txFreq + increment;
	}

	// Out of frequency in the current band, update freq to the next or prev band.
	if (trxGetBandFromFrequency(tmp_frequencyRx) == -1)
	{
		int band = trxGetNextOrPrevBandFromFrequency(tmp_frequencyRx, (increment > 0));

		if (band != -1)
		{
			tmp_frequencyRx = ((increment > 0) ? RADIO_FREQUENCY_BANDS[band].minFreq : RADIO_FREQUENCY_BANDS[band].maxFreq);
			tmp_frequencyTx = tmp_frequencyRx;
		}
		else
		{
			// ??
		}
	}

	if (trxGetBandFromFrequency(tmp_frequencyRx) != -1)
	{
		currentChannelData->txFreq = tmp_frequencyTx;
		currentChannelData->rxFreq = tmp_frequencyRx;
		if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_RX)
		{
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
		}

		announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);

	}
	else
	{
		soundSetMelody(MELODY_ERROR_BEEP);
	}
}

// ---------------------------------------- Quick Menu functions -------------------------------------------------------------------
enum VFO_SCREEN_QUICK_MENU_ITEMS // The last item in the list is used so that we automatically get a total number of items in the list
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R)
	VFO_SCREEN_QUICK_MENU_VFO_A_B = 0, VFO_SCREEN_QUICK_MENU_TX_SWAP_RX,
#elif defined(PLATFORM_DM1801)
	VFO_SCREEN_QUICK_MENU_TX_SWAP_RX = 0,
#endif
	VFO_SCREEN_QUICK_MENU_BOTH_TO_RX, VFO_SCREEN_QUICK_MENU_BOTH_TO_TX,
	VFO_SCREEN_QUICK_MENU_FILTER,
	VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER,
	VFO_SCREEN_QUICK_MENU_DMR_TS_FILTER,
	VFO_SCREEN_QUICK_MENU_VFO_TO_NEW, VFO_SCREEN_TONE_SCAN,
	NUM_VFO_SCREEN_QUICK_MENU_ITEMS
};

menuStatus_t uiVFOModeQuickMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		tmpQuickMenuDmrDestinationFilterLevel = nonVolatileSettings.dmrDestinationFilter;
		tmpQuickMenuDmrCcTsFilterLevel = nonVolatileSettings.dmrCcTsFilter;
		tmpQuickMenuAnalogFilterLevel = nonVolatileSettings.analogFilterLevel;

		if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
		{
			voicePromptsInit();
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendLanguageString(&currentLanguage->quick_menu);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
		}

		updateQuickMenuScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuQuickVFOExitStatus = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleQuickMenuEvent(ev);
		}
	}
	return menuQuickVFOExitStatus;
}

static void updateQuickMenuScreen(bool isFirstRun)
{
	int mNum = 0;
	static const int bufferLen = 17;
	char buf[bufferLen];
	char * const *leftSide;// initialise to please the compiler
	char * const *rightSideConst;// initialise to please the compiler
	char rightSideVar[bufferLen];
	int prompt;// For voice prompts

	ucClearBuf();
	menuDisplayTitle(currentLanguage->quick_menu);

	for(int i = -1; i <= 1; i++)
	{
		mNum = menuGetMenuOffset(NUM_VFO_SCREEN_QUICK_MENU_ITEMS, i);
		prompt = -1;// Prompt not used
		rightSideVar[0] = 0;
		rightSideConst = NULL;
		leftSide = NULL;

		switch(mNum)
		{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				sprintf(rightSideVar, "VFO:%c", ((nonVolatileSettings.currentVFONumber==0) ? 'A' : 'B'));
				break;
#endif
			case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
				prompt = PROMPT_VFO_EXCHANGE_TX_RX;
				strcpy(rightSideVar, "Tx <--> Rx");
				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
				prompt = PROMPT_VFO_COPY_RX_TO_TX;
				strcpy(rightSideVar, "Rx --> Tx");
				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
				prompt = PROMPT_VFO_COPY_TX_TO_RX;
				strcpy(rightSideVar, "Tx --> Rx");
				break;
			case VFO_SCREEN_QUICK_MENU_FILTER:

				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					leftSide = (char * const *)&currentLanguage->dmr_filter;
					if (tmpQuickMenuDmrDestinationFilterLevel == 0)
					{
						rightSideConst = (char * const *)&currentLanguage->none;
					}
					else
					{
						snprintf(rightSideVar, bufferLen, "%s", DMR_DESTINATION_FILTER_LEVELS[tmpQuickMenuDmrDestinationFilterLevel - 1]);
					}
				}
				else
				{
					leftSide = (char * const *)&currentLanguage->filter;
					if (tmpQuickMenuAnalogFilterLevel == 0)
					{
						rightSideConst = (char * const *)&currentLanguage->none;
					}
					else
					{
						snprintf(rightSideVar, bufferLen, "%s", ANALOG_FILTER_LEVELS[tmpQuickMenuAnalogFilterLevel - 1]);
					}
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER:
				leftSide = (char * const *)&currentLanguage->dmr_cc_filter;
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					rightSideConst = (tmpQuickMenuDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN)?(char * const *)&currentLanguage->on:(char * const *)&currentLanguage->off;
				}
				else
				{
					rightSideConst = (char * const *)&currentLanguage->n_a;
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_TS_FILTER:
				leftSide = (char * const *)&currentLanguage->dmr_ts_filter;
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					rightSideConst = (tmpQuickMenuDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN)?(char * const *)&currentLanguage->on:(char * const *)&currentLanguage->off;

				}
				else
				{
					rightSideConst = (char * const *)&currentLanguage->n_a;
				}
				break;
		    case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
		    	rightSideConst = (char * const *)&currentLanguage->vfoToNewChannel;
		    	break;
			case VFO_SCREEN_TONE_SCAN:
				leftSide = (char * const *)&currentLanguage->tone_scan;
				if(trxGetMode() == RADIO_MODE_ANALOG)
				{
					const char *scanCSS[] = { currentLanguage->all, "CTCSS", "DCS", "iDCS" };
					snprintf(rightSideVar, bufferLen, "%s", scanCSS[toneScanCSS]);
				}
				else
				{
					rightSideConst = (char * const *)&currentLanguage->n_a;
				}
				break;
			default:
				strcpy(buf, "");
				break;
		}

		if (leftSide != NULL)
		{
			snprintf(buf, bufferLen, "%s:%s", *leftSide, (rightSideVar[0] ? rightSideVar : *rightSideConst));
		}
		else
		{
			snprintf(buf, bufferLen, "%s", (rightSideVar[0] ? rightSideVar : *rightSideConst));
		}

		if ((i == 0) && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1))
		{
			if (!isFirstRun)
			{
				voicePromptsInit();
			}

			if (prompt != -1)
			{
				voicePromptsAppendPrompt(prompt);
			}
			else
			{
				if (leftSide != NULL)
				{
					voicePromptsAppendLanguageString((const char * const *)leftSide);
				}

				if (rightSideVar[0] != 0)
				{
					voicePromptsAppendString(rightSideVar);
				}
				else
				{
					voicePromptsAppendLanguageString((const char * const *)rightSideConst);
				}
			}

			voicePromptsPlay();
		}

		buf[bufferLen - 1] = 0;
		menuDisplayEntry(i, mNum, buf);
	}
	ucRender();
}

static void handleQuickMenuEvent(uiEvent_t *ev)
{
	bool isDirty = false;

	displayLightTrigger();

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		uiVFOModeStopScanning();

		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		switch(gMenusCurrentItemIndex)
		{
			case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
			{
				int tmpFreq = currentChannelData->txFreq;
				currentChannelData->txFreq = currentChannelData->rxFreq;
				currentChannelData->rxFreq = tmpFreq;
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
			}
			break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
				currentChannelData->txFreq = currentChannelData->rxFreq;
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
				currentChannelData->rxFreq = currentChannelData->txFreq;
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
				break;
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					settingsSet(nonVolatileSettings.dmrDestinationFilter, tmpQuickMenuDmrDestinationFilterLevel);
					init_digital_DMR_RX();
					disableAudioAmp(AUDIO_AMP_MODE_RF);
				}
				else
				{
					settingsSet(nonVolatileSettings.analogFilterLevel, tmpQuickMenuAnalogFilterLevel);
				}
				break;

			case VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER:
			case VFO_SCREEN_QUICK_MENU_DMR_TS_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					settingsSet(nonVolatileSettings.dmrCcTsFilter, tmpQuickMenuDmrCcTsFilterLevel);
					init_digital_DMR_RX();
					disableAudioAmp(AUDIO_AMP_MODE_RF);
				}
				break;

			case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
				//look for empty channel
				for (newChannelIndex = 1; newChannelIndex < 1024; newChannelIndex++)
				{
					if (!codeplugChannelIndexIsValid(newChannelIndex))
					{
						break;
					}
				}

				if (newChannelIndex < 1024)
				{
					//set zone to all channels and channel index to free channel found
					settingsSet(nonVolatileSettings.currentZone, (codeplugZonesGetCount() - 1));

					settingsSet(nonVolatileSettings.currentChannelIndexInAllZone, newChannelIndex);

					settingsCurrentChannelNumber = newChannelIndex;

					memcpy(&channelScreenChannelData.rxFreq, &settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq, sizeof(struct_codeplugChannel_t) - 16);// Don't copy the name of the vfo, which are in the first 16 bytes

					snprintf((char *) &channelScreenChannelData.name, 16, "%s %d", currentLanguage->new_channel, newChannelIndex);

					codeplugChannelSaveDataForIndex(newChannelIndex, &channelScreenChannelData);

					//Set channel index as valid
					codeplugChannelIndexSetValid(newChannelIndex);
					//settingsSet(nonVolatileSettings.overrideTG, 0); // remove any TG override
					settingsSet(nonVolatileSettings.currentChannelIndexInZone, 0);// Since we are switching zones the channel index should be reset
					channelScreenChannelData.rxFreq = 0x00; // Flag to the Channel screen that the channel data is now invalid and needs to be reloaded

					//copy current channel from vfo to channel
					settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[0], nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]);

					//copy current TS
					tsSetOverride(((Channel_t)nonVolatileSettings.currentVFONumber), (trxGetDMRTimeSlot() + 1));

					inhibitInitialVoicePrompt = true;
					menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, true);

					soundSetMelody(MELODY_ACK_BEEP);

					return;
				}
				else
				{
					soundSetMelody(MELODY_ERROR_BEEP);
				}
				break;
			case VFO_SCREEN_TONE_SCAN:
				if (trxGetMode() == RADIO_MODE_ANALOG)
				{
					scanToneActive = true;
					scanTimer = SCAN_TONE_INTERVAL;
					scanToneIndex = 0;
					scanToneType = ((toneScanCSS != TONE_SCAN_ALL) ? toneScanCSS : CSS_CTCSS);
					prevCSSTone = currentChannelData->rxTone;
					currentChannelData->rxTone = cssGetTone(scanToneIndex, scanToneType);
					trxSetRxCSS(currentChannelData->rxTone);
					disableAudioAmp(AUDIO_AMP_MODE_RF);
				}
				break;
		}
		menuSystemPopPreviousMenu();
		return;
	}
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R)
 #if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)
	else if (((ev->events & BUTTON_EVENT) && BUTTONCHECK_DOWN(ev, BUTTON_ORANGE)) && (gMenusCurrentItemIndex == VFO_SCREEN_QUICK_MENU_VFO_A_B))
 #elif defined(PLATFORM_RD5R)
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR) && (gMenusCurrentItemIndex == VFO_SCREEN_QUICK_MENU_VFO_A_B))
 #endif
	{
		settingsSet(nonVolatileSettings.currentVFONumber, (1 - nonVolatileSettings.currentVFONumber));// Switch to other VFO
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
		menuSystemPopPreviousMenu();
		if (nonVolatileSettings.currentVFONumber == 0)
		{
			// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
			ev->keys.event |= KEY_MOD_UP;
			ev->keys.key = 127;
			// End Trick

			menuQuickVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
		}
		return;
	}
#endif
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
	{
		isDirty = true;
		switch(gMenusCurrentItemIndex)
		{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				if (nonVolatileSettings.currentVFONumber == 0)
				{
					settingsIncrement(nonVolatileSettings.currentVFONumber, 1);
					currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				}
				break;
#endif
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (tmpQuickMenuDmrDestinationFilterLevel < NUM_DMR_DESTINATION_FILTER_LEVELS - 1)
					{
						tmpQuickMenuDmrDestinationFilterLevel++;
					}
				}
				else
				{
					if (tmpQuickMenuAnalogFilterLevel < NUM_ANALOG_FILTER_LEVELS - 1)
					{
						tmpQuickMenuAnalogFilterLevel++;
					}
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (!(tmpQuickMenuDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN))
					{
						tmpQuickMenuDmrCcTsFilterLevel |= DMR_CC_FILTER_PATTERN;
					}
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_TS_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (!(tmpQuickMenuDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN))
					{
						tmpQuickMenuDmrCcTsFilterLevel |= DMR_TS_FILTER_PATTERN;
					}
				}
				break;
			case VFO_SCREEN_TONE_SCAN:
				if (trxGetMode() == RADIO_MODE_ANALOG)
				{
					if (toneScanCSS < TONE_SCAN_DCS_INVERTED)
					{
						toneScanCSS++;
					}
				}
				break;
			}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
	{
		isDirty = true;
		switch(gMenusCurrentItemIndex)
		{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				if (nonVolatileSettings.currentVFONumber == 1)
				{
					settingsDecrement(nonVolatileSettings.currentVFONumber, 1);
					currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				}
				menuQuickVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
				break;
#endif
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (tmpQuickMenuDmrDestinationFilterLevel > DMR_DESTINATION_FILTER_NONE)
					{
						tmpQuickMenuDmrDestinationFilterLevel--;
					}
				}
				else
				{
					if (tmpQuickMenuAnalogFilterLevel > ANALOG_FILTER_NONE)
					{
						tmpQuickMenuAnalogFilterLevel--;
					}
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (tmpQuickMenuDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN)
					{
						tmpQuickMenuDmrCcTsFilterLevel &= ~DMR_CC_FILTER_PATTERN;
					}
				}
				break;
			case VFO_SCREEN_QUICK_MENU_DMR_TS_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (tmpQuickMenuDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN)
					{
						tmpQuickMenuDmrCcTsFilterLevel &= ~DMR_TS_FILTER_PATTERN;
					}
				}
				break;
			case VFO_SCREEN_TONE_SCAN:
				if (trxGetMode() == RADIO_MODE_ANALOG)
				{
					if (toneScanCSS > TONE_SCAN_ALL)
					{
						toneScanCSS--;
					}
				}
				break;
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		isDirty = true;
		menuSystemMenuIncrement(&gMenusCurrentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
		menuQuickVFOExitStatus |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		isDirty = true;
		menuSystemMenuDecrement(&gMenusCurrentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
		menuQuickVFOExitStatus |= MENU_STATUS_LIST_TYPE;
	}

	if (isDirty)
	{
		updateQuickMenuScreen(false);
	}
}

bool uiVFOModeIsScanning(void)
{
	return (scanToneActive || scanActive);
}

static void toneScan(void)
{
	if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
	{
		currentChannelData->txTone = currentChannelData->rxTone;
		menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		uiVFOModeUpdateScreen(0);
		prevCSSTone = (CODEPLUG_CSS_NONE - 1);
		scanToneActive = false;
		return;
	}

	if (scanTimer > 0)
	{
		scanTimer--;
	}
	else
	{
		cssIncrement(&currentChannelData->rxTone, &scanToneIndex, &scanToneType, true, (toneScanCSS != TONE_SCAN_ALL));
		trxAT1846RxOff();
		trxSetRxCSS(currentChannelData->rxTone);
		scanTimer = ((scanToneType == CSS_CTCSS) ? (SCAN_TONE_INTERVAL - (scanToneIndex * 2)) : SCAN_TONE_INTERVAL);
		trx_measure_count = 0;//synchronize the measurement with the scan.
		trxAT1846RxOn();
		menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		uiVFOModeUpdateScreen(0);
	}
}

static void uiVFOUpdateTrxID(void)
{
	if (nonVolatileSettings.overrideTG != 0)
	{
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}
	else
	{
		tsSetOverride(((Channel_t)nonVolatileSettings.currentVFONumber), TS_NO_OVERRIDE);

		// Check if this channel has an Rx Group
		if ((currentRxGroupData.name[0] != 0) && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
		{
			codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]], &currentContactData);
		}
		else
		{
			codeplugContactGetDataForIndex(currentChannelData->contact,&currentContactData);
		}

		trxTalkGroupOrPcId = currentContactData.tgNumber;
		if (currentContactData.callType == CONTACT_CALLTYPE_PC)
		{
			trxTalkGroupOrPcId |= (PC_CALL_FLAG << 24);
		}

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);
	}
	lastHeardClearLastID();
	menuClearPrivateCall();
}

static void setCurrentFreqToScanLimits(void)
{
	if((currentChannelData->rxFreq < nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]) || (currentChannelData->rxFreq > nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber]))    //if we are not already inside the Low and High Limit freqs then move to the low limit.
	{
		int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
		currentChannelData->rxFreq = nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
		currentChannelData->txFreq = currentChannelData->rxFreq + offset;
		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
		announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
	}
}

static void initScan(void)
{
	screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_SCAN;
	scanDirection = 1;

	// If scan limits have not been defined. Set them to the current Rx freq -1MHz to +1MHz
	if ((nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] == 0) || (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] == 0))
	{
		settingsSet(nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber], (currentChannelData->rxFreq - 100000));
		settingsSet(nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber], (currentChannelData->rxFreq + 100000));
	}

	//clear all nuisance delete channels at start of scanning
	for(int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
	{
		nuisanceDelete[i] = -1;
	}
	nuisanceDeleteIndex = 0;

	selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

	scanTimer = 500;
	scanState = SCAN_SCANNING;
	menuSystemPopAllAndDisplaySpecificRootMenu(UI_VFO_MODE, true);
}

static void scanning(void)
{
	//After initial settling time
	if((scanState == SCAN_SCANNING) && (scanTimer > SCAN_SKIP_CHANNEL_INTERVAL) && (scanTimer < (SCAN_TOTAL_INTERVAL - SCAN_FREQ_CHANGE_SETTLING_INTERVAL)))
	{
		//test for presence of RF Carrier.
		// In FM mode the dmr slot_state will always be DMR_STATE_IDLE
		if (slot_state != DMR_STATE_IDLE)
		{
			if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
			{
				scanActive = false;
				// Just update the header (to prevent hidden mode)
				ucClearRows(0, 2, false);
				menuUtilityRenderHeader();
				ucRenderRows(0, 2);
				return;
			}
			else
			{
				scanState = SCAN_PAUSED;
				scanTimer = nonVolatileSettings.scanDelay * 1000;
			}
		}
		else
		{
			if(trxCarrierDetected())
			{
				if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
				{
					scanActive = false;
					// Just update the header (to prevent hidden mode)
					ucClearRows(0, 2, false);
					menuUtilityRenderHeader();
					ucRenderRows(0, 2);
					return;
				}
				else
				{
					scanTimer = SCAN_SHORT_PAUSE_TIME;//start short delay to allow full detection of signal
					scanState = SCAN_SHORT_PAUSED;//state 1 = pause and test for valid signal that produces audio
				}
			}
		}
	}

	// Only do this once if scan mode is PAUSE do it every time if scan mode is HOLD
	if(((scanState == SCAN_PAUSED) && (nonVolatileSettings.scanModePause == SCAN_MODE_HOLD)) || (scanState == SCAN_SHORT_PAUSED))
	{
	    if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
	    {
	    	scanTimer = nonVolatileSettings.scanDelay * 1000;
	    	scanState = SCAN_PAUSED;
	    }
	}

	if(scanTimer > 0)
	{
		scanTimer--;
	}
	else
	{

		trx_measure_count = 0;//needed to allow time for Rx to settle after channel change.
		uiEvent_t tmpEvent={ .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = 0, .time = 0 };

		if (scanDirection == 1)
		{
			if(currentChannelData->rxFreq + VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] <= nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber])
			{
				handleUpKey(&tmpEvent);
			}
			else
			{
				int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
				currentChannelData->rxFreq = nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
				currentChannelData->txFreq = currentChannelData->rxFreq + offset;
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
			}
		}
		else
		{
			if(currentChannelData->rxFreq + VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] >= nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber])
			{
				handleUpKey(&tmpEvent);
			}
			else
			{
				int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
				currentChannelData->rxFreq = nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber];
				currentChannelData->txFreq = currentChannelData->rxFreq+offset;
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
			}
		}

		//allow extra time if scanning a simplex DMR channel.
		if ((trxGetMode() == RADIO_MODE_DIGITAL) && (trxDMRMode == DMR_MODE_ACTIVE) && (SCAN_TOTAL_INTERVAL < SCAN_DMR_SIMPLEX_MIN_INTERVAL))
		{
			scanTimer = SCAN_DMR_SIMPLEX_MIN_INTERVAL;
		}
		else
		{
			scanTimer = SCAN_TOTAL_INTERVAL;
		}

		scanState = SCAN_SCANNING;//state 0 = settling and test for carrier present.

		//check all nuisance delete entries and skip channel if there is a match
		for(int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
		{
			if (nuisanceDelete[i] == -1)
			{
				break;
			}
			else
			{
				if(nuisanceDelete[i] == currentChannelData->rxFreq)
				{
					scanTimer = SCAN_SKIP_CHANNEL_INTERVAL;
					break;
				}
			}
		}
	}
}
