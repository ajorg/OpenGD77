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

#include <EEPROM.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>
#include <math.h>
#include <HR-C6000.h>
#include <settings.h>
#include <SPI_Flash.h>
#include <ticks.h>
#include <trx.h>

settingsStruct_t originalNonVolatileSettings;

const int QSO_TIMER_TIMEOUT = 2400;
const int TX_TIMER_Y_OFFSET = 8;
const int CONTACT_Y_POS = 16;
const int FREQUENCY_X_POS = /* '>Ta'*/ (3 * 8) + 4;
static const int BAR_Y_POS = 10;
const int MAX_POWER_SETTING_NUM = 9;

static const int DMRID_MEMORY_STORAGE_START = 0x30000;
static const int DMRID_HEADER_LENGTH = 0x0C;
__attribute__((section(".data.$RAM2"))) LinkItem_t callsList[NUM_LASTHEARD_STORED];
LinkItem_t *LinkHead = callsList;
int numLastHeard=0;
int menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
int qsodata_timer;
const uint32_t RSSI_UPDATE_COUNTER_RELOAD = 100;

uint32_t menuUtilityReceivedPcId 	= 0;// No current Private call awaiting acceptance
uint32_t menuUtilityTgBeforePcMode 	= 0;// No TG saved, prior to a Private call being accepted.

const char *POWER_LEVELS[]={ "50mW","250mW","500mW","750mW","1W","2W","3W","4W","5W","5W++"};
const char *DMR_FILTER_LEVELS[]={"None","CC","CC,TS","CC,TS,TG","CC,TS,Ct"};
const char *ANALOG_FILTER_LEVELS[]={"None","CTCSS"};

volatile uint32_t lastID=0;// This needs to be volatile as lastHeardClearLastID() is called from an ISR
uint32_t lastTG=0;

static dmrIDsCache_t dmrIDsCache;

int nuisanceDelete[MAX_ZONE_SCAN_NUISANCE_CHANNELS];
int nuisanceDeleteIndex;
int scanTimer=0;
bool scanActive=false;
ScanState_t scanState = SCAN_SCANNING;		//state flag for scan routine.
int scanDirection = 1;

bool displaySquelch=false;

char freq_enter_digits[12] = { '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-' };
int freq_enter_idx;

const int SCAN_SHORT_PAUSE_TIME = 500;			//time to wait after carrier detected to allow time for full signal detection. (CTCSS or DMR)
const int SCAN_TOTAL_INTERVAL = 30;			    //time between each scan step
const int SCAN_DMR_SIMPLEX_MIN_INTERVAL=60;		//minimum time between steps when scanning DMR Simplex. (needs extra time to capture TDMA Pulsing)
const int SCAN_FREQ_CHANGE_SETTLING_INTERVAL = 1;//Time after frequency is changed before RSSI sampling starts
const int SCAN_SKIP_CHANNEL_INTERVAL = 1;		//This is actually just an implicit flag value to indicate the channel should be skipped

/*
 * Remove space at the end of the array, and return pointer to first non space character
 */
char *chomp(char *str)
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

int32_t getFirstSpacePos(char *str)
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

	for(int i = 0; i < NUM_LASTHEARD_STORED; i++)
	{
		callsList[i].id = 0;
		callsList[i].talkGroupOrPcId = 0;
		callsList[i].contact[0] = 0;
		callsList[i].talkgroup[0] = 0;
		callsList[i].talkerAlias[0] = 0;
		callsList[i].locator[0] = 0;
		callsList[i].time = 0;

		if (i == 0)
		{
			callsList[i].prev = NULL;
		}
		else
		{
			callsList[i].prev = &callsList[i - 1];
		}

		if (i < (NUM_LASTHEARD_STORED - 1))
		{
			callsList[i].next = &callsList[i + 1];
		}
		else
		{
			callsList[i].next = NULL;
		}
	}
}

LinkItem_t *lastheardFindInList(uint32_t id)
{
    LinkItem_t *item = LinkHead;

    while (item->next != NULL)
    {
        if (item->id == id)
        {
            // found it
            return item;
        }
        item = item->next;
    }
    return NULL;
}

static uint8_t *coordsToMaidenhead(double longitude, double latitude)
{
	static uint8_t maidenhead[15];
	double l, l2;
	uint8_t c;

	l = longitude;

	for (uint8_t i = 0; i < 2; i++)
	{
		l = l / ((i == 0) ? 20.0 : 10.0) + 9.0;
		c = (uint8_t) l;
		maidenhead[0 + i] = c + 'A';
		l2 = c;
		l -= l2;
		l *= 10.0;
		c = (uint8_t) l;
		maidenhead[2 + i] = c + '0';
		l2 = c;
		l -= l2;
		l *= 24.0;
		c = (uint8_t) l;
		maidenhead[4 + i] = c + 'A';

#if 0
		if (extended)
		{
			l2 = c;
			l -= l2;
			l *= 10.0;
			c = (uint8_t) l;
			maidenhead[6 + i] = c + '0';
			l2 = c;
			l -= l2;
			l *= 24.0;
			c = (uint8_t) l;
			maidenhead[8 + i] = c + (extended ? 'A' : 'a');
			l2 = c;
			l -= l2;
			l *= 10.0;
			c = (uint8_t) l;
			maidenhead[10 + i] = c + '0';
			l2 = c;
			l -= l2;
			l *= 24.0;
			c = (uint8_t) l;
			maidenhead[12 + i] = c + (extended ? 'A' : 'a');
		}
#endif

		l = latitude;
	}

#if 0
	maidenhead[extended ? 14 : 6] = '\0';
#else
	maidenhead[6] = '\0';
#endif

	return &maidenhead[0];
}

