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

#include <hardware/fw_EEPROM.h>
#include <hardware/fw_HR-C6000.h>
#include <hardware/fw_SPI_Flash.h>
#include <user_interface/menuSystem.h>
#include <user_interface/menuUtilityQSOData.h>
#include <user_interface/uiLocalisation.h>
#include "fw_trx.h"
#include "fw_settings.h"
#include <math.h>

void updateLastHeardList(int id,int talkGroup);

const int QSO_TIMER_TIMEOUT = 2400;
const int TX_TIMER_Y_OFFSET = 8;
const int CONTACT_Y_POS = 16;
static const int BAR_Y_POS = 10;


static const int DMRID_MEMORY_STORAGE_START = 0x30000;
static const int DMRID_HEADER_LENGTH = 0x0C;
LinkItem_t callsList[NUM_LASTHEARD_STORED];
LinkItem_t *LinkHead = callsList;
int numLastHeard=0;
int menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
int qsodata_timer;
int RssiUpdateCounter;
const int RSSI_UPDATE_COUNTER_RELOAD = 500;

uint32_t menuUtilityReceivedPcId 	= 0;// No current Private call awaiting acceptance
uint32_t menuUtilityTgBeforePcMode 	= 0;// No TG saved, prior to a Private call being accepted.

const char *POWER_LEVELS[]={"250mW","500mW","750mW","1W","2W","3W","4W","5W"};
const char *DMR_FILTER_LEVELS[]={"None","CC","CC,TS","CC,TS,TG"};

/*
 * Remove space at the end of the array, and return pointer to first non space character
 */
static char *chomp(char *str)
{
	char *sp = str, *ep = str;

	while (*ep != '\0')
		ep++;

	// Spaces at the end
	while (ep > str)
	{
		if (*ep == '\0')
			;
		else if (*ep == ' ')
			*ep = '\0';
		else
			break;

		ep--;
	}

	// Spaces at the beginning
	while (*sp == ' ')
		sp++;

	return sp;
}


static int32_t getCallsignEndingPos(char *str)
{
	char *p = str;

	while(*p != '\0')
	{
		if (*p == ' ')
			return (p - str);

		p++;
	}

	return -1;
}

void lastheardInitList(void)
{
    LinkHead = callsList;

    for(int i=0;i<NUM_LASTHEARD_STORED;i++)
    {
    	callsList[i].id=0;
        callsList[i].talkGroupOrPcId=0;
        if (i==0)
        {
            callsList[i].prev=NULL;
        }
        else
        {
            callsList[i].prev=&callsList[i-1];
        }
        if (i<(NUM_LASTHEARD_STORED-1))
        {
            callsList[i].next=&callsList[i+1];
        }
        else
        {
            callsList[i].next=NULL;
        }
    }
}

LinkItem_t * findInList(int id)
{
    LinkItem_t *item = LinkHead;

    while(item->next!=NULL)
    {
        if (item->id==id)
        {
            // found it
            return item;
        }
        item=item->next;
    }
    return NULL;
}

volatile uint32_t lastID=0;// This needs to be volatile as lastHeardClearLastID() is called from an ISR
uint32_t lastTG=0;

void lastHeardClearLastID(void)
{
	lastID=0;
}

