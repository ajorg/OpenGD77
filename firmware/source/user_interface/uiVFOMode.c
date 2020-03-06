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
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>

enum VFO_SELECTED_FREQUENCY_INPUT  {VFO_SELECTED_FREQUENCY_INPUT_RX , VFO_SELECTED_FREQUENCY_INPUT_TX};
typedef enum vfoScreenOperationMode {VFO_SCREEN_OPERATION_NORMAL , VFO_SCREEN_OPERATION_SCAN} vfoScreenOperationMode_t;

static int selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

static struct_codeplugRxGroup_t rxGroupData;
static struct_codeplugContact_t contactData;

// internal prototypes
static void handleEvent(uiEvent_t *ev);
static void handleQuickMenuEvent(uiEvent_t *ev);
static void updateQuickMenuScreen(void);
static void update_frequency(int tmp_frequency);
static void stepFrequency(int increment);
static void loadContact(void);
static void toneScan(void);
static void scanning(void);
static void initScan(void);
static void menuVFOUpdateTrxID(void );
static void setCurrentFreqToScanLimits(void);
static void handleUpKey(uiEvent_t *ev);

static bool isDisplayingQSOData=false;
static int tmpQuickMenuDmrFilterLevel;
static int tmpQuickMenuAnalogFilterLevel;
static int16_t newChannelIndex = 0;
static bool toneScanActive = false;//tone scan active flag  (CTCSS)
static const int TONESCANINTERVAL=200;//time between each tone for lowest tone. (higher tones take less time.)
static int scanIndex=0;
static bool displayChannelSettings;
static int prevDisplayQSODataState;
static vfoScreenOperationMode_t screenOperationMode[2] = {VFO_SCREEN_OPERATION_NORMAL,VFO_SCREEN_OPERATION_NORMAL};// For VFO A and B

