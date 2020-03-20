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
#include <settings.h>
#include <trx.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>


static void handleEvent(uiEvent_t *ev);
static void loadChannelData(bool useChannelDataInMemory);
static void scanning(void);
static void startScan(void);
static void handleUpKey(uiEvent_t *ev);

static void updateQuickMenuScreen(void);
static void handleQuickMenuEvent(uiEvent_t *ev);
static void menuChannelUpdateTrxID(void );

static void searchNextChannel(void);
static void setNextChannel(void);

static struct_codeplugZone_t currentZone;
static struct_codeplugRxGroup_t rxGroupData;
static struct_codeplugContact_t contactData;
static char currentZoneName[17];
static int directChannelNumber=0;

int currentChannelNumber=0;
static bool isDisplayingQSOData=false;
static bool isTxRxFreqSwap=false;

static int tmpQuickMenuDmrFilterLevel;
static int tmpQuickMenuAnalogFilterLevel;
static bool displayChannelSettings;
static int prevDisplayQSODataState;

static struct_codeplugChannel_t channelNextChannelData={.rxFreq=0};
static bool nextChannelReady = false;
static int nextChannelIndex = 0;


int menuChannelMode(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0, sqm = 0;

	if (isFirstRun)
	{
		LinkItem_t *item = NULL;
		uint32_t rxID = HRC6000GetReceivedSrcId();

		nonVolatileSettings.initialMenuNumber = MENU_CHANNEL_MODE;// This menu.
		displayChannelSettings = false;
		nextChannelReady = false;

		// We're in digital mode, RXing, and current talker is already at the top of last heard list,
		// hence immediately display complete contact/TG info on screen
		// This mostly happens when getting out of a menu.
		if ((trxIsTransmitting == false) && ((trxGetMode() == RADIO_MODE_DIGITAL) && (rxID != 0) && (HRC6000GetReceivedTgOrPcId() != 0)) &&
				//(GPIO_PinRead(GPIO_audio_amp_enable, Pin_audio_amp_enable) == 1)
				(getAudioAmpStatus() & AUDIO_AMP_MODE_RF) &&
				checkTalkGroupFilter() &&
				(((item = lastheardFindInList(rxID)) != NULL) && (item == LinkHead)))
		{
			menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
		}
		else
		{
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
		}

		lastHeardClearLastID();
		prevDisplayQSODataState = QSO_DISPLAY_IDLE;
		currentChannelData = &channelScreenChannelData;// Need to set this as currentChannelData is used by functions called by loadChannelData()

		if (channelScreenChannelData.rxFreq != 0)
		{
			loadChannelData(true);
		}
		else
		{
			isTxRxFreqSwap = false;
			codeplugZoneGetDataForNumber(nonVolatileSettings.currentZone, &currentZone);
			codeplugUtilConvertBufToString(currentZone.name, currentZoneName, 16);// need to convert to zero terminated string
			loadChannelData(false);
		}

		menuChannelModeUpdateScreen(0);

		if (scanActive==false)
		{
			scanState = SCAN_SCANNING;
		}
	}
	else
	{
		if (ev->events == NO_EVENT)
		{
			// is there an incoming DMR signal
			if (menuDisplayQSODataState != QSO_DISPLAY_IDLE)
			{
				menuChannelModeUpdateScreen(0);
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

			if(scanActive==true)
			{
				scanning();
			}
		}
		else
		{
			if (ev->hasEvent)
			{
				if ((trxGetMode() == RADIO_MODE_ANALOG) &&
						(ev->events & KEY_EVENT) && ((ev->keys.key == KEY_LEFT) || (ev->keys.key == KEY_RIGHT)))
				{
					sqm = ev->time;
				}

				handleEvent(ev);
			}
		}
	}
	return 0;
}

#if 0 // rename: we have an union declared (fw_sound.c) with the same name.
uint16_t byteSwap16(uint16_t in)
{
	return ((in &0xff << 8) | (in >>8));
}
#endif

static void searchNextChannel(void) {
	//bool allZones = strcmp(currentZoneName,currentLanguage->all_channels) == 0;
	int channel = 0;
	if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
	{
		do {
			nextChannelIndex += scanDirection;
			if (scanDirection == 1)
			{
				if (nextChannelIndex>1024)
				{
					nextChannelIndex = 1;
				}
			}
			else
			{
				// Note this is inefficient check all the index down from 1024 until it gets to the first valid index from the end.
				// To improve this. Highest valid channel number would need to be found and cached when the radio boots up
				if (nextChannelIndex < 1)
				{
					nextChannelIndex = 1024;
				}
			}
		} while(!codeplugChannelIndexIsValid(nextChannelIndex));
		channel = nextChannelIndex;
		codeplugChannelGetDataForIndex(nextChannelIndex,&channelNextChannelData);
	}
	else
	{
		nextChannelIndex += scanDirection;
		if (scanDirection == 1)
		{
			if (nextChannelIndex > currentZone.NOT_IN_MEMORY_numChannelsInZone - 1)
			{
				nextChannelIndex = 0;
			}
		}
		else
		{
			if (nextChannelIndex < 0)
			{
				nextChannelIndex = currentZone.NOT_IN_MEMORY_numChannelsInZone - 1;
			}
		}
		codeplugChannelGetDataForIndex(currentZone.channels[nextChannelIndex],&channelNextChannelData);
		channel = currentZone.channels[nextChannelIndex];
	}

	if ((currentZone.NOT_IN_MEMORY_isAllChannelsZone && (channelNextChannelData.flag4 & 0x10)) ||
			(!currentZone.NOT_IN_MEMORY_isAllChannelsZone && (channelNextChannelData.flag4 & 0x20)))
	{
		return;
	}
	else
	{
		for(int i=0;i<MAX_ZONE_SCAN_NUISANCE_CHANNELS;i++)														//check all nuisance delete entries and skip channel if there is a match
		{
			if (nuisanceDelete[i] == -1)
			{
				break;
			}
			else
			{
				if(nuisanceDelete[i] == channel)
				{
					return;
				}
			}
		}
	}

	nextChannelReady = true;
}

static void setNextChannel(void)
{
	if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
	{
		nonVolatileSettings.currentChannelIndexInAllZone = nextChannelIndex;
	}
	else
	{
		nonVolatileSettings.currentChannelIndexInZone = nextChannelIndex;
	}

	lastHeardClearLastID();

	loadChannelData(false);
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	menuChannelModeUpdateScreen(0);

	nextChannelReady = false;
	scanTimer = 500;
	scanState = SCAN_SCANNING;
}

static void loadChannelData(bool useChannelDataInMemory)
{
	if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
	{
		settingsCurrentChannelNumber = nonVolatileSettings.currentChannelIndexInAllZone;
	}
	else
	{
		settingsCurrentChannelNumber = currentZone.channels[nonVolatileSettings.currentChannelIndexInZone];
	}

	if (!useChannelDataInMemory)
	{
		if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
		{
			codeplugChannelGetDataForIndex(nonVolatileSettings.currentChannelIndexInAllZone, &channelScreenChannelData);
		}
		else
		{
			codeplugChannelGetDataForIndex(currentZone.channels[nonVolatileSettings.currentChannelIndexInZone], &channelScreenChannelData);
		}
	}

	trxSetFrequency(channelScreenChannelData.rxFreq, channelScreenChannelData.txFreq, DMR_MODE_AUTO);

	if (channelScreenChannelData.chMode == RADIO_MODE_ANALOG)
	{
		trxSetModeAndBandwidth(channelScreenChannelData.chMode, ((channelScreenChannelData.flag4 & 0x02) == 0x02));
		trxSetTxCTCSS(channelScreenChannelData.txTone);
		if (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE)
		{
			trxSetRxCTCSS(TRX_CTCSS_TONE_NONE);
		}
		else
		{
			trxSetRxCTCSS(channelScreenChannelData.rxTone);
		}
	}
	else
	{
		trxSetModeAndBandwidth(channelScreenChannelData.chMode, false);// bandwidth false = 12.5Khz as DMR uses 12.5kHz
		trxSetDMRColourCode(channelScreenChannelData.rxColor);

		codeplugRxGroupGetDataForIndex(channelScreenChannelData.rxGroupList,&rxGroupData);
		// Check if this channel has an Rx Group
		if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
		{
			codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]],&contactData);
		}
		else
		{
			codeplugContactGetDataForIndex(channelScreenChannelData.contact, &contactData);
		}

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);

		if (nonVolatileSettings.overrideTG == 0)
		{
			trxTalkGroupOrPcId = contactData.tgNumber;

			if (contactData.callType == CONTACT_CALLTYPE_PC)
			{
				trxTalkGroupOrPcId |= (PC_CALL_FLAG << 24);
			}
		}
		else
		{
			trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
		}

		if ((nonVolatileSettings.tsManualOverride & 0x0F) != 0)
		{
			trxSetDMRTimeSlot ((nonVolatileSettings.tsManualOverride & 0x0F) -1);
		}
	}
}