bool lastHeardListUpdate(uint8_t *dmrDataBuffer)
{
	bool retVal = false;
	uint32_t talkGroupOrPcId = (dmrDataBuffer[0]<<24) + (dmrDataBuffer[3]<<16)+(dmrDataBuffer[4]<<8)+(dmrDataBuffer[5]<<0);


	if (dmrDataBuffer[0]==TG_CALL_FLAG || dmrDataBuffer[0]==PC_CALL_FLAG)
	{
		uint32_t id=(dmrDataBuffer[6]<<16)+(dmrDataBuffer[7]<<8)+(dmrDataBuffer[8]<<0);

		if (id!=lastID)
		{
			retVal = true;// something has changed
			lastID=id;

			LinkItem_t *item = findInList(id);

			if (item!=NULL)
			{
				// Already in the list
				item->talkGroupOrPcId = talkGroupOrPcId;// update the TG in case they changed TG
				lastTG = talkGroupOrPcId;

				if (item == LinkHead)
				{
					menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
					return true;// already at top of the list
				}
				else
				{
					// not at top of the list
					// Move this item to the top of the list
					LinkItem_t *next=item->next;
					LinkItem_t *prev=item->prev;

					// set the previous item to skip this item and link to 'items' next item.
					prev->next = next;

					if (item->next!=NULL)
					{
						// not the last in the list
						next->prev = prev;// backwards link the next item to the item before us in the list.
					}

					item->next = LinkHead;// link our next item to the item at the head of the list

					LinkHead->prev = item;// backwards link the hold head item to the item moving to the top of the list.

					item->prev=NULL;// change the items prev to NULL now we are at teh top of the list
					LinkHead = item;// Change the global for the head of the link to the item that is to be at the top of the list.
					if (item->talkGroupOrPcId!=0)
					{
						menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
					}
				}
			}
			else
			{
				// Not in the list
				item = LinkHead;// setup to traverse the list from the top.

				// need to use the last item in the list as the new item at the top of the list.
				// find last item in the list
				while(item->next != NULL )
				{
					item=item->next;
				}
				//item is now the last

				(item->prev)->next = NULL;// make the previous item the last

				LinkHead->prev = item;// set the current head item to back reference this item.
				item->next = LinkHead;// set this items next to the current head
				LinkHead = item;// Make this item the new head

				item->id=id;
				item->talkGroupOrPcId =  talkGroupOrPcId;
				lastTG = talkGroupOrPcId;
				memset(item->talkerAlias,0,32);// Clear any TA data
				if (item->talkGroupOrPcId!=0)
				{
					menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
				}
			}
		}
		else // update TG even if the DMRID did not change
		{
			if (lastTG != talkGroupOrPcId)
			{
				LinkItem_t *item = findInList(id);

				if (item!=NULL)
				{
					// Already in the list
					item->talkGroupOrPcId = talkGroupOrPcId;// update the TG in case they changed TG
				}
				lastTG = talkGroupOrPcId;
				retVal = true;// something has changed
			}
		}
	}
	else
	{
		size_t TAStartPos;
		size_t TABlockLen;
		size_t TAOffset;

		// Data contains the Talker Alias Data
		switch(DMR_frame_buffer[0])
		{
			case 0x04:
				TAOffset=0;
				TAStartPos=3;
				TABlockLen=6;
				break;
			case 0x05:
				TAOffset=6;
				TAStartPos=2;
				TABlockLen=7;
				break;
			case 0x06:
				TAOffset=13;
				TAStartPos=2;
				TABlockLen=7;
				break;
			case 0x07:
				TAOffset=20;
				TAStartPos=2;
				TABlockLen=7;
				break;
			default:
				TAOffset=0;
				TAStartPos=0;
				TABlockLen=0;
				break;
		}

		if (LinkHead->talkerAlias[TAOffset] == 0x00 && TABlockLen!=0)
		{
			memcpy(&LinkHead->talkerAlias[TAOffset],(uint8_t *)&DMR_frame_buffer[TAStartPos],TABlockLen);// Brandmeister seems to send callsign as 6 chars only
			menuDisplayQSODataState=QSO_DISPLAY_CALLER_DATA;
		}
	}
	return retVal;
}

bool dmrIDLookup( int targetId,dmrIdDataStruct_t *foundRecord)
{
	uint32_t l = 0;
	uint32_t numRecords;
	uint32_t r;
	uint32_t m;
	uint32_t recordLenth;//15+4;
	uint8_t headerBuf[32];
	memset(foundRecord,0,sizeof(dmrIdDataStruct_t));

	int targetIdBCD=int2bcd(targetId);

	SPI_Flash_read(DMRID_MEMORY_STORAGE_START,headerBuf,DMRID_HEADER_LENGTH);

	if (headerBuf[0] != 'I' || headerBuf[1] != 'D' || headerBuf[2] != '-')
	{
		return false;
	}

	numRecords = (uint32_t) headerBuf[8] | (uint32_t) headerBuf[9] << 8 | (uint32_t)headerBuf[10] <<16 | (uint32_t)headerBuf[11] << 24 ;

	recordLenth = (uint32_t) headerBuf[3] - 0x4a;

	r = numRecords - 1;

	while (l <= r)
	{
		m = (l + r) >> 1;

		SPI_Flash_read((DMRID_MEMORY_STORAGE_START+DMRID_HEADER_LENGTH) + recordLenth*m,(uint8_t *)foundRecord,recordLenth);

		if (foundRecord->id < targetIdBCD)
		{
			l = m + 1;
		}
		else
		{
			if (foundRecord->id >targetIdBCD)
			{
				r = m - 1;
			}
			else
			{
				return true;
			}
		}
	}
	sprintf(foundRecord->text,"ID:%d",targetId);
	return false;
}