// Public interface
int menuVFOMode(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0, sqm = 0;

	if (isFirstRun)
	{
		LinkItem_t *item = NULL;
		uint32_t rxID = HRC6000GetReceivedSrcId();
		freq_enter_idx = 0;

		isDisplayingQSOData=false;
		nonVolatileSettings.initialMenuNumber=MENU_VFO_MODE;
		prevDisplayQSODataState = QSO_DISPLAY_IDLE;
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];

		settingsCurrentChannelNumber = -1;// This is not a regular channel. Its the special VFO channel!
		displayChannelSettings = false;

		trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);

		//Need to load the Rx group if specified even if TG is currently overridden as we may need it later when the left or right button is pressed
		if (currentChannelData->rxGroupList != 0)
		{
			codeplugRxGroupGetDataForIndex(currentChannelData->rxGroupList,&rxGroupData);
		}

		if (currentChannelData->chMode == RADIO_MODE_ANALOG)
		{
			trxSetModeAndBandwidth(currentChannelData->chMode, ((currentChannelData->flag4 & 0x02) == 0x02));
			trxSetTxCTCSS(currentChannelData->txTone);
			if (!toneScanActive)
			{
				if (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE)
				{
					trxSetRxCTCSS(TRX_CTCSS_TONE_NONE);
				}
				else
				{
					trxSetRxCTCSS(currentChannelData->rxTone);
				}
			}

			if (scanActive==false)
			{
				scanState = SCAN_SCANNING;
			}
		}
		else
		{
			trxSetDMRColourCode(currentChannelData->rxColor);
			trxSetModeAndBandwidth(currentChannelData->chMode, false);

			if (nonVolatileSettings.overrideTG == 0)
			{
				if (currentChannelData->rxGroupList != 0)
				{
					loadContact();

					// Check whether the contact data seems valid
					if (contactData.name[0] == 0 || contactData.tgNumber ==0 || contactData.tgNumber > 9999999)
					{
						nonVolatileSettings.overrideTG = 9;// If the VFO does not have an Rx Group list assigned to it. We can't get a TG from the codeplug. So use TG 9.
						trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
						trxSetDMRTimeSlot(((currentChannelData->flag2 & 0x40)!=0));
					}
					else
					{
						trxTalkGroupOrPcId = contactData.tgNumber;
						trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);
					}
				}
				else
				{
					nonVolatileSettings.overrideTG = 9;// If the VFO does not have an Rx Group list assigned to it. We can't get a TG from the codeplug. So use TG 9.
				}

			}
			else
			{
				trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
			}

			if ((nonVolatileSettings.tsManualOverride & 0xF0) != 0)
			{
				trxSetDMRTimeSlot(((nonVolatileSettings.tsManualOverride & 0xF0)>>4) - 1);
			}
		}

		// We're in digital mode, RXing, and current talker is already at the top of last heard list,
		// hence immediately display complete contact/TG info on screen
		// This mostly happens when getting out of a menu.
		if ((trxIsTransmitting == false) && ((trxGetMode() == RADIO_MODE_DIGITAL) && (rxID != 0) && (HRC6000GetReceivedTgOrPcId() != 0)) &&
				//(GPIO_PinRead(GPIO_audio_amp_enable, Pin_audio_amp_enable) == 1)
				(getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
				&& checkTalkGroupFilter() &&
				(((item = lastheardFindInList(rxID)) != NULL) && (item == LinkHead)))
		{
			menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
		}
		else
		{
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		}

		lastHeardClearLastID();
		reset_freq_enter_digits();
		menuVFOModeUpdateScreen(0);
	}
	else
	{
		if (ev->events == NO_EVENT)
		{
			// is there an incoming DMR signal
			if (menuDisplayQSODataState != QSO_DISPLAY_IDLE)
			{
				menuVFOModeUpdateScreen(0);
			}
			else
			{
				// Clear squelch region
				if (displaySquelch && ((ev->time - sqm) > 1000))
				{
					displaySquelch = false;

					ucClearRows(2, 4, false);
					ucRenderRows(2,4);
				}

				if ((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
				{
					m = ev->time;

					if (scanActive && (scanState == SCAN_PAUSED))
					{
						ucClearRows(0, 2, false);
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

			if(toneScanActive==true)
			{
				toneScan();
			}

			if (scanActive==true)
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
				if (toneScanActive)
				{
					if (((ev->events & BUTTON_EVENT) && (ev->buttons == BUTTON_NONE)) ||
							((ev->keys.key != 0) && (ev->keys.event & KEY_MOD_UP)))
					{
						menuVFOModeStopScanning();
					}

					return 0;
				}

				handleEvent(ev);
			}

		}
	}
	return 0;
}

void menuVFOModeUpdateScreen(int txTimeSecs)
{
	static const int bufferLen = 17;
	char buffer[bufferLen];
	struct_codeplugContact_t contact;
	int contactIndex;

	// Only render the header, then wait for the next run
	// Otherwise the screen could remain blank if TG and PC are == 0
	// since menuDisplayQSODataState won't be set to QSO_DISPLAY_IDLE
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (HRC6000GetReceivedTgOrPcId() == 0) &&
			((menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA) || (menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		ucClearRows(0,  2, false);
		menuUtilityRenderHeader();
		ucRenderRows(0,  2);
		return;
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
					if((trxTalkGroupOrPcId >> 24) == TG_CALL_FLAG)
					{
						contactIndex = codeplugContactIndexByTGorPC((trxTalkGroupOrPcId & 0x00FFFFFF), CONTACT_CALLTYPE_TG, &contact);
						if (contactIndex == 0) {
							snprintf(buffer, bufferLen, "TG %d", (trxTalkGroupOrPcId & 0x00FFFFFF));
						} else {
							codeplugUtilConvertBufToString(contact.name, buffer, 16);
						}
					}
					else
					{
						contactIndex = codeplugContactIndexByTGorPC((trxTalkGroupOrPcId & 0x00FFFFFF), CONTACT_CALLTYPE_PC, &contact);
						if (contactIndex == 0) {
							dmrIdDataStruct_t currentRec;
							dmrIDLookup((trxTalkGroupOrPcId & 0x00FFFFFF),&currentRec);
							strncpy(buffer, currentRec.text, bufferLen);
						} else {
							codeplugUtilConvertBufToString(contact.name, buffer, 16);
						}
					}

					if (trxIsTransmitting)
					{
						ucDrawRect(0, 34, 128, 16, true);
					}
					else
					{
						ucDrawRect(0, CONTACT_Y_POS, 128, 16, true);
					}
				}
				else
				{
					codeplugUtilConvertBufToString(contactData.name, buffer, 16);
				}

				buffer[bufferLen - 1] = 0;

				if (trxIsTransmitting)
				{
					ucPrintCentered(34, buffer, FONT_8x16);
				}
				else
				{
					ucPrintCentered(CONTACT_Y_POS, buffer, FONT_8x16);
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
					ucPrintAt(0 + ((strlen(buffer) * 8) < xbar - 2 ? (((xbar - 2) - (strlen(buffer) * 8)) >> 1) : 0), 16, buffer, FONT_8x16);
					int bargraph = 1 + ((currentChannelData->sql - 1) * 5) /2;
					ucDrawRect(xbar - 2, 17, 55, 13, true);
					ucFillRect(xbar, 19, bargraph, 9, false);
				}

				// SK1 is pressed, we don't want to clear the first info row after 1s
				if (displayChannelSettings && displaySquelch)
				{
					displaySquelch = false;
				}

				if(toneScanActive == true)
				{
					sprintf(buffer,"CTCSS %d.%dHz",TRX_CTCSSTones[scanIndex]/10,TRX_CTCSSTones[scanIndex] % 10);
					ucPrintCentered(16,buffer, FONT_8x16);
				}

			}

			if (freq_enter_idx==0)
			{
				if (!trxIsTransmitting)
				{
					// if CC scan is active, Rx freq is moved down to the Tx location,
					// as Contact Info will be displayed here
					printFrequency(false, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_RX), 32, currentChannelData->rxFreq, true, screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN);
				}
				else
				{
					// Squelch is displayed, PTT was pressed
					// Clear its region
					if (displaySquelch)
					{
						displaySquelch = false;
						ucClearRows(2, 4, false);
					}

					snprintf(buffer, bufferLen, " %d ", txTimeSecs);
					ucPrintCentered(TX_TIMER_Y_OFFSET, buffer, FONT_16x32);
				}

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					printFrequency(true, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX || trxIsTransmitting), 48, currentChannelData->txFreq, true, false);
				}
				else
				{
					snprintf(buffer, bufferLen, "%d.%03d  %d.%03d", 	nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100,
																	nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100);
					buffer[bufferLen - 1] = 0;
					ucPrintAt(0, 48, buffer, FONT_8x16);
				}
			}
			else
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					snprintf(buffer, bufferLen, "%c%c%c.%c%c%c%c%c MHz", freq_enter_digits[0], freq_enter_digits[1], freq_enter_digits[2],
							freq_enter_digits[3], freq_enter_digits[4], freq_enter_digits[5], freq_enter_digits[6], freq_enter_digits[7]);

					if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
					{
						ucPrintCentered(48, buffer, FONT_8x16);
					}
					else
					{
						ucPrintCentered(32, buffer, FONT_8x16);
					}
				}
				else
				{
					snprintf(buffer, bufferLen, "%c%c%c.%c%c%c  %c%c%c.%c%c%c",
							freq_enter_digits[0], freq_enter_digits[1], freq_enter_digits[2], freq_enter_digits[3], freq_enter_digits[4], freq_enter_digits[5],
							freq_enter_digits[6] , freq_enter_digits[7], freq_enter_digits[8], freq_enter_digits[9], freq_enter_digits[10], freq_enter_digits[11]);
					ucPrintCentered(48, buffer, FONT_8x16);
				}
			}

			ucRender();
			break;

		case QSO_DISPLAY_CALLER_DATA:
			displayLightTrigger();
		case QSO_DISPLAY_CALLER_DATA_UPDATE:
			prevDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
			isDisplayingQSOData=true;
			displayChannelSettings = false;
			menuUtilityRenderQSOData();
			ucRender();
			break;
	}

	menuDisplayQSODataState = QSO_DISPLAY_IDLE;
}