void menuChannelModeUpdateScreen(int txTimeSecs)
{
	int channelNumber;
	static const int nameBufferLen = 23;
	char nameBuf[nameBufferLen];
	static const int bufferLen = 17;
	char buffer[bufferLen];
	int verticalPositionOffset = 0;
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
			if (trxIsTransmitting)
			{
				// Squelch is displayed, PTT was pressed
				// Clear its region
				if (displaySquelch)
				{
					displaySquelch = false;
					ucClearRows(2, 4, false);
				}

				snprintf(buffer, bufferLen, " %d ", txTimeSecs);
				buffer[bufferLen - 1] = 0;
				ucPrintCentered(TX_TIMER_Y_OFFSET, buffer, FONT_16x32);
				verticalPositionOffset=16;
			}
			else
			{
				// Display some channel settings
				if (displayChannelSettings)
				{
					printToneAndSquelch();
					printFrequency(false, false, 32, currentChannelData->rxFreq, false, false);
					printFrequency(true, false, 48, currentChannelData->txFreq, false, false);
				}
				else
				{
					// Print RX Frequency and Offset at bottom left
					snprintRxFreqAndOffset(buffer, bufferLen, channelScreenChannelData.rxFreq, channelScreenChannelData.txFreq);
					ucPrintAt(6, 57, buffer, FONT_6x8);

					if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
					{
						channelNumber=nonVolatileSettings.currentChannelIndexInAllZone;
						if (directChannelNumber>0)
						{
							snprintf(nameBuf, nameBufferLen, "%s %d", currentLanguage->gotoChannel, directChannelNumber);
						}
						else
						{
							snprintf(nameBuf, nameBufferLen, "%s Ch:%d",currentLanguage->all_channels, channelNumber);
						}
						nameBuf[nameBufferLen - 1] = 0;
						ucPrintCentered(48, nameBuf, FONT_6x8);
					}
					else
					{
						channelNumber=nonVolatileSettings.currentChannelIndexInZone+1;
						if (directChannelNumber>0)
						{
							snprintf(nameBuf, nameBufferLen, "%s %d", currentLanguage->gotoChannel, directChannelNumber);
							nameBuf[nameBufferLen - 1] = 0;
						}
						else
						{
							snprintf(nameBuf, nameBufferLen, "%s Ch:%d", currentZoneName,channelNumber);
							nameBuf[nameBufferLen - 1] = 0;
						}
						ucPrintCentered(48, (char *)nameBuf, FONT_6x8);
					}
				}
			}

			if (!displayChannelSettings)
			{
				codeplugUtilConvertBufToString(channelScreenChannelData.name, nameBuf, 16);
				ucPrintCentered(32 + verticalPositionOffset, nameBuf, FONT_8x16);
			}

			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if (nonVolatileSettings.overrideTG != 0)
				{
					if((trxTalkGroupOrPcId>>24) == TG_CALL_FLAG)
					{
						contactIndex = codeplugContactIndexByTGorPC((trxTalkGroupOrPcId & 0x00FFFFFF), CONTACT_CALLTYPE_TG, &contact);
						if (contactIndex == 0) {
							snprintf(nameBuf, bufferLen, "TG %d", (trxTalkGroupOrPcId & 0x00FFFFFF));
						} else {
							codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
						}
					}
					else
					{
						contactIndex = codeplugContactIndexByTGorPC((trxTalkGroupOrPcId & 0x00FFFFFF), CONTACT_CALLTYPE_PC, &contact);
						if (contactIndex == 0) {
							dmrIdDataStruct_t currentRec;
							dmrIDLookup((trxTalkGroupOrPcId & 0x00FFFFFF), &currentRec);
							strncpy(nameBuf, currentRec.text, bufferLen);
						} else {
							codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
						}
					}
					nameBuf[bufferLen - 1] = 0;
					ucDrawRect(0, CONTACT_Y_POS + verticalPositionOffset, 128, 16, true);
				}
				else
				{
					codeplugUtilConvertBufToString(contactData.name, nameBuf, 16);
				}
				ucPrintCentered(CONTACT_Y_POS + verticalPositionOffset, nameBuf, FONT_8x16);
			}
			// Squelch will be cleared later, 1s after last change
			else if(displaySquelch && !trxIsTransmitting && !displayChannelSettings)
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