static uint8_t *decodeGPSPosition(uint8_t *data)
{
#if 0
	uint8_t errorI = (data[2U] & 0x0E) >> 1U;
	const char* error;
	switch (errorI) {
	case 0U:
		error = "< 2m";
		break;
	case 1U:
		error = "< 20m";
		break;
	case 2U:
		error = "< 200m";
		break;
	case 3U:
		error = "< 2km";
		break;
	case 4U:
		error = "< 20km";
		break;
	case 5U:
		error = "< 200km";
		break;
	case 6U:
		error = "> 200km";
		break;
	default:
		error = "not known";
		break;
	}
#endif

	int32_t longitudeI = ((data[2U] & 0x01U) << 31) | (data[3U] << 23) | (data[4U] << 15) | (data[5U] << 7);
	longitudeI >>= 7;

	int32_t latitudeI = (data[6U] << 24) | (data[7U] << 16) | (data[8U] << 8);
	latitudeI >>= 8;

	float longitude = 360.0F / 33554432.0F;	// 360/2^25 steps
	float latitude  = 180.0F / 16777216.0F;	// 180/2^24 steps

	longitude *= (float)longitudeI;
	latitude  *= (float)latitudeI;

	return (coordsToMaidenhead(longitude, latitude));
}

static uint8_t *decodeTA(uint8_t *TA)
{
	uint8_t *b;
	uint8_t c;
	int8_t j;
    uint8_t i, t1, t2;
    static uint8_t buffer[32];
    uint8_t *talkerAlias = TA;
    uint8_t TAformat = (talkerAlias[0] >> 6U) & 0x03U;
    uint8_t TAsize   = (talkerAlias[0] >> 1U) & 0x1FU;

    switch (TAformat)
    {
		case 0U:		// 7 bit
			memset(&buffer, 0, sizeof(buffer));
			b = &talkerAlias[0];
			t1 = 0U; t2 = 0U; c = 0U;

			for (i = 0U; (i < 32U) && (t2 < TAsize); i++)
			{
				for (j = 7; j >= 0; j--)
				{
					c = (c << 1U) | (b[i] >> j);

					if (++t1 == 7U)
					{
						if (i > 0U)
							buffer[t2++] = c & 0x7FU;

						t1 = 0U;
						c = 0U;
					}
				}
			}
			buffer[TAsize] = 0;
			break;

		case 1U:		// ISO 8 bit
		case 2U:		// UTF8
			memcpy(&buffer, talkerAlias + 1U, sizeof(buffer));
			break;

		case 3U:		// UTF16 poor man's conversion
			t2=0;
			memset(&buffer, 0, sizeof(buffer));
			for (i = 0U; (i < 15U) && (t2 < TAsize); i++)
			{
				if (talkerAlias[2U * i + 1U] == 0)
					buffer[t2++] = talkerAlias[2U * i + 2U];
				else
					buffer[t2++] = '?';
			}
			buffer[TAsize] = 0;
			break;
    }

	return &buffer[0];
}

void lastHeardClearLastID(void)
{
	lastID=0;
}