void menuVFOModeStopScanning(void)
{
	toneScanActive = false;
	scanActive=false;
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	menuVFOModeUpdateScreen(0); // Needs to redraw the screen now
	displayLightTrigger();
}

static void update_frequency(int frequency)
{
	if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
	{
		if (trxGetBandFromFrequency(frequency)!=-1)
		{
			currentChannelData->txFreq = frequency;
			trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
			set_melody(melody_ACK_beep);
		}
	}
	else
	{
		int deltaFrequency = frequency - currentChannelData->rxFreq;
		if (trxGetBandFromFrequency(frequency)!=-1)
		{
			currentChannelData->rxFreq = frequency;
			currentChannelData->txFreq = currentChannelData->txFreq + deltaFrequency;
			trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);

			if (trxGetBandFromFrequency(currentChannelData->txFreq)!=-1)
			{
				set_melody(melody_ACK_beep);
			}
			else
			{
				currentChannelData->txFreq = frequency;
				set_melody(melody_ERROR_beep);

			}
		}
		else
		{
			set_melody(melody_ERROR_beep);
		}
	}
	menuClearPrivateCall();
}

static void checkAndFixIndexInRxGroup(void)
{
	if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]
			> (rxGroupData.NOT_IN_MEMORY_numTGsInGroup - 1))
	{
		nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] = 0;
	}
}

static void loadContact(void)
{
	// Check if this channel has an Rx Group
	if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
	{
		codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]],&contactData);
	}
	else
	{
		codeplugContactGetDataForIndex(currentChannelData->contact,&contactData);
	}
}