static void handleEvent(uiEvent_t *ev)
{
	displayLightTrigger();

	if (scanActive && (ev->events & KEY_EVENT))
	{
		// Key pressed during scanning

		if (!(ev->buttons & BUTTON_SK2))
		{
			// if we are scanning and down key is pressed then enter current channel into nuisance delete array.
			if(scanState==SCAN_PAUSED &&  ev->keys.key == KEY_RIGHT)
			{
				nuisanceDelete[nuisanceDeleteIndex++] = settingsCurrentChannelNumber;
				if(nuisanceDeleteIndex > (MAX_ZONE_SCAN_NUISANCE_CHANNELS - 1))
				{
					nuisanceDeleteIndex = 0; //rolling list of last MAX_NUISANCE_CHANNELS deletes.
				}
				scanTimer = SCAN_SKIP_CHANNEL_INTERVAL;	//force scan to continue;
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
		// stop the scan on any button except UP without Shift (allows scan to be manually continued)
		// or SK2 on its own (allows Backlight to be triggered)
		if (((ev->keys.key == KEY_UP) && (ev->buttons & BUTTON_SK2) == 0) == false)
		{
			menuChannelModeStopScanning();
			fw_reset_keyboard();
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			menuChannelModeUpdateScreen(0);
			return;
		}
	}



	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == START_SCANNING)
		{
			directChannelNumber = 0;
			startScan();
			return;
		}
	}
	if (ev->events & BUTTON_EVENT)
	{
		uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);

		// If Blue button is pressed during reception it sets the Tx TG to the incoming TG
		if (isDisplayingQSOData && (ev->buttons & BUTTON_SK2) && trxGetMode() == RADIO_MODE_DIGITAL &&
				(trxTalkGroupOrPcId != tg ||
				(dmrMonitorCapturedTS!=-1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot()) ||
				(trxGetDMRColourCode() != currentChannelData->rxColor)))
		{
			lastHeardClearLastID();

			// Set TS to overriden TS
			if (dmrMonitorCapturedTS != -1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot())
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
			menuChannelModeUpdateScreen(0);
			return;
		}

		// Display channel settings (RX/TX/etc) while SK1 is pressed
		if ((displayChannelSettings == false) && (ev->buttons & BUTTON_SK1))
		{
			int prevQSODisp = prevDisplayQSODataState;

			displayChannelSettings = true;
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			menuChannelModeUpdateScreen(0);
			prevDisplayQSODataState = prevQSODisp;
			return;
		}
		else if ((displayChannelSettings==true) && (ev->buttons & BUTTON_SK1)==0)
		{
			displayChannelSettings = false;
			menuDisplayQSODataState = prevDisplayQSODataState;
			menuChannelModeUpdateScreen(0);
			return;
		}
		if (ev->buttons & BUTTON_ORANGE)
		{
			if (ev->buttons & BUTTON_SK2)
			{
				settingsPrivateCallMuteMode = !settingsPrivateCallMuteMode;// Toggle PC mute only mode
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
			else
			{
				// Quick Menu
				menuSystemPushNewMenu(MENU_CHANNEL_QUICK_MENU);
			}

			return;
		}
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_SHORTUP(ev->keys,KEY_GREEN))
		{
			if (directChannelNumber>0)
			{
				if(currentZone.NOT_IN_MEMORY_isAllChannelsZone)
				{
					if (codeplugChannelIndexIsValid(directChannelNumber))
					{
						nonVolatileSettings.currentChannelIndexInAllZone = directChannelNumber;
						loadChannelData(false);
					}
					else
					{
						set_melody(melody_ERROR_beep);
					}
				}
				else
				{
					if (directChannelNumber-1<currentZone.NOT_IN_MEMORY_numChannelsInZone)
					{
						nonVolatileSettings.currentChannelIndexInZone = directChannelNumber-1;
						loadChannelData(false);
					}
					else
					{
						set_melody(melody_ERROR_beep);
					}

				}
				directChannelNumber = 0;
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
			else if (ev->buttons & BUTTON_SK2 )
			{
				menuSystemPushNewMenu(MENU_CHANNEL_DETAILS);
			}
			else
			{
				menuSystemPushNewMenu(MENU_MAIN_MENU);
			}
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys,KEY_HASH))
		{
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if ((ev->buttons & BUTTON_SK2) != 0)
				{
					menuSystemPushNewMenu(MENU_CONTACT_QUICKLIST);
				} else {
					menuSystemPushNewMenu(MENU_NUMERICAL_ENTRY);
				}
				return;
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
		{
			if ((ev->buttons & BUTTON_SK2 ) != 0 && menuUtilityTgBeforePcMode != 0)
			{
				nonVolatileSettings.overrideTG = menuUtilityTgBeforePcMode;
				menuClearPrivateCall();

				menuChannelUpdateTrxID();
				menuDisplayQSODataState= QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
				menuChannelModeUpdateScreen(0);
				return;// The event has been handled
			}
			if(directChannelNumber > 0)
			{
				directChannelNumber = 0;
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
			else
			{
#if defined(PLATFORM_GD77)
				menuSystemSetCurrentMenu(MENU_VFO_MODE);
#endif
				return;
			}
		}
#if defined(PLATFORM_DM1801)
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR))
		{
			directChannelNumber = 0;
			menuSystemSetCurrentMenu(MENU_VFO_MODE);
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
					menuChannelModeUpdateScreen(0);
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
					menuChannelModeUpdateScreen(0);
				}
			}
			else
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (nonVolatileSettings.overrideTG == 0)
					{
						nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]++;
						if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]
								> (rxGroupData.NOT_IN_MEMORY_numTGsInGroup - 1))
						{
							nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] = 0;
						}
					}
					nonVolatileSettings.overrideTG = 0;// setting the override TG to 0 indicates the TG is not overridden
					menuClearPrivateCall();
					menuChannelUpdateTrxID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
					menuChannelModeUpdateScreen(0);
				}
				else
				{
					if(currentChannelData->sql == 0)			//If we were using default squelch level
					{
						currentChannelData->sql=nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
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
					menuChannelModeUpdateScreen(0);
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
					menuChannelModeUpdateScreen(0);
				}
			}
			else
				{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					// To Do change TG in on same channel freq
					if (nonVolatileSettings.overrideTG == 0)
					{
						nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]--;
						if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < 0)
						{
							nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] =
									rxGroupData.NOT_IN_MEMORY_numTGsInGroup - 1;
						}
					}
					nonVolatileSettings.overrideTG = 0;// setting the override TG to 0 indicates the TG is not overridden
					menuClearPrivateCall();
					menuChannelUpdateTrxID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
					menuChannelModeUpdateScreen(0);
				}
				else
				{
					if(currentChannelData->sql == 0)			//If we were using default squelch level
					{
						currentChannelData->sql=nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
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
					menuChannelModeUpdateScreen(0);
				}

			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			// Toggle TimeSlot
			if (ev->buttons & BUTTON_SK2 )
			{
				if (trxGetMode() == RADIO_MODE_ANALOG)
				{
					channelScreenChannelData.chMode = RADIO_MODE_DIGITAL;
					trxSetModeAndBandwidth(channelScreenChannelData.chMode, false);
				}
				else
				{
					channelScreenChannelData.chMode = RADIO_MODE_ANALOG;
					trxSetModeAndBandwidth(channelScreenChannelData.chMode, ((channelScreenChannelData.flag4 & 0x02) == 0x02));
					trxSetTxCTCSS(currentChannelData->rxTone);
				}
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
			else
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					// Toggle timeslot
					trxSetDMRTimeSlot(1-trxGetDMRTimeSlot());
					nonVolatileSettings.tsManualOverride &= 0xF0;// Clear lower nibble value
					nonVolatileSettings.tsManualOverride |= (trxGetDMRTimeSlot()+1);// Store manual TS override

					//	init_digital();
					disableAudioAmp(AUDIO_AMP_MODE_RF);
					clearActiveDMRID();
					lastHeardClearLastID();
					menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
					menuChannelModeUpdateScreen(0);
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
				nonVolatileSettings.tsManualOverride &= 0xF0; // remove TS override from channel
				if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
				{
					codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]],&contactData);
				}
				else
				{
					codeplugContactGetDataForIndex(channelScreenChannelData.contact,&contactData);
				}

				trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);

				clearActiveDMRID();
				lastHeardClearLastID();
				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			if (ev->buttons & BUTTON_SK2)
			{
				int numZones = codeplugZonesGetCount();

				if (nonVolatileSettings.currentZone == 0) {
					nonVolatileSettings.currentZone = numZones-1;
				}
				else
				{
					nonVolatileSettings.currentZone--;
				}
				nonVolatileSettings.overrideTG = 0; // remove any TG override
				nonVolatileSettings.tsManualOverride &= 0xF0; // remove TS override from channel
				nonVolatileSettings.currentChannelIndexInZone = 0;// Since we are switching zones the channel index should be reset
				channelScreenChannelData.rxFreq=0x00; // Flag to the Channel screeen that the channel data is now invalid and needs to be reloaded
				menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);
			}
			else
			{
				lastHeardClearLastID();
				if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
				{
					do
					{
						nonVolatileSettings.currentChannelIndexInAllZone--;
						if (nonVolatileSettings.currentChannelIndexInAllZone<1)
						{
							nonVolatileSettings.currentChannelIndexInAllZone = 1024;
						}
					} while(!codeplugChannelIndexIsValid(nonVolatileSettings.currentChannelIndexInAllZone));
				}
				else
				{
					nonVolatileSettings.currentChannelIndexInZone--;
					if (nonVolatileSettings.currentChannelIndexInZone < 0)
					{
						nonVolatileSettings.currentChannelIndexInZone = currentZone.NOT_IN_MEMORY_numChannelsInZone - 1;
					}
				}
			}
			loadChannelData(false);
			menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
			menuChannelModeUpdateScreen(0);
		}
		else if (KEYCHECK_PRESS(ev->keys,KEY_UP))
		{
			handleUpKey(ev);
		}
		else
		{
			int keyval = menuGetKeypadKeyValue(ev, true);

			if (keyval < 10)
			{
				directChannelNumber = (directChannelNumber*10) + keyval;
				if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
				{
					if(directChannelNumber>1024)
					{
						directChannelNumber = 0;
						set_melody(melody_ERROR_beep);
					}
				}
				else
				{
					if(directChannelNumber>currentZone.NOT_IN_MEMORY_numChannelsInZone)
						{
							directChannelNumber=0;
							set_melody(melody_ERROR_beep);
						}

				}

				menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
				menuChannelModeUpdateScreen(0);
			}
		}
	}
}