static void updateLHItem(LinkItem_t *item)
{
	static const int bufferLen = 33; // displayChannelNameOrRxFrequency() use 6x8 font
	char buffer[bufferLen];// buffer passed to the DMR ID lookup function, needs to be large enough to hold worst case text length that is returned. Currently 16+1
	dmrIdDataStruct_t currentRec;

	if ((item->talkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		// Its a Private call
		switch (nonVolatileSettings.contactDisplayPriority)
		{
			case CONTACT_DISPLAY_PRIO_CC_DB_TA:
			case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
				{
					snprintf(item->contact, 16, "%s", buffer);
					item->contact[16] = 0;
				}
				else
				{
					dmrIDLookup(item->id, &currentRec);
					snprintf(item->contact, 16, "%s", currentRec.text);
					item->contact[16] = 0;
				}
				break;

			case CONTACT_DISPLAY_PRIO_DB_CC_TA:
			case CONTACT_DISPLAY_PRIO_TA_DB_CC:
				if (dmrIDLookup(item->id, &currentRec) == true)
				{
					snprintf(item->contact, 16, "%s", currentRec.text);
					item->contact[16] = 0;
				}
				else
				{
					if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
					{
						snprintf(item->contact, 16, "%s", buffer);
						item->contact[16] = 0;
					}
					else
					{
						snprintf(item->contact, 16, "%s", currentRec.text);
						item->contact[16] = 0;
					}
				}
				break;
		}

		if (item->talkGroupOrPcId != (trxDMRID | (PC_CALL_FLAG << 24)))
		{
			if (contactIDLookup(item->talkGroupOrPcId & 0x00FFFFFF, CONTACT_CALLTYPE_PC, buffer) == true)
			{
				snprintf(item->talkgroup, 16, "%s", buffer);
				item->talkgroup[16] = 0;
			}
			else
			{
				dmrIDLookup(item->talkGroupOrPcId & 0x00FFFFFF, &currentRec);
				snprintf(item->talkgroup, 16, "%s", currentRec.text);
				item->talkgroup[16] = 0;
			}
		}
	}
	else
	{
		// TalkGroup
		if (contactIDLookup(item->talkGroupOrPcId, CONTACT_CALLTYPE_TG, buffer) == true)
		{
			snprintf(item->talkgroup, 16, "%s", buffer);
			item->talkgroup[16] = 0;
		}
		else
		{
			snprintf(item->talkgroup, 16, "%s %d", currentLanguage->tg, (item->talkGroupOrPcId & 0x00FFFFFF));
			item->talkgroup[16] = 0;
		}

		switch (nonVolatileSettings.contactDisplayPriority)
		{
			case CONTACT_DISPLAY_PRIO_CC_DB_TA:
			case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
				{
					snprintf(item->contact, 20, "%s", buffer);
					item->contact[20] = 0;
				}
				else
				{
					dmrIDLookup((item->id & 0x00FFFFFF), &currentRec);
					snprintf(item->contact, 20, "%s", currentRec.text);
					item->contact[20] = 0;
				}
				break;

			case CONTACT_DISPLAY_PRIO_DB_CC_TA:
			case CONTACT_DISPLAY_PRIO_TA_DB_CC:
				if (dmrIDLookup((item->id & 0x00FFFFFF), &currentRec) == true)
				{
					snprintf(item->contact, 20, "%s", currentRec.text);
					item->contact[20] = 0;
				}
				else
				{
					if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
					{
						snprintf(item->contact, 20, "%s", buffer);
						item->contact[20] = 0;
					}
					else
					{
						snprintf(item->contact, 20, "%s", currentRec.text);
						item->contact[20] = 0;
					}
				}
				break;
		}
	}
}

bool lastHeardListUpdate(uint8_t *dmrDataBuffer, bool forceOnHotspot)
{
	static uint8_t bufferTA[32];
	static uint8_t blocksTA = 0x00;
	bool retVal = false;
	uint32_t talkGroupOrPcId = (dmrDataBuffer[0] << 24) + (dmrDataBuffer[3] << 16) + (dmrDataBuffer[4] << 8) + (dmrDataBuffer[5] << 0);
	static bool overrideTA = false;

	if ((HRC6000GetReceivedTgOrPcId() != 0) || forceOnHotspot)
	{
		if (dmrDataBuffer[0] == TG_CALL_FLAG || dmrDataBuffer[0] == PC_CALL_FLAG)
		{
			uint32_t id = (dmrDataBuffer[6] << 16) + (dmrDataBuffer[7] << 8) + (dmrDataBuffer[8] << 0);

			if (id != lastID)
			{
				memset(bufferTA, 0, 32);// Clear any TA data in TA buffer (used for decode)
				blocksTA = 0x00;
				overrideTA = false;

				retVal = true;// something has changed
				lastID = id;

				LinkItem_t *item = lastheardFindInList(id);

				// Already in the list
				if (item != NULL)
				{
					if (item->talkGroupOrPcId != talkGroupOrPcId)
					{
						item->talkGroupOrPcId = talkGroupOrPcId; // update the TG in case they changed TG
						updateLHItem(item);
					}

					item->time = fw_millis();
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
						LinkItem_t *next = item->next;
						LinkItem_t *prev = item->prev;

						// set the previous item to skip this item and link to 'items' next item.
						prev->next = next;

						if (item->next != NULL)
						{
							// not the last in the list
							next->prev = prev;// backwards link the next item to the item before us in the list.
						}

						item->next = LinkHead;// link our next item to the item at the head of the list

						LinkHead->prev = item;// backwards link the hold head item to the item moving to the top of the list.

						item->prev = NULL;// change the items prev to NULL now we are at teh top of the list
						LinkHead = item;// Change the global for the head of the link to the item that is to be at the top of the list.
						if (item->talkGroupOrPcId != 0)
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
					while(item->next != NULL)
					{
						item = item->next;
					}
					//item is now the last

					(item->prev)->next = NULL;// make the previous item the last

					LinkHead->prev = item;// set the current head item to back reference this item.
					item->next = LinkHead;// set this items next to the current head
					LinkHead = item;// Make this item the new head

					item->id = id;
					item->talkGroupOrPcId = talkGroupOrPcId;
					item->time = fw_millis();
					lastTG = talkGroupOrPcId;

					memset(item->contact, 0, sizeof(item->contact)); // Clear contact's datas
					memset(item->talkgroup, 0, sizeof(item->talkgroup));
					memset(item->talkerAlias, 0, sizeof(item->talkerAlias));
					memset(item->locator, 0, sizeof(item->locator));

					updateLHItem(item);

					if (item->talkGroupOrPcId != 0)
					{
						menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
					}
				}
			}
			else // update TG even if the DMRID did not change
			{
				if (lastTG != talkGroupOrPcId)
				{
					LinkItem_t *item = lastheardFindInList(id);

					if (item != NULL)
					{
						// Already in the list
						item->talkGroupOrPcId = talkGroupOrPcId;// update the TG in case they changed TG
						updateLHItem(item);
						item->time = fw_millis();
					}

					lastTG = talkGroupOrPcId;
					memset(bufferTA, 0, 32);// Clear any TA data in TA buffer (used for decode)
					blocksTA = 0x00;
					overrideTA = false;
					retVal = true;// something has changed
				}
			}
		}
		else
		{
			// Data contains the Talker Alias Data
			uint8_t blockID = (forceOnHotspot ? dmrDataBuffer[0] : DMR_frame_buffer[0]) - 4;

			// ID 0x04..0x07: TA
			if (blockID < 4)
			{

				// Already stored first byte in block TA Header has changed, lets clear other blocks too
				if ((blockID == 0) && ((blocksTA & (1 << blockID)) != 0) &&
						(bufferTA[0] != (forceOnHotspot ? dmrDataBuffer[2] : DMR_frame_buffer[2])))
				{
					blocksTA &= ~(1 << 0);

					// Clear all other blocks if they're already stored
					if ((blocksTA & (1 << 1)) != 0)
					{
						blocksTA &= ~(1 << 1);
						memset(bufferTA + 7, 0, 7); // Clear 2nd TA block
					}
					if ((blocksTA & (1 << 2)) != 0)
					{
						blocksTA &= ~(1 << 2);
						memset(bufferTA + 14, 0, 7); // Clear 3rd TA block
					}
					if ((blocksTA & (1 << 3)) != 0)
					{
						blocksTA &= ~(1 << 3);
						memset(bufferTA + 21, 0, 7); // Clear 4th TA block
					}
					overrideTA = true;
				}

				// We don't already have this TA block
				if ((blocksTA & (1 << blockID)) == 0)
				{
					static const uint8_t blockLen = 7;
					uint32_t blockOffset = blockID * blockLen;

					blocksTA |= (1 << blockID);

					if ((blockOffset + blockLen) < sizeof(bufferTA))
					{
						memcpy(bufferTA + blockOffset, (void *)(forceOnHotspot ? &dmrDataBuffer[2] : &DMR_frame_buffer[2]), blockLen);

						// Format and length infos are available, we can decode now
						if (bufferTA[0] != 0x0)
						{
							uint8_t *decodedTA;

							if ((decodedTA = decodeTA(&bufferTA[0])) != NULL)
							{
								// TAs doesn't match, update contact and screen.
								if (overrideTA || (strlen((const char *)decodedTA) > strlen((const char *)&LinkHead->talkerAlias)))
								{
									memcpy(&LinkHead->talkerAlias, decodedTA, 31);// Brandmeister seems to send callsign as 6 chars only

									if ((blocksTA & (1 << 1)) != 0) // we already received the 2nd TA block, check for 'DMR ID:'
									{
										char *p = NULL;

										// Get rid of 'DMR ID:xxxxxxx' part of the TA, sent by BM
										if (((p = strstr(&LinkHead->talkerAlias[0], "DMR ID:")) != NULL) || ((p = strstr(&LinkHead->talkerAlias[0], "DMR I")) != NULL))
										{
											*p = 0;
										}
									}

									overrideTA = false;
									menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
								}
							}
						}
					}
				}
			}
			else if (blockID == 4) // ID 0x08: GPS
			{
				uint8_t *locator = decodeGPSPosition((uint8_t *)(forceOnHotspot ? &dmrDataBuffer[0] : &DMR_frame_buffer[0]));

				if (strncmp((char *)&LinkHead->locator, (char *)locator, 7) != 0)
				{
					memcpy(&LinkHead->locator, locator, 7);
					menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA_UPDATE;
				}
			}
		}
	}

	return retVal;
}


static void dmrIDReadContactInFlash(uint32_t contactOffset, uint8_t *data, uint32_t len)
{
	SPI_Flash_read((DMRID_MEMORY_STORAGE_START + DMRID_HEADER_LENGTH) + contactOffset, data, len);
}


void dmrIDCacheInit(void)
{
	uint8_t headerBuf[32];

	memset(&dmrIDsCache, 0, sizeof(dmrIDsCache_t));
	memset(&headerBuf, 0, sizeof(headerBuf));

	SPI_Flash_read(DMRID_MEMORY_STORAGE_START, headerBuf, DMRID_HEADER_LENGTH);

	if (headerBuf[0] != 'I' || headerBuf[1] != 'D' || headerBuf[2] != '-')
	{
		return;
	}

	dmrIDsCache.entries = ((uint32_t)headerBuf[8] | (uint32_t)headerBuf[9] << 8 | (uint32_t)headerBuf[10] << 16 | (uint32_t)headerBuf[11] << 24);
	dmrIDsCache.contactLength = (uint8_t)headerBuf[3] - 0x4a;

	if (dmrIDsCache.entries > 0)
	{
		dmrIdDataStruct_t dmrIDContact;

		// Set Min and Max IDs boundaries
		// First available ID
		dmrIDReadContactInFlash(0, (uint8_t *)&dmrIDContact, 4U);
		dmrIDsCache.slices[0] = dmrIDContact.id;

		// Last available ID
		dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.entries - 1)), (uint8_t *)&dmrIDContact, 4U);
		dmrIDsCache.slices[ID_SLICES - 1] = dmrIDContact.id;

		if (dmrIDsCache.entries > MIN_ENTRIES_BEFORE_USING_SLICES)
		{
			dmrIDsCache.IDsPerSlice = dmrIDsCache.entries / (ID_SLICES - 1);

			for (uint8_t i = 0; i < (ID_SLICES - 2); i++)
			{
				dmrIDReadContactInFlash((dmrIDsCache.contactLength * ((dmrIDsCache.IDsPerSlice * i) + dmrIDsCache.IDsPerSlice)), (uint8_t *)&dmrIDContact, 4U);
				dmrIDsCache.slices[i + 1] = dmrIDContact.id;
			}
		}
	}
}