static void handleEvent(uiEvent_t *ev)
{
	displayLightTrigger();

	if (scanActive && (ev->events & KEY_EVENT))
	{
		if (!(ev->buttons & BUTTON_SK2))
		{
			// Right key sets the current frequency as a 'niusance' frequency.
			if(scanState==SCAN_PAUSED &&  ev->keys.key == KEY_RIGHT)
			{
				nuisanceDelete[nuisanceDeleteIndex++]=currentChannelData->rxFreq;
				if(nuisanceDeleteIndex > (MAX_ZONE_SCAN_NUISANCE_CHANNELS - 1))
				{
					nuisanceDeleteIndex = 0; //rolling list of last MAX_NUISANCE_CHANNELS deletes.
				}
				scanTimer = SCAN_SKIP_CHANNEL_INTERVAL;//force scan to continue;
				scanState=SCAN_SCANNING;
				fw_reset_keyboard();
				return;
			}

			// Left key reverses the scan direction
			if (scanState == SCAN_SCANNING && ev->keys.key == KEY_LEFT)
			{
				scanDirection *= -1;
				fw_reset_keyboard();
				return;
			}
		}

		// Stop the scan on any key except UP without Shift (allows scan to be manually continued)
		// or SK2 on its own (allows Backlight to be triggered)
		if (ev->keys.key != KEY_UP )
		{
			menuVFOModeStopScanning();
			fw_reset_keyboard();
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
		// Stop the scan if any button is pressed.
		if (scanActive)
		{
			menuVFOModeStopScanning();
			return;
		}

		uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);

		// If Blue button is pressed during reception it sets the Tx TG to the incoming TG
		if (isDisplayingQSOData && (ev->buttons & BUTTON_SK2) && trxGetMode() == RADIO_MODE_DIGITAL &&
					(trxTalkGroupOrPcId != tg ||
					(dmrMonitorCapturedTS!=-1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot()) ||
					(trxGetDMRColourCode() != currentChannelData->rxColor)))
		{
			lastHeardClearLastID();

			// Set TS to overriden TS
			if (dmrMonitorCapturedTS!=-1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot())
			{
				trxSetDMRTimeSlot(dmrMonitorCapturedTS);
				nonVolatileSettings.tsManualOverride &= 0xF0;// Clear lower nibble value
				nonVolatileSettings.tsManualOverride |= (dmrMonitorCapturedTS+1);// Store manual TS override
			}
			if (trxTalkGroupOrPcId != tg)
			{
				if ((tg>>24) & PC_CALL_FLAG)
				{
					menuAcceptPrivateCall(tg & 0xffffff);
				}
				else
				{
					trxTalkGroupOrPcId = tg;
					nonVolatileSettings.overrideTG = trxTalkGroupOrPcId;
				}
			}

			currentChannelData->rxColor = trxGetDMRColourCode();// Set the CC to the current CC, which may have been determined by the CC finding algorithm in C6000.c

			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			menuVFOModeUpdateScreen(0);
			return;
		}

		// Display channel settings (CTCSS, Squelch) while SK1 is pressed
		if ((displayChannelSettings == false) && (ev->buttons & BUTTON_SK1))
		{
			int prevQSODisp = prevDisplayQSODataState;

			displayChannelSettings = true;
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			menuVFOModeUpdateScreen(0);
			prevDisplayQSODataState = prevQSODisp;
			return;
		}
		else if ((displayChannelSettings == true) && (ev->buttons & BUTTON_SK1)==0)
		{
			displayChannelSettings = false;
			menuDisplayQSODataState = prevDisplayQSODataState;
			menuVFOModeUpdateScreen(0);
			return;
		}
		if (ev->buttons & BUTTON_ORANGE)
		{
			if (ev->buttons & BUTTON_SK2)
			{
				settingsPrivateCallMuteMode = !settingsPrivateCallMuteMode;// Toggle PC mute only mode
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuVFOModeUpdateScreen(0);
			}
			else
			{
				menuSystemPushNewMenu(MENU_VFO_QUICK_MENU);
			}

			return;
		}
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_SHORTUP(ev->keys,KEY_GREEN))
		{
			if (ev->buttons & BUTTON_SK2 )
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


		if (freq_enter_idx==0)
		{
			if (KEYCHECK_SHORTUP(ev->keys,KEY_HASH))
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if ((ev->buttons & BUTTON_SK2) != 0)
					{
						menuSystemPushNewMenu(MENU_CONTACT_QUICKLIST);
					} else {
						menuSystemPushNewMenu(MENU_NUMERICAL_ENTRY);
					}
				}
				return;
			}

			if (KEYCHECK_SHORTUP(ev->keys,KEY_STAR))
			{
				if (ev->buttons & BUTTON_SK2 )
				{
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						currentChannelData->chMode = RADIO_MODE_DIGITAL;
						trxSetModeAndBandwidth(currentChannelData->chMode, false);
						checkAndFixIndexInRxGroup();
						// Check if the contact data for the VFO has previous been loaded
						if (contactData.name[0] == 0x00)
						{
							loadContact();
						}
					}
					else
					{
						currentChannelData->chMode = RADIO_MODE_ANALOG;
						trxSetModeAndBandwidth(currentChannelData->chMode, ((currentChannelData->flag4 & 0x02) == 0x02));
						trxSetTxCTCSS(currentChannelData->rxTone);
					}
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// Toggle TimeSlot
						trxSetDMRTimeSlot(1-trxGetDMRTimeSlot());
						nonVolatileSettings.tsManualOverride &= 0x0F;// Clear upper nibble value
						nonVolatileSettings.tsManualOverride |= (trxGetDMRTimeSlot()+1)<<4;// Store manual TS override for VFO in upper nibble

						disableAudioAmp(AUDIO_AMP_MODE_RF);
						clearActiveDMRID();
						lastHeardClearLastID();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
					}
					else
					{
						set_melody(melody_ERROR_beep);
					}
				}
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_STAR))
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					nonVolatileSettings.tsManualOverride &= 0x0F; // remove TS override for VFO
					// Check if this channel has an Rx Group
					if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
					{
						codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]],&contactData);
					}
					else
					{
						codeplugContactGetDataForIndex(currentChannelData->contact,&contactData);
					}

					trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);

					clearActiveDMRID();
					lastHeardClearLastID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
					menuVFOModeUpdateScreen(0);
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys,KEY_DOWN))
			{
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				if (ev->buttons & BUTTON_SK2 )
				{
					selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_TX;
				}
				else
				{
					stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] * -1);
					menuVFOModeUpdateScreen(0);
				}
			}
			else if (KEYCHECK_LONGDOWN(ev->keys,KEY_DOWN))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{

					screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_NORMAL;
					menuVFOModeStopScanning();
					return;
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys,KEY_UP))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{

					setCurrentFreqToScanLimits();
					if (!scanActive)
					{
						scanActive=true;
					}
					else
					{
						handleUpKey(ev);
					}
				}
				else
				{
					handleUpKey(ev);
				}
			}
			else if (KEYCHECK_LONGDOWN(ev->keys,KEY_UP))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN)
				{
					initScan();
					return;
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
			{
				if ((ev->buttons & BUTTON_SK2 )!=0 && menuUtilityTgBeforePcMode != 0)
				{
					nonVolatileSettings.overrideTG = menuUtilityTgBeforePcMode;
					menuVFOUpdateTrxID();
					menuDisplayQSODataState= QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
					menuClearPrivateCall();
					menuVFOModeUpdateScreen(0);
					return;// The event has been handled
				}

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)
				menuSystemSetCurrentMenu(MENU_CHANNEL_MODE);
#endif
				return;
			}