static void handleUpKey(uiEvent_t *ev)
{
	if (ev->buttons & BUTTON_SK2)
	{
		int numZones = codeplugZonesGetCount();

		nonVolatileSettings.currentZone++;
		if (nonVolatileSettings.currentZone >= numZones) {
			nonVolatileSettings.currentZone = 0;
		}
		nonVolatileSettings.overrideTG = 0; // remove any TG override
		nonVolatileSettings.tsManualOverride &= 0xF0; // remove TS override from channel
		nonVolatileSettings.currentChannelIndexInZone = 0;// Since we are switching zones the channel index should be reset
		channelScreenChannelData.rxFreq=0x00; // Flag to the Channel screen that the channel data is now invalid and needs to be reloaded
		menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);
	}
	else
	{
		lastHeardClearLastID();
		if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
		{
			do
			{
				nonVolatileSettings.currentChannelIndexInAllZone++;
				if (nonVolatileSettings.currentChannelIndexInAllZone > 1024)
				{
					nonVolatileSettings.currentChannelIndexInAllZone = 1;
				}
			} while(!codeplugChannelIndexIsValid(nonVolatileSettings.currentChannelIndexInAllZone));
		}
		else
		{
			nonVolatileSettings.currentChannelIndexInZone++;
			if (nonVolatileSettings.currentChannelIndexInZone > currentZone.NOT_IN_MEMORY_numChannelsInZone - 1)
			{
					nonVolatileSettings.currentChannelIndexInZone = 0;

			}
		}
		scanTimer=500;
		scanState = SCAN_SCANNING;
	}

	loadChannelData(false);
	menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
	menuChannelModeUpdateScreen(0);
}