bool dmrIDLookup(int targetId, dmrIdDataStruct_t *foundRecord)
{
	int targetIdBCD = int2bcd(targetId);

	if ((dmrIDsCache.entries > 0) && (targetIdBCD >= dmrIDsCache.slices[0]) && (targetIdBCD <= dmrIDsCache.slices[ID_SLICES - 1]))
	{
		uint32_t startPos = 0;
		uint32_t endPos = dmrIDsCache.entries - 1;
		uint32_t curPos;

		if (dmrIDsCache.entries > MIN_ENTRIES_BEFORE_USING_SLICES) // Use slices
		{
			for (uint8_t i = 0; i < ID_SLICES - 1; i++)
			{
				// Check if ID is in slices boundaries, with a special case for the last slice as [ID_SLICES - 1] is the last ID
				if ((targetIdBCD >= dmrIDsCache.slices[i]) &&
						((i == ID_SLICES - 2) ? (targetIdBCD <= dmrIDsCache.slices[i + 1]) : (targetIdBCD < dmrIDsCache.slices[i + 1])))
				{
					// targetID is the min slice limit, don't go further
					if (targetIdBCD == dmrIDsCache.slices[i])
					{
						foundRecord->id = dmrIDsCache.slices[i];
						dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.IDsPerSlice * i)) + 4U, (uint8_t *)foundRecord + 4U, (dmrIDsCache.contactLength - 4U));

						return true;
					}

					startPos = dmrIDsCache.IDsPerSlice * i;
					endPos = (i == ID_SLICES - 2) ? (dmrIDsCache.entries - 1) : dmrIDsCache.IDsPerSlice * (i + 1);

					break;
				}
			}
		}
		else // Not enough contact to use slices
		{
			bool isMin;

			// Check if targetID is equal to the first or the last in the IDs list
			if ((isMin = (targetIdBCD == dmrIDsCache.slices[0])) || (targetIdBCD == dmrIDsCache.slices[ID_SLICES - 1]))
			{
				foundRecord->id = dmrIDsCache.slices[(isMin ? 0 : (ID_SLICES - 1))];
				dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.IDsPerSlice * (isMin ? 0 : (ID_SLICES - 1)))) + 4U, (uint8_t *)foundRecord + 4U, (dmrIDsCache.contactLength - 4U));

				return true;
			}
		}

		// Look for the ID now
		while (startPos <= endPos)
		{
			curPos = (startPos + endPos) >> 1;

			dmrIDReadContactInFlash((dmrIDsCache.contactLength * curPos), (uint8_t *)foundRecord, 4U);

			if (foundRecord->id < targetIdBCD)
			{
				startPos = curPos + 1;
			}
			else
			{
				if (foundRecord->id > targetIdBCD)
				{
					endPos = curPos - 1;
				}
				else
				{
					dmrIDReadContactInFlash((dmrIDsCache.contactLength * curPos) + 4U, (uint8_t *)foundRecord + 4U, (dmrIDsCache.contactLength - 4U));
					return true;
				}
			}
		}
	}

	snprintf(foundRecord->text, 20, "ID:%d", targetId);
	return false;
}