#if defined(PLATFORM_DM1801)
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR))
			{
				menuSystemSetCurrentMenu(MENU_CHANNEL_MODE);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_A_B))
			{
				nonVolatileSettings.currentVFONumber = 1 - nonVolatileSettings.currentVFONumber;// Switch to other VFO
				currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuSystemPopAllAndDisplayRootMenu(); // Force to set all TX/RX settings.
				return;
			}
#endif
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RIGHT))
			{
				// Long press allows the 5W+ power setting to be selected immediately
				if (ev->buttons & BUTTON_SK2)
				{
					if (nonVolatileSettings.txPowerLevel == (MAX_POWER_SETTING_NUM - 1))
					{
						nonVolatileSettings.txPowerLevel++;
						trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
					}
				}
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
			{
				if (ev->buttons & BUTTON_SK2)
				{
					if (nonVolatileSettings.txPowerLevel < (MAX_POWER_SETTING_NUM - 1))
					{
						nonVolatileSettings.txPowerLevel++;
						trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
					}
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (nonVolatileSettings.overrideTG == 0)
						{
							nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]++;
							checkAndFixIndexInRxGroup();
						}
						nonVolatileSettings.overrideTG = 0;// setting the override TG to 0 indicates the TG is not overridden
						menuClearPrivateCall();
						menuVFOUpdateTrxID();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
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
						displaySquelch=true;
						menuVFOModeUpdateScreen(0);
					}
				}
			}
			else if (KEYCHECK_PRESS(ev->keys,KEY_LEFT))
			{
				if (ev->buttons & BUTTON_SK2)
				{
					if (nonVolatileSettings.txPowerLevel > 0)
					{
						nonVolatileSettings.txPowerLevel--;
						trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
					}
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// To Do change TG in on same channel freq
						if (nonVolatileSettings.overrideTG == 0)
						{
							nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]--;
							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < 0)
							{
								nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] =
										rxGroupData.NOT_IN_MEMORY_numTGsInGroup - 1;
							}
						}
						nonVolatileSettings.overrideTG = 0;// setting the override TG to 0 indicates the TG is not overridden
						menuClearPrivateCall();
						menuVFOUpdateTrxID();
						menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
						menuVFOModeUpdateScreen(0);
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
						displaySquelch=true;
						menuVFOModeUpdateScreen(0);
					}
				}
			}
		}
		else
		{
			if (KEYCHECK_PRESS(ev->keys,KEY_LEFT))
			{
				freq_enter_idx--;
				freq_enter_digits[freq_enter_idx]='-';
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
			else if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
			{
				reset_freq_enter_digits();
				set_melody(melody_NACK_beep);
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					int tmp_frequency=read_freq_enter_digits(0,8);
					if (trxGetBandFromFrequency(tmp_frequency)!=-1)
					{
						update_frequency(tmp_frequency);
						reset_freq_enter_digits();
					}
					else
					{
						set_melody(melody_ERROR_beep);
					}
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				else
				{
					if (freq_enter_idx!=0)
					{
						set_melody(melody_ERROR_beep);
					}
				}
			}
		}
		if (freq_enter_idx < ((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL )?8:12))
		{
			int keyval = menuGetKeypadKeyValue(ev, true);
			if (keyval != 99)
			{
				freq_enter_digits[freq_enter_idx] = (char) keyval+'0';
				freq_enter_idx++;

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL )
				{
					if (freq_enter_idx==8)
					{
						int tmp_frequency=read_freq_enter_digits(0,8);
						if (trxGetBandFromFrequency(tmp_frequency)!=-1)
						{
							update_frequency(tmp_frequency);
							reset_freq_enter_digits();
							set_melody(melody_ACK_beep);
						}
						else
						{
							set_melody(melody_ERROR_beep);
						}
					}
				}
				else
				{
					if (freq_enter_idx==12)
					{
						int fLower=read_freq_enter_digits(0,6)  * 100;
						int fUpper=read_freq_enter_digits(6,12) * 100;

						if (trxGetBandFromFrequency(fLower)!=-1 && trxGetBandFromFrequency(fUpper)!=-1 && (fLower < fUpper))
						{
							nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] = fLower;
							nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] = fUpper;

							reset_freq_enter_digits();
							set_melody(melody_ACK_beep);
							menuVFOModeUpdateScreen(0);
						}
						else
						{
							set_melody(melody_ERROR_beep);
						}
					}
				}

				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}
	}