bool menuUtilityHandlePrivateCallActions(int buttons, int keys, int events)
{
	if ((buttons & BUTTON_SK2 )!=0 &&   menuUtilityTgBeforePcMode != 0 && (keys & KEY_RED))
	{
		trxTalkGroupOrPcId = menuUtilityTgBeforePcMode;
		nonVolatileSettings.overrideTG = menuUtilityTgBeforePcMode;
		menuUtilityReceivedPcId = 0;
		menuUtilityTgBeforePcMode = 0;
		menuDisplayQSODataState= QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
		return true;// The event has been handled
	}

	// Note.  menuUtilityReceivedPcId is used to store the PcId but also used as a flag to indicate that a Pc request has occurred.
	if (menuUtilityReceivedPcId != 0x00 && (LinkHead->talkGroupOrPcId>>24) == PC_CALL_FLAG && nonVolatileSettings.overrideTG != LinkHead->talkGroupOrPcId)
	{
		if ((keys & KEY_GREEN)!=0)
		{
			// User has accepted the private call
			menuUtilityTgBeforePcMode = trxTalkGroupOrPcId;// save the current TG
			nonVolatileSettings.overrideTG =  menuUtilityReceivedPcId;
			trxTalkGroupOrPcId = menuUtilityReceivedPcId;
			settingsPrivateCallMuteMode=false;
			menuUtilityRenderQSOData();
		}
		menuUtilityReceivedPcId = 0;
		qsodata_timer=1;// time out the qso timer will force the VFO or Channel mode screen to redraw its normal display
		return true;// The event has been handled
	}
	return false;// The event has not been handled
}

static void displayChannelNameOrRxFrequency(char *buffer)
{
	if (menuSystemGetCurrentMenuNumber() == MENU_CHANNEL_MODE)
	{
		codeplugUtilConvertBufToString(currentChannelData->name,buffer,16);
	}
	else
	{
		int val_before_dp = currentChannelData->rxFreq/100000;
		int val_after_dp = currentChannelData->rxFreq - val_before_dp*100000;
		sprintf(buffer,currentLanguage->displayRxFreq,val_before_dp, val_after_dp);
	}
	UC1701_printCentered(52,buffer,UC1701_FONT_6x8);
}

/*
 * Try to extract callsign and extra text from TA or DMR ID data, then display that on
 * two lines, if possible.
 * We don't care if extra text is larger than 16 chars, UC1701_print*() functions cut them.
 *.
 */
static void displayContactTextInfos(char *text, size_t maxLen,bool isFromTalkerAlias)
{
	char buffer[32];

	if (strlen(text) >= 5 && isFromTalkerAlias) // if its Talker Alias and there is more text than just the callsign, split across 2 lines
	{
		char    *pbuf;
		int32_t  cpos;

		if ((cpos = getCallsignEndingPos(text)) != -1)
		{
			// Callsign found
			memcpy(buffer, text, cpos);
			buffer[cpos] = 0;
			UC1701_printCentered(32, chomp(buffer), UC1701_FONT_8x16);

			memcpy(buffer, text + (cpos + 1), (maxLen - (cpos + 1)));
			buffer[(strlen(text) - (cpos + 1))] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
				UC1701_printAt(0, 48, pbuf, UC1701_FONT_8x16);
			else
				displayChannelNameOrRxFrequency(buffer);
		}
		else
		{
			// No space found, use a chainsaw
			memcpy(buffer, text, 6);
			buffer[6] = 0;

			UC1701_printCentered(32, chomp(buffer), UC1701_FONT_8x16);

			memcpy(buffer, text + 6, (maxLen - 6));
			buffer[(strlen(text) - 6)] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
				UC1701_printAt(0, 48, pbuf, UC1701_FONT_8x16);
			else
				displayChannelNameOrRxFrequency(buffer);
		}
	}
	else
	{
		memcpy(buffer, text, strlen(text));
		buffer[strlen(text)] = 0;
		UC1701_printCentered(32, chomp(buffer), UC1701_FONT_8x16);
		displayChannelNameOrRxFrequency(buffer);
	}
}