bool contactIDLookup(uint32_t id, int calltype, char *buffer)
{
	struct_codeplugContact_t contact;

	int contactIndex = codeplugContactIndexByTGorPC((id & 0x00FFFFFF), calltype, &contact);
	if (contactIndex != 0)
	{
		codeplugUtilConvertBufToString(contact.name, buffer, 16);
		return true;
	}

	return false;
}

static void displayChannelNameOrRxFrequency(char *buffer, size_t maxLen)
{
	if (menuSystemGetCurrentMenuNumber() == MENU_CHANNEL_MODE)
	{
		codeplugUtilConvertBufToString(currentChannelData->name,buffer,16);
	}
	else
	{
		int val_before_dp = currentChannelData->rxFreq/100000;
		int val_after_dp = currentChannelData->rxFreq - val_before_dp*100000;
		snprintf(buffer, maxLen, "%d.%05d MHz", val_before_dp, val_after_dp);
		buffer[maxLen - 1] = 0;
	}
	ucPrintCentered(52, buffer, FONT_6x8);
}

static void printSplitOrSpanText(uint8_t y, char *text)
{
	uint8_t len = strlen(text);

	if (len == 0)
		return;

	if (len <= 16)
	{
		ucPrintCentered(y, text, FONT_8x16);
	}
	else
	{
		uint8_t nLines = len / 21 + (((len % 21) != 0) ? 1 : 0);

		if (nLines > 2)
			nLines = 2;

		if (nLines > 1)
		{
			char buffer[43]; // 2 * 21 chars + NULL

			memcpy(buffer, text, len + 1);

			char *p = buffer + 20;

			// Find a space backward
			while ((*p != ' ') && (p > buffer))
				p--;

			uint8_t rest = (uint8_t)((buffer + strlen(buffer)) - p) - ((*p == ' ') ? 1 : 0);

			// rest is too long, just split the line in two chunks
			if (rest > 21)
			{
				char c = buffer[21];

				buffer[21] = 0;

				ucPrintCentered(y, buffer, FONT_6x8); // 2 pixels are saved, could center

				buffer[21] = c;
				buffer[42] = 0;

				ucPrintCentered(y + 8, buffer + 21, FONT_6x8);
			}
			else
			{
				*p = 0;

				ucPrintCentered(y, buffer, FONT_6x8);
				ucPrintCentered(y + 8, p + 1, FONT_6x8);
			}
		}
		else // One line of 21 chars max
		{
			ucPrintCentered(y + 4, text, FONT_6x8);
		}
	}
}


/*
 * Try to extract callsign and extra text from TA or DMR ID data, then display that on
 * two lines, if possible.
 * We don't care if extra text is larger than 16 chars, ucPrint*() functions cut them.
 *.
 */