//	menuVFOModeUpdateScreen(0);
}


static void handleUpKey(uiEvent_t *ev)
{
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	if (ev->buttons & BUTTON_SK2 )
	{
		selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;
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
		menuVFOModeUpdateScreen(0);
	}
	scanTimer=500;
	scanState = SCAN_SCANNING;
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
	if (trxGetBandFromFrequency(tmp_frequencyRx)!=-1)
	{
		currentChannelData->txFreq = tmp_frequencyTx;
		currentChannelData->rxFreq =  tmp_frequencyRx;
		if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_RX)
		{
			trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
		}
	}
	else
	{
		set_melody(melody_ERROR_beep);
	}
}

// ---------------------------------------- Quick Menu functions -------------------------------------------------------------------
enum VFO_SCREEN_QUICK_MENU_ITEMS // The last item in the list is used so that we automatically get a total number of items in the list
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)
	VFO_SCREEN_QUICK_MENU_VFO_A_B = 0,
#elif defined(PLATFORM_DM1801)
	VFO_SCREEN_QUICK_MENU_SCAN = 0,
#endif
	VFO_SCREEN_QUICK_MENU_TX_SWAP_RX, VFO_SCREEN_QUICK_MENU_BOTH_TO_RX, VFO_SCREEN_QUICK_MENU_BOTH_TO_TX,
	VFO_SCREEN_QUICK_MENU_FILTER,VFO_SCREEN_QUICK_MENU_VFO_TO_NEW,
	NUM_VFO_SCREEN_QUICK_MENU_ITEMS
};

int menuVFOModeQuickMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		tmpQuickMenuDmrFilterLevel = nonVolatileSettings.dmrFilterLevel;
		tmpQuickMenuAnalogFilterLevel = nonVolatileSettings.analogFilterLevel;
		updateQuickMenuScreen();
	}
	else
	{
		if (ev->hasEvent)
			handleQuickMenuEvent(ev);
	}
	return 0;
}

static void updateQuickMenuScreen(void)
{
	int mNum = 0;
	static const int bufferLen = 17;
	char buf[bufferLen];

	ucClearBuf();
	menuDisplayTitle(currentLanguage->quick_menu);

	for(int i = -1; i <= 1; i++)
	{
		mNum = menuGetMenuOffset(NUM_VFO_SCREEN_QUICK_MENU_ITEMS, i);

		switch(mNum)
		{
		    case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
		    	strncpy(buf, currentLanguage->vfoToNewChannel, bufferLen);
		    	break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
				strcpy(buf, "Rx --> Tx");
				break;
			case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
				strcpy(buf, "Tx <--> Rx");
				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
				strcpy(buf, "Tx --> Rx");
				break;
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if(trxGetMode() == RADIO_MODE_ANALOG)
				{
					snprintf(buf, bufferLen, "%s:%s", currentLanguage->filter, ((tmpQuickMenuAnalogFilterLevel == 0) ? currentLanguage->none : ANALOG_FILTER_LEVELS[tmpQuickMenuAnalogFilterLevel]));
				}
				else
				{
					snprintf(buf, bufferLen, "%s:%s", currentLanguage->filter, ((tmpQuickMenuDmrFilterLevel == 0) ? currentLanguage->none : DMR_FILTER_LEVELS[tmpQuickMenuDmrFilterLevel]));
				}
				break;
#if defined(PLATFORM_GD77)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				sprintf(buf, "VFO:%c", ((nonVolatileSettings.currentVFONumber==0) ? 'A' : 'B'));
				break;
#endif
			default:
				strcpy(buf, "");
				break;
		}

		buf[bufferLen - 1] = 0;
		menuDisplayEntry(i, mNum, buf);
	}
	ucRender();
}