void menuUtilityRenderQSOData(void)
{
	char buffer[32];// buffer passed to the DMR ID lookup function, needs to be large enough to hold worst case text length that is returned. Currently 16+1
	dmrIdDataStruct_t currentRec;

	menuUtilityReceivedPcId=0;//reset the received PcId

	if ((LinkHead->talkGroupOrPcId>>24) == PC_CALL_FLAG)
	{
		// Its a Private call
		dmrIDLookup( (LinkHead->id & 0xFFFFFF),&currentRec);
		snprintf(buffer, 20, "%s", currentRec.text);
		buffer[20] = 0;
		UC1701_printCentered(16, buffer,UC1701_FONT_8x16);

		// Are we already in PC mode to this caller ?
		if (trxTalkGroupOrPcId != (LinkHead->id | (PC_CALL_FLAG<<24)))
		{
			// No either we are not in PC mode or not on a Private Call to this station
			UC1701_printCentered(32, currentLanguage->accept_call,UC1701_FONT_8x16);
			UC1701_drawChoice(UC1701_CHOICE_YESNO, false);
			menuUtilityReceivedPcId = LinkHead->id | (PC_CALL_FLAG<<24);
		    set_melody(melody_private_call);
		}
		else
		{
			UC1701_printCentered(32, currentLanguage->private_call,UC1701_FONT_8x16);
		}
	}
	else
	{
		// Group call
		uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);
		sprintf(buffer,"%s %d",currentLanguage->tg, tg);
		if (tg != trxTalkGroupOrPcId || (dmrMonitorCapturedTS!=-1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot()) ||
										(dmrMonitorCapturedCC!=-1 && dmrMonitorCapturedCC != trxGetDMRColourCode()))
		{
			UC1701_fillRect(0,16,128,16,false);// fill background with black
			UC1701_printCore(0, CONTACT_Y_POS, buffer,UC1701_FONT_8x16,UC1701_TEXT_ALIGN_CENTER,true);// draw the text in inverse video
		}
		else
		{
			UC1701_printCentered(CONTACT_Y_POS, buffer,UC1701_FONT_8x16);
		}

		// first check if we have this ID in the DMR ID data
		if (dmrIDLookup(LinkHead->id, &currentRec))
		{
			displayContactTextInfos(currentRec.text, sizeof(currentRec.text),false);
		}
		else
		{
			// We don't have this ID, so try looking in the Talker alias data
			if (LinkHead->talkerAlias[0] != 0x00)
			{
				displayContactTextInfos(LinkHead->talkerAlias, sizeof(LinkHead->talkerAlias),true);
			}
			else
			{
				// No talker alias. So we can only show the ID.
				sprintf(buffer, "ID: %d", LinkHead->id);
				UC1701_printCentered(32, buffer, UC1701_FONT_8x16);
				displayChannelNameOrRxFrequency(buffer);
			}
		}
	}
}