static void displayContactTextInfos(char *text, size_t maxLen, bool isFromTalkerAlias)
{
	char buffer[37]; // Max: TA 27 (in 7bit format) + ' [' + 6 (Maidenhead)  + ']' + NULL

	if (strlen(text) >= 5 && isFromTalkerAlias) // if it's Talker Alias and there is more text than just the callsign, split across 2 lines
	{
		char    *pbuf;
		int32_t  cpos;

		// User prefers to not span the TA info over two lines, check it that could fit
		if ((nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY) ||
				((nonVolatileSettings.splitContact == SPLIT_CONTACT_AUTO) && (strlen(text) <= 16)))
		{
			memcpy(buffer, text, 17);
			buffer[16] = 0;
			ucPrintCentered(32, chomp(buffer), FONT_8x16);
			displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			return;
		}

		if ((cpos = getFirstSpacePos(text)) != -1)
		{
			// Callsign found
			memcpy(buffer, text, cpos);
			buffer[cpos] = 0;
			ucPrintCentered(32, chomp(buffer), FONT_8x16);

			memcpy(buffer, text + (cpos + 1), (maxLen - (cpos + 1)));
			buffer[(strlen(text) - (cpos + 1))] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
				printSplitOrSpanText(48, pbuf);
			else
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
		}
		else
		{
			// No space found, use a chainsaw
			memcpy(buffer, text, 16);
			buffer[16] = 0;

			ucPrintCentered(32, chomp(buffer), FONT_8x16);

			memcpy(buffer, text + 16, (maxLen - 16));
			buffer[(strlen(text) - 16)] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
				printSplitOrSpanText(48, pbuf);
			else
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
		}
	}
	else
	{
		memcpy(buffer, text, 17);
		buffer[16] = 0;
		ucPrintCentered(32, chomp(buffer), FONT_8x16);
		displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
	}
}