static void handleQuickMenuEvent(uiEvent_t *ev)
{
	displayLightTrigger();

	if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
	{
		toneScanActive=false;

		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys,KEY_GREEN))
	{
		switch(gMenusCurrentItemIndex)
		{
			case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
				//look for empty channel
				for (newChannelIndex = 1; newChannelIndex< 1024; newChannelIndex++)
				{
					if (!codeplugChannelIndexIsValid(newChannelIndex)) {
						break;
					}
				}
				if (newChannelIndex < 1024)
				{
					//set zone to all channels and channel index to free channel found
					nonVolatileSettings.currentZone = codeplugZonesGetCount() - 1;

					nonVolatileSettings.currentChannelIndexInAllZone = newChannelIndex;

					settingsCurrentChannelNumber = newChannelIndex;

					memcpy(&channelScreenChannelData.rxFreq,&settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq,sizeof(struct_codeplugChannel_t)- 16);// Don't copy the name of the vfo, which are in the first 16 bytes

					snprintf((char *) &channelScreenChannelData.name, 16, "%s %d", currentLanguage->new_channel, newChannelIndex);

					codeplugChannelSaveDataForIndex(newChannelIndex, &channelScreenChannelData);

					//Set channel index as valid
					codeplugChannelIndexSetValid(newChannelIndex);
					//nonVolatileSettings.overrideTG = 0; // remove any TG override
					nonVolatileSettings.currentChannelIndexInZone = 0;// Since we are switching zones the channel index should be reset
					channelScreenChannelData.rxFreq=0x00; // Flag to the Channel screen that the channel data is now invalid and needs to be reloaded

					//copy current channel from vfo to channel
					nonVolatileSettings.currentIndexInTRxGroupList[0] = nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber];

					//copy current TS
					nonVolatileSettings.tsManualOverride &= 0xF0;// Clear lower nibble value
					nonVolatileSettings.tsManualOverride |= (trxGetDMRTimeSlot()+1);// Store manual TS override

					menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);

					set_melody(melody_ACK_beep);
					return;
				}
				else
				{
					set_melody(melody_ERROR_beep);
				}

				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
				currentChannelData->txFreq = currentChannelData->rxFreq;
				trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
				break;
			case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
				{
					int tmpFreq = currentChannelData->txFreq;
					currentChannelData->txFreq = currentChannelData->rxFreq;
					currentChannelData->rxFreq =  tmpFreq;
					trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
				}
				break;
			case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
				currentChannelData->rxFreq = currentChannelData->txFreq;
				trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);

				break;
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					nonVolatileSettings.dmrFilterLevel = tmpQuickMenuDmrFilterLevel;
					init_digital_DMR_RX();
					disableAudioAmp(AUDIO_AMP_MODE_RF);
				}
				else
				{
					nonVolatileSettings.analogFilterLevel = tmpQuickMenuAnalogFilterLevel;
				}
				break;
		}
		menuSystemPopPreviousMenu();
		return;
	}
#if defined(PLATFORM_GD77)
	else if (((ev->events & BUTTON_EVENT) && (ev->buttons & BUTTON_ORANGE)) && (gMenusCurrentItemIndex==VFO_SCREEN_QUICK_MENU_VFO_A_B))
	{
		nonVolatileSettings.currentVFONumber = 1 - nonVolatileSettings.currentVFONumber;// Switch to other VFO
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
		menuSystemPopPreviousMenu();
		return;
	}
#endif
	else if (KEYCHECK_PRESS(ev->keys,KEY_RIGHT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (tmpQuickMenuDmrFilterLevel < NUM_DMR_FILTER_LEVELS - 1)
					{
						tmpQuickMenuDmrFilterLevel++;
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
#if defined(PLATFORM_GD77)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				if (nonVolatileSettings.currentVFONumber==0)
				{
					nonVolatileSettings.currentVFONumber++;
					currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				}
				break;
#endif
			}
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_LEFT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					// Require long-press to change the DMR Filter to None
					//
					//  This feature is not intended for normal operation. Its only intended
					//  as a way to listen to stations using an unknown CC.
					if (tmpQuickMenuDmrFilterLevel == DMR_FILTER_NONE + 1)
					{
						tmpQuickMenuDmrFilterLevel--;
					}
				}
		}
	}
	else if (KEYCHECK_PRESS(ev->keys,KEY_LEFT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case VFO_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					// Note that long-press is required to change DMR filter to None
					// See KEYCHECK_LONGDOWN section above.
					if (tmpQuickMenuDmrFilterLevel > DMR_FILTER_NONE + 1)
					{
						tmpQuickMenuDmrFilterLevel--;
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
#if defined(PLATFORM_GD77)
			case VFO_SCREEN_QUICK_MENU_VFO_A_B:
				if (nonVolatileSettings.currentVFONumber==1)
				{
					nonVolatileSettings.currentVFONumber--;
					currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
				}
				break;
#endif
		}
	}
	else if (KEYCHECK_PRESS(ev->keys,KEY_DOWN))
	{
		MENU_INC(gMenusCurrentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
	}
	else if (KEYCHECK_PRESS(ev->keys,KEY_UP))
	{
		MENU_DEC(gMenusCurrentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
	}

	updateQuickMenuScreen();
}

bool menuVFOModeIsScanning(void)
{
	return (toneScanActive || scanActive );
}

static void toneScan(void)
{
	if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
	{
		currentChannelData->txTone=TRX_CTCSSTones[scanIndex];
		currentChannelData->rxTone=TRX_CTCSSTones[scanIndex];
		trxSetTxCTCSS(currentChannelData->txTone);
		menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		menuVFOModeUpdateScreen(0);
		toneScanActive=false;
		return;
	}

	if(scanTimer>0)
	{
		scanTimer--;
	}
	else
	{
		scanIndex++;
		if(scanIndex > (TRX_NUM_CTCSS-1))
		{
			scanIndex=1;
		}
		trxAT1846RxOff();
		trxSetRxCTCSS(TRX_CTCSSTones[scanIndex]);
		scanTimer=TONESCANINTERVAL-(scanIndex*2);
		trx_measure_count=0;//synchronise the measurement with the scan.
		trxAT1846RxOn();
		menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		menuVFOModeUpdateScreen(0);
	}
}

static void menuVFOUpdateTrxID(void )
{
	if (nonVolatileSettings.overrideTG != 0)
	{
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}
	else
	{
		nonVolatileSettings.tsManualOverride &= 0x0F; // remove TS override for VFO

		// Check if this channel has an Rx Group
		if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
		{
			codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]],&contactData);
		}
		else
		{
			codeplugContactGetDataForIndex(currentChannelData->contact,&contactData);
		}

		trxTalkGroupOrPcId = contactData.tgNumber;

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);
	}
	lastHeardClearLastID();
	menuClearPrivateCall();
}