// Quick Menu functions

enum CHANNEL_SCREEN_QUICK_MENU_ITEMS { CH_SCREEN_QUICK_MENU_SCAN=0, CH_SCREEN_QUICK_MENU_COPY2VFO, CH_SCREEN_QUICK_MENU_COPY_FROM_VFO,
	CH_SCREEN_QUICK_MENU_FILTER,
	NUM_CH_SCREEN_QUICK_MENU_ITEMS };// The last item in the list is used so that we automatically get a total number of items in the list

static void updateQuickMenuScreen(void)
{
	int mNum = 0;
	static const int bufferLen = 17;
	char buf[bufferLen];

	ucClearBuf();
	menuDisplayTitle(currentLanguage->quick_menu);

	for(int i =- 1; i <= 1; i++)
	{
		mNum = menuGetMenuOffset(NUM_CH_SCREEN_QUICK_MENU_ITEMS, i);
		buf[0] = 0;

		switch(mNum)
		{
			case CH_SCREEN_QUICK_MENU_SCAN:
				strncpy(buf, currentLanguage->scan, bufferLen);
				break;
			case CH_SCREEN_QUICK_MENU_COPY2VFO:
				strncpy(buf, currentLanguage->channelToVfo, bufferLen);
				break;
			case CH_SCREEN_QUICK_MENU_COPY_FROM_VFO:
				strncpy(buf, currentLanguage->vfoToChannel, bufferLen);
				break;
			case CH_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					snprintf(buf, bufferLen, "%s:%s", currentLanguage->filter, (tmpQuickMenuDmrFilterLevel == 0) ? currentLanguage->none : DMR_FILTER_LEVELS[tmpQuickMenuDmrFilterLevel]);
				}
				else
				{
					snprintf(buf, bufferLen, "%s:%s", currentLanguage->filter, (tmpQuickMenuAnalogFilterLevel == 0) ? currentLanguage->none : ANALOG_FILTER_LEVELS[tmpQuickMenuAnalogFilterLevel]);
				}
				break;
			default:
				strcpy(buf, "");
		}

		buf[bufferLen - 1] = 0;
		menuDisplayEntry(i, mNum, buf);
	}

	ucRender();
	displayLightTrigger();
}