void menuUtilityRenderQSOData(void)
{
	menuUtilityReceivedPcId=0;//reset the received PcId

	/*
	 * Note.
	 * When using Brandmeister reflectors. TalkGroups can be used to select reflectors e.g. TG 4009, and TG 5000 to check the connnection
	 * Under these conditions Brandmeister seems to respond with a message via a private call even if the command was sent as a TalkGroup,
	 * and this caused the Private Message acceptance system to operate.
	 * Brandmeister seems respond on the same ID as the keyed TG, so the code
	 * (LinkHead->id & 0xFFFFFF) != (trxTalkGroupOrPcId & 0xFFFFFF)  is designed to stop the Private call system tiggering in these instances
	 *
	 * FYI. Brandmeister seems to respond with a TG value of the users on ID number,
	 * but I thought it was safer to disregard any PC's from IDs the same as the current TG
	 * rather than testing if the TG is the user's ID, though that may work as well.
	 */
	if (HRC6000GetReceivedTgOrPcId() != 0)
	{
		if ((LinkHead->talkGroupOrPcId >> 24) == PC_CALL_FLAG) // &&  (LinkHead->id & 0xFFFFFF) != (trxTalkGroupOrPcId & 0xFFFFFF))
		{
			// Its a Private call
			ucPrintCentered(16, LinkHead->contact, FONT_8x16);
			ucPrintCentered(32, currentLanguage->private_call, FONT_8x16);

			if (LinkHead->talkGroupOrPcId != (trxDMRID | (PC_CALL_FLAG << 24)))
			{
				ucPrintCentered(52, LinkHead->talkgroup, FONT_6x8);
				ucPrintAt(1, 52, "=>", FONT_6x8);
			}
		}
		else
		{
			// Group call
			if (	(LinkHead->talkGroupOrPcId & 0xFFFFFF) != trxTalkGroupOrPcId ||
					(dmrMonitorCapturedTS!=-1 && dmrMonitorCapturedTS != trxGetDMRTimeSlot()) ||
					(trxGetDMRColourCode() != currentChannelData->rxColor))
			{
				// draw the text in inverse video
				ucClearRows(2, 4, true);
				ucPrintCore(0, CONTACT_Y_POS, LinkHead->talkgroup, FONT_8x16, TEXT_ALIGN_CENTER, true);
			}
			else
			{
				ucPrintCentered(CONTACT_Y_POS, LinkHead->talkgroup, FONT_8x16);
			}

			switch (nonVolatileSettings.contactDisplayPriority)
			{
				case CONTACT_DISPLAY_PRIO_CC_DB_TA:
				case CONTACT_DISPLAY_PRIO_DB_CC_TA:
					// No contact found is codeplug and DMRIDs, use TA as fallback, if any.
					if ((strncmp(LinkHead->contact, "ID:", 3) == 0) && (LinkHead->talkerAlias[0] != 0x00))
					{
						if (LinkHead->locator[0] != 0)
						{
							char bufferTA[37]; // TA + ' [' + Maidenhead + ']' + NULL

							memset(bufferTA, 0, sizeof(bufferTA));
							snprintf(bufferTA, 36, "%s [%s]", LinkHead->talkerAlias, LinkHead->locator);
							displayContactTextInfos(bufferTA, sizeof(bufferTA), true);
						}
						else
						{
							displayContactTextInfos(LinkHead->talkerAlias, sizeof(LinkHead->talkerAlias), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
						}
					}
					else
					{
						displayContactTextInfos(LinkHead->contact, sizeof(LinkHead->contact), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
					}
					break;

				case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				case CONTACT_DISPLAY_PRIO_TA_DB_CC:
					// Talker Alias have the priority here
					if (LinkHead->talkerAlias[0] != 0x00)
					{
						if (LinkHead->locator[0] != 0)
						{
							char bufferTA[37]; // TA + ' [' + Maidenhead + ']' + NULL

							memset(bufferTA, 0, sizeof(bufferTA));
							snprintf(bufferTA, 36, "%s [%s]", LinkHead->talkerAlias, LinkHead->locator);
							displayContactTextInfos(bufferTA, sizeof(bufferTA), true);
						}
						else
						{
							displayContactTextInfos(LinkHead->talkerAlias, sizeof(LinkHead->talkerAlias), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
						}
					}
					else // No TA, then use the one extracted from Codeplug or DMRIdDB
					{
						displayContactTextInfos(LinkHead->contact, sizeof(LinkHead->contact), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
					}
					break;
			}
		}
	}
}

void menuUtilityRenderHeader(void)
{
	const int Y_OFFSET = 2;
	static const int bufferLen = 17;
	char buffer[bufferLen];
	static bool scanBlinkPhase = true;
	static uint32_t blinkTime = 0;

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

	if (scanActive &&  menuVFOModeIsScanning())
	{
		int blinkPeriod = 1000;
		if (scanBlinkPhase)
		{
			blinkPeriod = 500;
		}

		if ((fw_millis() - blinkTime) > blinkPeriod)
		{
			blinkTime = fw_millis();
			scanBlinkPhase = !scanBlinkPhase;
		}
	}
	else
	{
		scanBlinkPhase = false;
	}

	switch(trxGetMode())
	{
		case RADIO_MODE_ANALOG:
			strcpy(buffer, "FM");
			if (!trxGetBandwidthIs25kHz())
			{
				strcat(buffer,"N");
			}
			ucPrintCore(0, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, scanBlinkPhase);

			if ((currentChannelData->txTone!=65535)||(currentChannelData->rxTone!=65535))
			{
				int rectWidth = 7;
				strcpy(buffer, "C");
				if (currentChannelData->txTone!=65535)
				{
					rectWidth += 6;
					strcat(buffer, "T");
				}
				if (currentChannelData->rxTone!=65535)
				{
					rectWidth += 6;
					strcat(buffer, "R");
				}
				bool isInverted = (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE);
				if (isInverted)
				{
					ucFillRect(23, Y_OFFSET - 1, rectWidth, 9, false);
				}
				ucPrintCore(24, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, isInverted);
			}
			break;

		case RADIO_MODE_DIGITAL:
			if (settingsUsbMode != USB_MODE_HOTSPOT)
			{
				const int DMR_TEXT_X_OFFSET = 1;
//				(trxGetMode() == RADIO_MODE_DIGITAL && settingsPrivateCallMuteMode == true)?" MUTE":"");// The location that this was displayed is now used for the power level
				if (!scanBlinkPhase && (nonVolatileSettings.dmrFilterLevel > DMR_FILTER_CC_TS))
				{
					ucFillRect(0, Y_OFFSET - 1, 20, 9, false);
				}
				if (!scanBlinkPhase)
				{
					bool isInverted = scanBlinkPhase ^ (nonVolatileSettings.dmrFilterLevel > DMR_FILTER_CC_TS);
					ucPrintCore(DMR_TEXT_X_OFFSET, Y_OFFSET, "DMR", ((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) ? FONT_6x8_BOLD : FONT_6x8), TEXT_ALIGN_LEFT, isInverted);
				}

				snprintf(buffer, bufferLen, "%s%d", currentLanguage->ts, trxGetDMRTimeSlot() + 1);
				buffer[bufferLen - 1] = 0;
				if (nonVolatileSettings.dmrFilterLevel < DMR_FILTER_CC_TS)
				{
					ucFillRect(20, Y_OFFSET - 1, 21, 9, false);
					ucPrintCore(22, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, true);
				}
				else
				{
					ucPrintCore(22, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, false);
//					if (nonVolatileSettings.tsManualOverride != 0)
//					{
//						ucFillRect(34, Y_OFFSET, 7, 8, false);
//						snprintf(buffer, bufferLen, "%d", trxGetDMRTimeSlot() + 1);
//						ucPrintCore(35, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, true);
//					}
				}
			}
			break;
	}

	/* NO ROOM TO DISPLAY THIS
	if (keypadLocked)
	{
		strcat(buffer," L");
	}*/

	ucPrintCentered(Y_OFFSET, (char *)POWER_LEVELS[nonVolatileSettings.txPowerLevel], FONT_6x8);


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
		snprintf(buffer, bufferLen, "%d%%", batteryPerentage);
		buffer[bufferLen - 1] = 0;
		ucPrintCore(0, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_RIGHT, false);// Display battery percentage at the right
	}
	else
	{
		const int COLOR_CODE_X_POSITION = 84;
		int ccode = trxGetDMRColourCode();
		snprintf(buffer, bufferLen, "C%d", ccode);
		if (nonVolatileSettings.dmrFilterLevel == DMR_FILTER_NONE )
		{
			ucFillRect(COLOR_CODE_X_POSITION - 1, Y_OFFSET - 1,13 + ((ccode > 9)*6) ,9,false);
		}

		ucPrintCore(COLOR_CODE_X_POSITION, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_LEFT, nonVolatileSettings.dmrFilterLevel == DMR_FILTER_NONE);

		snprintf(buffer, bufferLen, "%d%%", batteryPerentage);
		buffer[bufferLen - 1] = 0;
		ucPrintCore(0, Y_OFFSET, buffer, FONT_6x8, TEXT_ALIGN_RIGHT, false);// Display battery percentage at the right
	}

}

void drawRSSIBarGraph(void)
{
	int dBm,barGraphLength;

	ucFillRect(0, BAR_Y_POS,128,4,true);

	if (trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
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
	ucFillRect(0, BAR_Y_POS,barGraphLength,4,false);
	trxRxSignal=0;
}

void drawDMRMicLevelBarGraph(void)
{
	float barGraphLength = sqrt(micAudioSamplesTotal)*1.5;

	ucFillRect(0, BAR_Y_POS,128,3,true);

	if (barGraphLength > 127)
	{
		barGraphLength = 127;
	}

	ucFillRect(0, BAR_Y_POS,(int)barGraphLength,3,false);
}

void setOverrideTGorPC(int tgOrPc, bool privateCall) {
	int tmpTGorPC = nonVolatileSettings.overrideTG;

	menuUtilityTgBeforePcMode = 0;
	nonVolatileSettings.overrideTG = tgOrPc;
	if (privateCall == true)
	{
		// Private Call

		if ((tmpTGorPC >> 24) != PC_CALL_FLAG)
		{
			// if the current Tx TG is a TalkGroup then save it so it can be restored after the end of the private call
			menuUtilityTgBeforePcMode = tmpTGorPC;
		}
		nonVolatileSettings.overrideTG |= (PC_CALL_FLAG << 24);
	}
}

void printToneAndSquelch(void)
{
	char buf[24];
	if (trxGetMode() == RADIO_MODE_ANALOG)
	{
		if (currentChannelData->rxTone == TRX_CTCSS_TONE_NONE)
		{
			snprintf(buf, 24, "CTCSS:%s|", currentLanguage->none);
			buf[23] = 0;
		}
		else
		{
			snprintf(buf, 24, "CTCSS:%d.%dHz|", currentChannelData->rxTone / 10 , currentChannelData->rxTone % 10);
		}

		if (currentChannelData->txTone == TRX_CTCSS_TONE_NONE)
		{
			snprintf(buf, 24, "%s%s", buf, currentLanguage->none);
			buf[23] = 0;
		}
		else
		{
			snprintf(buf, 24, "%s%d.%dHz", buf, currentChannelData->txTone / 10 , currentChannelData->txTone % 10);
		}
		ucPrintCentered(16, buf, FONT_6x8);

		snprintf(buf, 24, "SQL:%d%%", 5*(((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql)-1));
		ucPrintCentered(24 + 1, buf, FONT_6x8);
	}
}

void printFrequency(bool isTX, bool hasFocus, uint8_t y, uint32_t frequency, bool displayVFOChannel, bool isScanMode)
{
	static const int bufferLen = 17;
	char buffer[bufferLen];
	int val_before_dp = frequency / 100000;
	int val_after_dp = frequency - val_before_dp * 100000;

	// Focus + direction
	snprintf(buffer, bufferLen, "%c%c", ((hasFocus && !isScanMode)? '>' : ' '), (isTX ? 'T' : 'R'));
	ucPrintAt(0, y, buffer, FONT_8x16);
	// VFO
	if (displayVFOChannel)
	{
		ucPrintAt(16, y + 8, (nonVolatileSettings.currentVFONumber == 0) ? "A" : "B", FONT_8x8);
	}
	// Frequency
	snprintf(buffer, bufferLen, "%d.%05d", val_before_dp, val_after_dp);
	buffer[bufferLen - 1] = 0;
	ucPrintAt(FREQUENCY_X_POS, y, buffer, FONT_8x16);
	// Unit
	ucPrintAt(128 - (3 * 8), y, "MHz", FONT_8x16);
}

int snprintRxFreqAndOffset(char *s, size_t n, int rxFreq, int txFreq)
{
	int rxMHz, rxDec, width, len;
	int off, offMHz, offDec;
	char sign = '+';

	// Split MHz and decimal parts
	rxMHz = rxFreq / 100000;
	rxDec = rxFreq - (rxMHz * 100000);

	// Trim trailing zeros
	// width of the decimal part
	width = 5;
	while (rxDec > 0 && rxDec % 10 == 0)
	{
		rxDec = rxDec / 10;
		// Each time we remove a 0 off the end, decrement width
		width--;
	}
	// If no decimal part, just print .0
	if (rxDec == 0) width = 1;
	len = snprintf(s, n, "%d.%0*d", rxMHz, width, rxDec);

	// Add the offset, if there is one
	// This should be correct in cross-band operation, but not as useful
	if (txFreq != rxFreq)
	{
		off = txFreq - rxFreq;
		offMHz = off / 100000;
		offDec = abs(off - (offMHz * 100000));
		width = 5;
		while (offDec > 0 && offDec % 10 == 0)
		{
			width--;
			offDec = offDec / 10;
		}
		if (offDec == 0) width = 1;
		if (off < 0) sign = '-';
		len += snprintf(s+len, n-len, " %c%d.%0*d", sign, abs(offMHz), width, offDec);
	}
	return len;
}

void reset_freq_enter_digits(void)
{
	for (int i=0;i<12;i++)
	{
		freq_enter_digits[i]='-';
	}
	freq_enter_idx = 0;
}

int read_freq_enter_digits(int startDigit, int endDigit)
{
	int result=0;
	for (int i=startDigit;i<endDigit;i++)
	{
		result=result*10;
		if ((freq_enter_digits[i]>='0') && (freq_enter_digits[i]<='9'))
		{
			result=result+freq_enter_digits[i]-'0';
		}
	}
	return result;
}