static void setCurrentFreqToScanLimits(void)
{
	if((currentChannelData->rxFreq < nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]) || (currentChannelData->rxFreq > nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber]))    //if we are not already inside the Low and High Limit freqs then move to the low limit.
	{
		int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
		currentChannelData->rxFreq=nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
		currentChannelData->txFreq=currentChannelData->rxFreq+offset;
		trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
	}
}

static void initScan(void)
{

	screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_SCAN;
	scanDirection = 1;

	// If scan limits have not been defined. Set them to the current Rx freq -1MHz to +1MHz
	if (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] == 0 || nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] == 0)
	{
		nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]=currentChannelData->rxFreq - 100000;
		nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber]=currentChannelData->rxFreq + 100000;
	}

	//clear all nuisance delete channels at start of scanning
	for(int i= 0;i<MAX_ZONE_SCAN_NUISANCE_CHANNELS;i++)
	{
		nuisanceDelete[i]=-1;
	}
	nuisanceDeleteIndex=0;

	selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

	scanTimer=500;
	scanState = SCAN_SCANNING;
	menuSystemPopAllAndDisplaySpecificRootMenu(MENU_VFO_MODE);
}

static void scanning(void)
{
	//After initial settling time
	if((scanState == SCAN_SCANNING) && (scanTimer > SCAN_SKIP_CHANNEL_INTERVAL) && (scanTimer < ( SCAN_TOTAL_INTERVAL - SCAN_FREQ_CHANGE_SETTLING_INTERVAL)))
	{
		//test for presence of RF Carrier.
		// In FM mode the dmr slot_state will always be DMR_STATE_IDLE
		if (slot_state != DMR_STATE_IDLE)
		{
			if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
			{
				scanActive = false;
				// Just update the header (to prevent hidden mode)
				ucClearRows(0,  2, false);
				menuUtilityRenderHeader();
				ucRenderRows(0,  2);
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
			if(trx_carrier_detected())
			{
				if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
				{
					scanActive = false;
					// Just update the header (to prevent hidden mode)
					ucClearRows(0,  2, false);
					menuUtilityRenderHeader();
					ucRenderRows(0,  2);
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

		trx_measure_count=0;//needed to allow time for Rx to settle after channel change.
		uiEvent_t tmpEvent={ .buttons = 0, .keys = NO_KEYCODE, .function = 0, .events = NO_EVENT, .hasEvent = 0, .time = 0 };

		if (scanDirection==1)
		{
			if(currentChannelData->rxFreq + VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]  <= nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber])
			{
				handleUpKey(&tmpEvent);
			}
			else
			{
				int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
				currentChannelData->rxFreq=nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
				currentChannelData->txFreq=currentChannelData->rxFreq+offset;
				trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
			}
		}
		else
		{
			if(currentChannelData->rxFreq + VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]  >= nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber])
			{
				handleUpKey(&tmpEvent);
			}
			else
			{
				int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
				currentChannelData->rxFreq=nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber];
				currentChannelData->txFreq=currentChannelData->rxFreq+offset;
				trxSetFrequency(currentChannelData->rxFreq,currentChannelData->txFreq,DMR_MODE_AUTO);
			}
		}

		//allow extra time if scanning a simplex DMR channel.
		if ((trxGetMode() == RADIO_MODE_DIGITAL) && (trxDMRMode == DMR_MODE_ACTIVE) && (SCAN_TOTAL_INTERVAL < SCAN_DMR_SIMPLEX_MIN_INTERVAL) )
		{
			scanTimer = SCAN_DMR_SIMPLEX_MIN_INTERVAL;
		}
		else
		{
			scanTimer = SCAN_TOTAL_INTERVAL;
		}

		scanState = SCAN_SCANNING;//state 0 = settling and test for carrier present.

		//check all nuisance delete entries and skip channel if there is a match
		for(int i=0;i<MAX_ZONE_SCAN_NUISANCE_CHANNELS;i++)
		{
			if (nuisanceDelete[i]==-1)
			{
				break;
			}
			else
			{
				if(nuisanceDelete[i]==currentChannelData->rxFreq)
				{
					scanTimer=SCAN_SKIP_CHANNEL_INTERVAL;
					break;
				}
			}
		}
	}
}