static void handleQuickMenuEvent(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
	{
		menuChannelModeStopScanning();
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		switch(gMenusCurrentItemIndex)
		{
			case CH_SCREEN_QUICK_MENU_SCAN:
				startScan();
				menuSystemPopPreviousMenu();
				break;
			case CH_SCREEN_QUICK_MENU_COPY2VFO:
				memcpy(&settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq,&channelScreenChannelData.rxFreq,sizeof(struct_codeplugChannel_t) - 16);// Don't copy the name of channel, which are in the first 16 bytes
				menuSystemPopAllAndDisplaySpecificRootMenu(MENU_VFO_MODE);
				break;
			case CH_SCREEN_QUICK_MENU_COPY_FROM_VFO:
				memcpy(&channelScreenChannelData.rxFreq,&settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq,sizeof(struct_codeplugChannel_t)- 16);// Don't copy the name of the vfo, which are in the first 16 bytes
				codeplugChannelSaveDataForIndex(settingsCurrentChannelNumber,&channelScreenChannelData);
				menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);
				break;
			case CH_SCREEN_QUICK_MENU_FILTER:
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
				menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);
				break;
		}
		return;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case CH_SCREEN_QUICK_MENU_FILTER:
				if (trxGetMode() == RADIO_MODE_DIGITAL) {
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
		}
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_LEFT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case CH_SCREEN_QUICK_MENU_FILTER:
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
				break;
		}

	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
	{
		switch(gMenusCurrentItemIndex)
		{
			case CH_SCREEN_QUICK_MENU_FILTER:
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
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		MENU_INC(gMenusCurrentItemIndex, NUM_CH_SCREEN_QUICK_MENU_ITEMS);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		MENU_DEC(gMenusCurrentItemIndex, NUM_CH_SCREEN_QUICK_MENU_ITEMS);
	}
	else if (((ev->events & BUTTON_EVENT) && (ev->buttons & BUTTON_ORANGE)) && (gMenusCurrentItemIndex==CH_SCREEN_QUICK_MENU_SCAN))
	{
		startScan();
		menuSystemPopPreviousMenu();
		return;
	}

	updateQuickMenuScreen();
}


int menuChannelModeQuickMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuChannelModeStopScanning();
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

//Scan Mode

static void startScan(void)
{
	scanDirection = 1;

	for(int i= 0;i<MAX_ZONE_SCAN_NUISANCE_CHANNELS;i++)						//clear all nuisance delete channels at start of scanning
	{
		nuisanceDelete[i]=-1;
	}
	nuisanceDeleteIndex=0;

	scanActive = true;
	scanTimer = SCAN_SHORT_PAUSE_TIME;
	scanState = SCAN_SCANNING;
	menuSystemPopAllAndDisplaySpecificRootMenu(MENU_CHANNEL_MODE);

	//get current channel index
	if (currentZone.NOT_IN_MEMORY_isAllChannelsZone)
	{
		nextChannelIndex = nonVolatileSettings.currentChannelIndexInAllZone;
	}
	else
	{
		nextChannelIndex = currentZone.channels[nonVolatileSettings.currentChannelIndexInZone];
	}
	nextChannelReady = false;

}

static void scanning(void)
{
	if((scanState == SCAN_SCANNING) && (scanTimer > SCAN_SKIP_CHANNEL_INTERVAL) && (scanTimer < ( SCAN_TOTAL_INTERVAL - SCAN_FREQ_CHANGE_SETTLING_INTERVAL)))							    			//after initial settling time
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
					scanTimer = SCAN_SHORT_PAUSE_TIME;	//start short delay to allow full detection of signal
					scanState = SCAN_SHORT_PAUSED;		//state 1 = pause and test for valid signal that produces audio
				}
			}
		}
	}

	if(((scanState == SCAN_PAUSED) && (nonVolatileSettings.scanModePause == SCAN_MODE_HOLD)) || (scanState == SCAN_SHORT_PAUSED))   // only do this once if scan mode is PAUSE do it every time if scan mode is HOLD
	{
	    //if (GPIO_PinRead(GPIO_audio_amp_enable, Pin_audio_amp_enable) == 1)	    	// if speaker on we must be receiving a signal so extend the time before resuming scan.
	    if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
	    {
	    	scanTimer = nonVolatileSettings.scanDelay * 1000;
	    	scanState = SCAN_PAUSED;
	    }
	}

	if (!nextChannelReady)
	{
		searchNextChannel();
	}

	if(scanTimer > 0)
	{
		scanTimer--;
	}
	else
	{
		if (nextChannelReady)
		{
			setNextChannel();
			trx_measure_count = 0;

			if ((trxGetMode() == RADIO_MODE_DIGITAL) && (trxDMRMode == DMR_MODE_ACTIVE) && (SCAN_TOTAL_INTERVAL < SCAN_DMR_SIMPLEX_MIN_INTERVAL) )				//allow extra time if scanning a simplex DMR channel.
			{
				scanTimer = SCAN_DMR_SIMPLEX_MIN_INTERVAL;
			}
			else
			{
				scanTimer = SCAN_TOTAL_INTERVAL;
			}
		}

		scanState = SCAN_SCANNING;													//state 0 = settling and test for carrier present.
	}
}

void menuChannelModeStopScanning(void)
{
	scanActive = false;
}

void menuChannelColdStart(void)
{
	channelScreenChannelData.rxFreq = 0;	// Force to re-read codeplug data (needed due to "All Channels" translation)
}

static void menuChannelUpdateTrxID(void )
{
	if (nonVolatileSettings.overrideTG != 0)
	{
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}
	else
	{
		nonVolatileSettings.tsManualOverride &= 0xF0; // remove TS override for channel

		if (rxGroupData.name[0]!=0 && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < rxGroupData.NOT_IN_MEMORY_numTGsInGroup)
		{
			codeplugContactGetDataForIndex(rxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]],&contactData);
		}
		else
		{
			codeplugContactGetDataForIndex(channelScreenChannelData.contact,&contactData);
		}

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&contactData);
		trxTalkGroupOrPcId = contactData.tgNumber;
	}
	lastHeardClearLastID();
    menuClearPrivateCall();
}