void menuUtilityRenderHeader(void)
{
	const int Y_OFFSET = 2;
	char buffer[24];

	if (!trxIsTransmitting)
	{
		drawRSSIBarGraph();
	}
	else
	{
		if (trxGetMode() == RADIO_MODE_DIGITAL)
		{
			drawDMRMicLevelBarGraph();
		}
	}


	switch(trxGetMode())
	{
		case RADIO_MODE_ANALOG:
			strcpy(buffer, "FM");
			if (!trxGetBandwidthIs25kHz())
			{
				strcat(buffer,"N");
			}
			if ((currentChannelData->txTone!=65535)||(currentChannelData->rxTone!=65535))
			{
				strcat(buffer," C");
			}
			if (currentChannelData->txTone!=65535)
			{
				strcat(buffer,"T");
			}
			if (currentChannelData->rxTone!=65535)
			{
				strcat(buffer,"R");
			}
			UC1701_printAt(0,Y_OFFSET, buffer,UC1701_FONT_6x8);
			break;
		case RADIO_MODE_DIGITAL:


			if (settingsUsbMode == USB_MODE_HOTSPOT)
			{
				UC1701_printAt(0,Y_OFFSET, "DMR",UC1701_FONT_6x8);
			}
			else
			{
//				(trxGetMode() == RADIO_MODE_DIGITAL && settingsPrivateCallMuteMode == true)?" MUTE":"");// The location that this was displayed is now used for the power level

				UC1701_printAt(0,Y_OFFSET, "DMR",UC1701_FONT_6x8);
				sprintf(buffer, "%s%d",currentLanguage->ts,trxGetDMRTimeSlot()+1);
				if (nonVolatileSettings.dmrFilterLevel < DMR_FILTER_CC_TS)
				{
					UC1701_fillRect(20, Y_OFFSET,20,8,false);
					UC1701_printCore(22,Y_OFFSET, buffer,UC1701_FONT_6x8,UC1701_TEXT_ALIGN_LEFT,true);
				}
				else
				{
					UC1701_printCore(22,Y_OFFSET, buffer,UC1701_FONT_6x8,UC1701_TEXT_ALIGN_LEFT,false);
				}
			}
			break;
	}

	if (keypadLocked)
	{
		UC1701_printAt(83, Y_OFFSET, "L", UC1701_FONT_6x8);
	}

	UC1701_printCentered(Y_OFFSET,(char *)POWER_LEVELS[nonVolatileSettings.txPowerLevel],UC1701_FONT_6x8);


	int  batteryPerentage = (int)(((averageBatteryVoltage - CUTOFF_VOLTAGE_UPPER_HYST) * 100) / (BATTERY_MAX_VOLTAGE - CUTOFF_VOLTAGE_UPPER_HYST));
	if (batteryPerentage>100)
	{
		batteryPerentage=100;
	}
	if (batteryPerentage<0)
	{
		batteryPerentage=0;
	}
	if (settingsUsbMode == USB_MODE_HOTSPOT || trxGetMode() == RADIO_MODE_ANALOG)
	{
		// In hotspot mode the CC is show as part of the rest of the display and in Analogue mode the CC is meaningless
		sprintf(buffer,"%d%%",batteryPerentage);
	}
	else
	{
		sprintf(buffer,"C%d %d%%",trxGetDMRColourCode(),batteryPerentage);
	}

	UC1701_printCore(0,Y_OFFSET,buffer,UC1701_FONT_6x8,UC1701_TEXT_ALIGN_RIGHT,false);// Display battery percentage at the right
}

void drawRSSIBarGraph(void)
{
	int dBm,barGraphLength;

	UC1701_fillRect(0, BAR_Y_POS,128,4,true);

	if (trxCheckFrequencyIsUHF(trxGetFrequency()))
	{
		// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
		dBm = -151 + trxRxSignal;// Note no the RSSI value on UHF does not need to be scaled like it does on VHF
	}
	else
	{
		// VHF
		// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
		dBm = -164 + ((trxRxSignal * 32) / 27);
	}

	barGraphLength = ((dBm + 130) * 24)/10;
	if (barGraphLength<0)
	{
		barGraphLength=0;
	}

	if (barGraphLength>123)
	{
		barGraphLength=123;
	}
	UC1701_fillRect(0, BAR_Y_POS,barGraphLength,4,false);
	trxRxSignal=0;
}

void drawDMRMicLevelBarGraph(void)
{
	float barGraphLength = sqrt(micAudioSamplesTotal)*1.5;

	UC1701_fillRect(0, BAR_Y_POS,128,3,true);

	if (barGraphLength > 127)
	{
		barGraphLength = 127;
	}

	UC1701_fillRect(0, BAR_Y_POS,(int)barGraphLength,3,false);
}

void setOverrideTGorPC(int tgOrPc, bool privateCall) {
	uint32_t saveTrxTalkGroupOrPcId = trxTalkGroupOrPcId;
	nonVolatileSettings.overrideTG = tgOrPc;
	if (privateCall == true)
	{
		// Private Call

		if ((saveTrxTalkGroupOrPcId >> 24) != PC_CALL_FLAG)
		{
			// if the current Tx TG is a TalkGroup then save it so it can be stored after the end of the private call
			menuUtilityTgBeforePcMode = saveTrxTalkGroupOrPcId;
		}
		nonVolatileSettings.overrideTG |= (PC_CALL_FLAG << 24);
	}
}
