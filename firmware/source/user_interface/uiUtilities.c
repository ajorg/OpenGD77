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



#if defined(PLATFORM_RD5R)
static const int BAR_Y_POS = 8;
#else
static const int BAR_Y_POS = 10;
#endif


static const int DMRID_MEMORY_STORAGE_START = 0x30000;
static const int DMRID_HEADER_LENGTH = 0x0C;
__attribute__((section(".data.$RAM4"))) LinkItem_t callsList[NUM_LASTHEARD_STORED];
LinkItem_t *LinkHead = callsList;
int numLastHeard=0;
int menuDisplayQSODataState = QSO_DISPLAY_DEFAULT_SCREEN;
int qsodata_timer;
const uint32_t RSSI_UPDATE_COUNTER_RELOAD = 100;

uint32_t menuUtilityReceivedPcId 	= 0;// No current Private call awaiting acceptance
uint32_t menuUtilityTgBeforePcMode 	= 0;// No TG saved, prior to a Private call being accepted.

const char *POWER_LEVELS[]         = { "50","250","500","750","1","2","3","4","5","5"};
const char *POWER_LEVEL_UNITS[]    = { "mW","mW","mW","mW","W","W","W","W","W","W++"};
const char *DMR_DESTINATION_FILTER_LEVELS[]   = {"TG","Ct","RxG"};
const char *ANALOG_FILTER_LEVELS[] = {"CTCSS|DCS"};

volatile uint32_t lastID = 0;// This needs to be volatile as lastHeardClearLastID() is called from an ISR
uint32_t lastTG = 0;

static dmrIDsCache_t dmrIDsCache;

int nuisanceDelete[MAX_ZONE_SCAN_NUISANCE_CHANNELS];
int nuisanceDeleteIndex;
int scanTimer = 0;
bool scanActive = false;
ScanState_t scanState = SCAN_SCANNING;		//state flag for scan routine.
int scanDirection = 1;

bool displaySquelch = false;

char freq_enter_digits[12] = { '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-' };
int freq_enter_idx;

int tmpQuickMenuDmrDestinationFilterLevel;
int tmpQuickMenuDmrCcTsFilterLevel;
int tmpQuickMenuAnalogFilterLevel;

__attribute__((section(".data.$RAM4"))) struct_codeplugRxGroup_t currentRxGroupData;
struct_codeplugContact_t currentContactData;

const int SCAN_SHORT_PAUSE_TIME = 500;			//time to wait after carrier detected to allow time for full signal detection. (CTCSS or DMR)
const int SCAN_TOTAL_INTERVAL = 30;			    //time between each scan step
const int SCAN_DMR_SIMPLEX_MIN_INTERVAL = 60;	//minimum time between steps when scanning DMR Simplex. (needs extra time to capture TDMA Pulsing)
const int SCAN_FREQ_CHANGE_SETTLING_INTERVAL = 1;//Time after frequency is changed before RSSI sampling starts
const int SCAN_SKIP_CHANNEL_INTERVAL = 1;		//This is actually just an implicit flag value to indicate the channel should be skipped

voicePromptItem_t voicePromptSequenceState = PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ;
struct_codeplugZone_t currentZone;
static bool voicePromptWasPlaying;
bool inhibitInitialVoicePrompt =
#if defined(PLATFORM_GD77S)
		true;
#else
		false;//Used to indicate whether the voice prompts should be reloaded with the channel name or VFO freq
#endif

// Set TS manual override
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// ts: 1, 2, TS_NO_OVERRIDE
void tsSetOverride(Channel_t chan, int8_t ts)
{
	uint8_t tsOverride = nonVolatileSettings.tsManualOverride;

	// Clear TS override for given channel
	tsOverride &= ~(0x03 << (2 * ((int8_t)chan)));
	if (ts != TS_NO_OVERRIDE)
	{
		// Set TS override for given channel
		tsOverride |= (ts << (2 * ((int8_t)chan)));
	}

	settingsSet(nonVolatileSettings.tsManualOverride, tsOverride);
}

// Set TS manual override
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// contact: apply TS override from contact setting
void tsSetContactOverride(Channel_t chan, struct_codeplugContact_t *contact)
{
	if ((contact->reserve1 & 0x01) == 0x00)
	{
		tsSetOverride(chan, (((contact->reserve1 & 0x02) >> 1) + 1));
	}
	else
	{
		tsSetOverride(chan, TS_NO_OVERRIDE);
	}
}

// Get TS override value
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// returns (TS + 1, 0 no override)
int8_t tsGetOverride(Channel_t chan)
{
	return (nonVolatileSettings.tsManualOverride >> (2 * (int8_t)chan)) & 0x03;
}

// Check if TS is overrode
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// returns true on overrode for the specified channel
bool tsIsOverridden(Channel_t chan)
{
	return (nonVolatileSettings.tsManualOverride & (0x03 << (2 * ((int8_t)chan))));
}

bool isQSODataAvailableForCurrentTalker(void)
{
	LinkItem_t *item = NULL;
	uint32_t rxID = HRC6000GetReceivedSrcId();

	// We're in digital mode, RXing, and current talker is already at the top of last heard list,
	// hence immediately display complete contact/TG info on screen
	if ((trxTransmissionEnabled == false) && ((trxGetMode() == RADIO_MODE_DIGITAL) && (rxID != 0) && (HRC6000GetReceivedTgOrPcId() != 0)) &&
			(getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
			&& checkTalkGroupFilter() &&
			(((item = lastheardFindInList(rxID)) != NULL) && (item == LinkHead)))
	{
		return true;
	}

	return false;
}

/*
 * Remove space at the end of the array, and return pointer to first non space character
 */
char *chomp(char *str)
{
	char *sp = str, *ep = str;

	while (*ep != '\0')
	{
		ep++;
	}

	// Spaces at the end
	while (ep > str)
	{
		if (*ep == '\0')
		{
		}
		else if (*ep == ' ')
		{
			*ep = '\0';
		}
		else
		{
			break;
		}

		ep--;
	}

	// Spaces at the beginning
	while (*sp == ' ')
	{
		sp++;
	}

	return sp;
}

int32_t getFirstSpacePos(char *str)
{
	char *p = str;

	while(*p != '\0')
	{
		if (*p == ' ')
		{
			return (p - str);
		}

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
						{
							buffer[t2++] = c & 0x7FU;
						}

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
				{
					buffer[t2++] = talkerAlias[2U * i + 2U];
				}
				else
				{
					buffer[t2++] = '?';
				}
			}
			buffer[TAsize] = 0;
			break;
    }

	return &buffer[0];
}

void lastHeardClearLastID(void)
{
	lastID = 0;
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
			snprintf(item->talkgroup, 16, "%s %ld", currentLanguage->tg, (item->talkGroupOrPcId & 0x00FFFFFF));
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

					if (numLastHeard < NUM_LASTHEARD_STORED)
					{
						numLastHeard++;
					}

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

	if ((headerBuf[0] != 'I') || (headerBuf[1] != 'D') || (headerBuf[2] != '-'))
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
	if (menuSystemGetCurrentMenuNumber() == UI_CHANNEL_MODE)
	{
		codeplugUtilConvertBufToString(currentChannelData->name, buffer, 16);
	}
	else
	{
		int val_before_dp = currentChannelData->rxFreq / 100000;
		int val_after_dp = currentChannelData->rxFreq - val_before_dp * 100000;
		snprintf(buffer, maxLen, "%d.%05d MHz", val_before_dp, val_after_dp);
		buffer[maxLen - 1] = 0;
	}

#if defined(PLATFORM_RD5R)
	ucPrintCentered(41, buffer, FONT_SIZE_1);
#else
	ucPrintCentered(52, buffer, FONT_SIZE_1);
#endif
}

static void printSplitOrSpanText(uint8_t y, char *text)
{
	if (text != NULL)
	{
		uint8_t len = strlen(text);

		if (len == 0)
		{
			return;
		}
		else if (len <= 16)
		{
			ucPrintCentered(y, text, FONT_SIZE_3);
		}
		else
		{
			uint8_t nLines = len / 21 + (((len % 21) != 0) ? 1 : 0);

			if (nLines > 2)
			{
				nLines = 2;
				len = 42; // 2 lines max.
			}

			if (nLines > 1)
			{
				char buffer[43]; // 2 * 21 chars + NULL

				memcpy(buffer, text, len + 1);

				char *p = buffer + 20;

				// Find a space backward
				while ((*p != ' ') && (p > buffer))
				{
					p--;
				}

				uint8_t rest = (uint8_t)((buffer + strlen(buffer)) - p) - ((*p == ' ') ? 1 : 0);

				// rest is too long, just split the line in two chunks
				if (rest > 21)
				{
					char c = buffer[21];

					buffer[21] = 0;

					ucPrintCentered(y, chomp(buffer), FONT_SIZE_1); // 2 pixels are saved, could center

					buffer[21] = c;
					buffer[42] = 0;

					ucPrintCentered(y + 8, chomp(buffer + 21), FONT_SIZE_1);
				}
				else
				{
					*p = 0;

					ucPrintCentered(y, chomp(buffer), FONT_SIZE_1);
					ucPrintCentered(y + 8, chomp(p + 1), FONT_SIZE_1);
				}
			}
			else // One line of 21 chars max
			{
				ucPrintCentered(y + 4, text, FONT_SIZE_1);
			}
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

			ucPrintCentered(CONTACT_FIRST_LINE_Y_POS, chomp(buffer), FONT_SIZE_3);
			displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			return;
		}

		if ((cpos = getFirstSpacePos(text)) != -1)
		{
			// Callsign found
			memcpy(buffer, text, cpos);
			buffer[cpos] = 0;

			ucPrintCentered(CONTACT_FIRST_LINE_Y_POS, chomp(buffer), FONT_SIZE_3);

			memcpy(buffer, text + (cpos + 1), (maxLen - (cpos + 1)));
			buffer[(strlen(text) - (cpos + 1))] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
			{
				printSplitOrSpanText(CONTACT_SECOND_LINE_Y_POS, pbuf);
			}
			else
			{
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			}
		}
		else
		{
			// No space found, use a chainsaw
			memcpy(buffer, text, 16);
			buffer[16] = 0;

			ucPrintCentered(CONTACT_FIRST_LINE_Y_POS, chomp(buffer), FONT_SIZE_3);

			memcpy(buffer, text + 16, (maxLen - 16));
			buffer[(strlen(text) - 16)] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
			{
				printSplitOrSpanText(CONTACT_SECOND_LINE_Y_POS, pbuf);
			}
			else
			{
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			}
		}
	}
	else
	{
		memcpy(buffer, text, 17);
		buffer[16] = 0;

#if defined(PLATFORM_RD5R)
		ucPrintCentered(24, chomp(buffer), FONT_SIZE_3);
#else
		ucPrintCentered(32, chomp(buffer), FONT_SIZE_3);
#endif
		displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
	}
}

void menuUtilityRenderQSOData(void)
{
	menuUtilityReceivedPcId = 0;//reset the received PcId

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
			ucPrintCentered(16, LinkHead->contact, FONT_SIZE_3);

			ucPrintCentered((DISPLAY_SIZE_Y / 2), currentLanguage->private_call, FONT_SIZE_3);

			if (LinkHead->talkGroupOrPcId != (trxDMRID | (PC_CALL_FLAG << 24)))
			{
#if defined(PLATFORM_RD5R)
				ucPrintCentered(41, LinkHead->talkgroup, FONT_SIZE_1);
				ucPrintAt(1, 41, "=>", FONT_SIZE_1);
#else
				ucPrintCentered(52, LinkHead->talkgroup, FONT_SIZE_1);
				ucPrintAt(1, 52, "=>", FONT_SIZE_1);
#endif
			}
		}
		else
		{
			// Group call
			if (((LinkHead->talkGroupOrPcId & 0xFFFFFF) != trxTalkGroupOrPcId )||
					((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot())) ||
					(trxGetDMRColourCode() != currentChannelData->txColor))
			{
#if defined(PLATFORM_RD5R)
				// draw the text in inverse video
				ucFillRect(0, CONTACT_Y_POS + 1, DISPLAY_SIZE_X, 10, false);
				ucPrintCore(0, CONTACT_Y_POS + 2, LinkHead->talkgroup, FONT_SIZE_3, TEXT_ALIGN_CENTER, true);
#else
				// draw the text in inverse video
				ucClearRows(2, 4, true);
				ucPrintCore(0, CONTACT_Y_POS, LinkHead->talkgroup, FONT_SIZE_3, TEXT_ALIGN_CENTER, true);
#endif
			}
			else
			{
#if defined(PLATFORM_RD5R)
				ucPrintCentered(CONTACT_Y_POS + 2, LinkHead->talkgroup, FONT_SIZE_3);
#else
				ucPrintCentered(CONTACT_Y_POS, LinkHead->talkgroup, FONT_SIZE_3);
#endif
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
#if defined(PLATFORM_RD5R)
	const int Y_OFFSET = 0;
#else
	const int Y_OFFSET = 2;
#endif
	static const int bufferLen = 17;
	char buffer[bufferLen];
	static bool scanBlinkPhase = true;
	static uint32_t blinkTime = 0;

	if (!trxTransmissionEnabled)
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

	if (scanActive && uiVFOModeIsScanning())
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

	const int MODE_TEXT_X_OFFSET = 1;
	const int FILTER_TEXT_X_OFFSET = 25;
	switch(trxGetMode())
	{
		case RADIO_MODE_ANALOG:
			strcpy(buffer, "FM");
			if (!trxGetBandwidthIs25kHz())
			{
				strcat(buffer,"N");
			}
			ucPrintCore(MODE_TEXT_X_OFFSET, Y_OFFSET, buffer, ((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, scanBlinkPhase);

			if ((currentChannelData->txTone != CODEPLUG_CSS_NONE) || (currentChannelData->rxTone != CODEPLUG_CSS_NONE))
			{
				int rectWidth = 9;
				if (codeplugChannelToneIsDCS(currentChannelData->txTone))
				{
					strcpy(buffer, "D");
				}
				else
				{
					strcpy(buffer, "C");
				}
				if (currentChannelData->txTone != CODEPLUG_CSS_NONE)
				{
					rectWidth += 6;
					strcat(buffer, "T");
				}
				if (currentChannelData->rxTone != CODEPLUG_CSS_NONE)
				{
					rectWidth += 6;
					strcat(buffer, "R");
				}

				bool isInverted = (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE);
				if (isInverted)
				{
					ucFillRect(FILTER_TEXT_X_OFFSET - 2, Y_OFFSET - 1, rectWidth, 9, false);
				}
				ucPrintCore(FILTER_TEXT_X_OFFSET, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isInverted);
			}
			break;

		case RADIO_MODE_DIGITAL:
			if (settingsUsbMode != USB_MODE_HOTSPOT)
			{
//				(trxGetMode() == RADIO_MODE_DIGITAL && settingsPrivateCallMuteMode == true)?" MUTE":"");// The location that this was displayed is now used for the power level
				if (!scanBlinkPhase && (nonVolatileSettings.dmrDestinationFilter > DMR_DESTINATION_FILTER_NONE))
				{
					ucFillRect(0, Y_OFFSET - 1, 20, 9, false);
				}
				if (!scanBlinkPhase)
				{
					bool isInverted = scanBlinkPhase ^ (nonVolatileSettings.dmrDestinationFilter > DMR_DESTINATION_FILTER_NONE);
					ucPrintCore(MODE_TEXT_X_OFFSET, Y_OFFSET, "DMR", ((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, isInverted);
				}

				snprintf(buffer, bufferLen, "%s%d", currentLanguage->ts, trxGetDMRTimeSlot() + 1);
				buffer[bufferLen - 1] = 0;
				if (!(nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN))
				{
					ucFillRect(FILTER_TEXT_X_OFFSET - 2, Y_OFFSET - 1, 21, 9, false);
					ucPrintCore(FILTER_TEXT_X_OFFSET, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
				}
				else
				{
					ucPrintCore(FILTER_TEXT_X_OFFSET, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
//					if (nonVolatileSettings.tsManualOverride != 0)
//					{
//						ucFillRect(34, Y_OFFSET, 7, 8, false);
//						snprintf(buffer, bufferLen, "%d", trxGetDMRTimeSlot() + 1);
//						ucPrintCore(35, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
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

	sprintf(buffer,"%s%s", POWER_LEVELS[nonVolatileSettings.txPowerLevel], POWER_LEVEL_UNITS[nonVolatileSettings.txPowerLevel]);
	ucPrintCentered(Y_OFFSET,buffer, FONT_SIZE_1);

	if ((settingsUsbMode == USB_MODE_HOTSPOT) || (trxGetMode() == RADIO_MODE_ANALOG))
	{
		// In hotspot mode the CC is show as part of the rest of the display and in Analogue mode the CC is meaningless
		snprintf(buffer, bufferLen, "%d%%", getBatteryPercentage());
		buffer[bufferLen - 1] = 0;
		ucPrintCore(0, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);// Display battery percentage at the right
	}
	else
	{
		const int COLOR_CODE_X_POSITION = 84;
		int ccode = trxGetDMRColourCode();
		snprintf(buffer, bufferLen, "C%d", ccode);
		bool isNotFilteringCC = !(nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN);
		if (isNotFilteringCC)
		{
			ucFillRect(COLOR_CODE_X_POSITION - 1, Y_OFFSET - 1,13 + ((ccode > 9)*6), 9, false);
		}

		ucPrintCore(COLOR_CODE_X_POSITION, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isNotFilteringCC);

		snprintf(buffer, bufferLen, "%d%%", getBatteryPercentage());
		buffer[bufferLen - 1] = 0;
		ucPrintCore(0, Y_OFFSET, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);// Display battery percentage at the right
	}

}

void drawRSSIBarGraph(void)
{
	int dBm,barGraphLength;

	ucFillRect(0, BAR_Y_POS, DISPLAY_SIZE_X, 4, true);

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
	if (barGraphLength < 0)
	{
		barGraphLength = 0;
	}

	if (barGraphLength > 123)
	{
		barGraphLength = 123;
	}
	ucFillRect(0, BAR_Y_POS, barGraphLength, 4, false);
	trxRxSignal = 0;
}

void drawFMMicLevelBarGraph(void)
{
	trxReadVoxAndMicStrength();

	uint8_t micdB = (trxTxMic >> 1); // trxTxMic is in 0.5dB unit, displaying 50dB .. 100dB
	uint16_t w = 0;

	// display from 50dB to 100dB, span over 128pix
	w = (uint16_t)(((float)DISPLAY_SIZE_X / 50.0) * ((float)micdB - 50.0));

	ucFillRect(0, BAR_Y_POS, DISPLAY_SIZE_X, 3, true);
	ucFillRect(0, BAR_Y_POS, (int16_t)((w > (DISPLAY_SIZE_X - 1)) ? (DISPLAY_SIZE_X - 1) : w), 3, false);
}

void drawDMRMicLevelBarGraph(void)
{
	uint16_t barGraphLength = (uint16_t)(sqrt(micAudioSamplesTotal) * 1.5);

	ucFillRect(0, BAR_Y_POS, DISPLAY_SIZE_X, 3, true);
	ucFillRect(0, BAR_Y_POS, (int16_t)((barGraphLength > (DISPLAY_SIZE_X - 1)) ? (DISPLAY_SIZE_X - 1) : barGraphLength), 3, false);
}

void setOverrideTGorPC(int tgOrPc, bool privateCall)
{
	menuUtilityTgBeforePcMode = 0;
	settingsSet(nonVolatileSettings.overrideTG, tgOrPc);
	if (privateCall == true)
	{
		// Private Call

		if ((trxTalkGroupOrPcId >> 24) != PC_CALL_FLAG)
		{
			// if the current Tx TG is a TalkGroup then save it so it can be restored after the end of the private call
			menuUtilityTgBeforePcMode = trxTalkGroupOrPcId;
		}
		settingsSet(nonVolatileSettings.overrideTG, (nonVolatileSettings.overrideTG | (PC_CALL_FLAG << 24)));
	}
}

void printToneAndSquelch(void)
{
	char buf[24];
	int pos = 0;
	if (trxGetMode() == RADIO_MODE_ANALOG)
	{
		pos += snprintf(buf + pos, 24 - pos, "Rx:");
		if (codeplugChannelToneIsCTCSS(currentChannelData->rxTone))
		{
			pos += snprintf(buf + pos, 24 - pos, "%d.%dHz", currentChannelData->rxTone / 10 , currentChannelData->rxTone % 10);
		}
		else if (codeplugChannelToneIsDCS(currentChannelData->rxTone))
		{
			pos += snprintDCS(buf + pos, 24 - pos, currentChannelData->rxTone & 0777, (currentChannelData->rxTone & CODEPLUG_DCS_INVERTED_MASK));
		}
		else
		{
			pos += snprintf(buf + pos, 24 - pos, "%s", currentLanguage->none);
		}
		pos += snprintf(buf + pos, 24 - pos, "|Tx:");

		if (codeplugChannelToneIsCTCSS(currentChannelData->txTone))
		{
			pos += snprintf(buf + pos, 24 - pos, "%d.%dHz", currentChannelData->txTone / 10 , currentChannelData->txTone % 10);
		}
		else if (codeplugChannelToneIsDCS(currentChannelData->txTone))
		{
			pos += snprintDCS(buf + pos, 24 - pos, currentChannelData->txTone & 0777, (currentChannelData->txTone & CODEPLUG_DCS_INVERTED_MASK));
		}
		else
		{
			pos += snprintf(buf + pos, 24 - pos, "%s", currentLanguage->none);
		}

#if defined(PLATFORM_RD5R)
		ucPrintCentered(13, buf, FONT_SIZE_1);
		snprintf(buf, 24, "SQL:%d%%", 5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql)-1));
		ucPrintCentered(21 + 1, buf, FONT_SIZE_1);
#else
		ucPrintCentered(16, buf, FONT_SIZE_1);

		snprintf(buf, 24, "SQL:%d%%", 5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql)-1));
		ucPrintCentered(24 + 1, buf, FONT_SIZE_1);
#endif

	}
}

void printFrequency(bool isTX, bool hasFocus, uint8_t y, uint32_t frequency, bool displayVFOChannel, bool isScanMode)
{
#if defined(PLATFORM_RD5R)
	const int VFO_LETTER_Y_OFFSET = 0;// This is the different in height of the SIZE_1 and SIZE_3 fonts
#else
	const int VFO_LETTER_Y_OFFSET = 8;// This is the different in height of the SIZE_1 and SIZE_3 fonts
#endif
	static const int bufferLen = 17;
	char buffer[bufferLen];
	int val_before_dp = frequency / 100000;
	int val_after_dp = frequency - val_before_dp * 100000;

	// Focus + direction
	snprintf(buffer, bufferLen, "%c%c", ((hasFocus && !isScanMode)? '>' : ' '), (isTX ? 'T' : 'R'));

	ucPrintAt(0, y, buffer, FONT_SIZE_3);
	// VFO
	if (displayVFOChannel)
	{
		ucPrintAt(16, y + VFO_LETTER_Y_OFFSET, (nonVolatileSettings.currentVFONumber == 0) ? "A" : "B", FONT_SIZE_1);
	}
	// Frequency
	snprintf(buffer, bufferLen, "%d.%05d", val_before_dp, val_after_dp);
	buffer[bufferLen - 1] = 0;
	ucPrintAt(FREQUENCY_X_POS, y, buffer, FONT_SIZE_3);
	ucPrintAt(DISPLAY_SIZE_X - (3 * 8), y, "MHz", FONT_SIZE_3);
}

size_t snprintDCS(char *s, size_t n, uint16_t code, bool inverted)
{
	return snprintf(s, n, "D%03o%c", code, (inverted ? 'I' : 'N'));
}

void reset_freq_enter_digits(void)
{
	for (int i = 0; i < 12; i++)
	{
		freq_enter_digits[i] = '-';
	}
	freq_enter_idx = 0;
}

int read_freq_enter_digits(int startDigit, int endDigit)
{
	int result = 0;
	for (int i = startDigit; i < endDigit; i++)
	{
		result = result * 10;
		if ((freq_enter_digits[i] >= '0') && (freq_enter_digits[i] <= '9'))
		{
			result = result + freq_enter_digits[i] - '0';
		}
	}
	return result;
}

int getBatteryPercentage(void)
{
	int  batteryPerentage = (int)(((averageBatteryVoltage - CUTOFF_VOLTAGE_UPPER_HYST) * 100) / (BATTERY_MAX_VOLTAGE - CUTOFF_VOLTAGE_UPPER_HYST));

	if (batteryPerentage > 100)
	{
		batteryPerentage = 100;
	}

	if (batteryPerentage < 0)
	{
		batteryPerentage = 0;
	}

	return batteryPerentage;
}

void increasePowerLevel(void)
{
	settingsIncrement(nonVolatileSettings.txPowerLevel, 1);
	trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
	announceItem(PROMPT_SEQUENCE_POWER, PROMPT_THRESHOLD_3);
}

void decreasePowerLevel(void)
{
	settingsDecrement(nonVolatileSettings.txPowerLevel, 1);
	trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
	announceItem(PROMPT_SEQUENCE_POWER, PROMPT_THRESHOLD_3);
}

void announceRadioMode(void)
{
	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(&currentLanguage->mode);
	}
	voicePromptsAppendString( (trxGetMode() == RADIO_MODE_DIGITAL) ? "DMR" : "FM");
}

void announceZoneName(void)
{
	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(&currentLanguage->zone);
	}
	voicePromptsAppendString(currentZone.name);
}

void announceContactNameTgOrPc(void)
{
	if (nonVolatileSettings.overrideTG == 0)
	{
		voicePromptsAppendLanguageString(&currentLanguage->contact);
		voicePromptsAppendString(currentContactData.name);
	}
	else
	{
		char buf[17];
		itoa(nonVolatileSettings.overrideTG & 0xFFFFFF, buf, 10);
		if ((nonVolatileSettings.overrideTG >> 24) == PC_CALL_FLAG)
		{
			voicePromptsAppendLanguageString(&currentLanguage->private_call);
			voicePromptsAppendString("ID");
		}
		else
		{
			voicePromptsAppendPrompt(PROMPT_TALKGROUP);
		}
		voicePromptsAppendString(buf);
	}
}

void announcePowerLevel(void)
{
	voicePromptsAppendString((char *)POWER_LEVELS[nonVolatileSettings.txPowerLevel]);
	switch(nonVolatileSettings.txPowerLevel)
	{
		case 0://50mW
		case 1://250mW
		case 2://500mW
		case 3://750mW
			voicePromptsAppendPrompt(PROMPT_MILLIWATTS);
			break;
		case 4://1W
			voicePromptsAppendPrompt(PROMPT_WATT);
			break;
		default:
			voicePromptsAppendPrompt(PROMPT_WATTS);
			break;
	}

	// When in 5W++ mode
	if (nonVolatileSettings.txPowerLevel == 9)
	{
		voicePromptsAppendPrompt(PROMPT_PLUS);
		voicePromptsAppendPrompt(PROMPT_PLUS);
	}
}

void announceBatteryPercentage(void)
{
	voicePromptsAppendLanguageString(&currentLanguage->battery);
	voicePromptsAppendInteger(getBatteryPercentage());
	voicePromptsAppendPrompt(PROMPT_PERCENT);
}

void announceTS(void)
{
	voicePromptsAppendPrompt(PROMPT_TIMESLOT);
	voicePromptsAppendInteger(trxGetDMRTimeSlot() + 1);
}

void announceCC(void)
{
	voicePromptsAppendLanguageString(&currentLanguage->colour_code);
	voicePromptsAppendInteger(trxGetDMRColourCode());
}

void announceChannelName(void)
{
	char voiceBuf[17];
	codeplugUtilConvertBufToString(channelScreenChannelData.name, voiceBuf, 16);

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendPrompt(PROMPT_CHANNEL);
	}

	voicePromptsAppendString(voiceBuf);
}

static void removeUnnecessaryZerosFromVoicePrompts(char *str)
{
	const int NUM_DECIMAL_PLACES = 1;
	int len = strlen(str);
	for(int i = len; i > 2; i--)
	{
		if ((str[i - 1] != '0') || (str[i - (NUM_DECIMAL_PLACES + 1)] == '.'))
		{
			str[i] = 0;
			return;
		}
	}
}

void announceFrequency(void)
{
	char buffer[17];

	if (currentChannelData->txFreq != currentChannelData->rxFreq)
	{
		voicePromptsAppendPrompt(PROMPT_RECEIVE);
	}
	int val_before_dp = currentChannelData->rxFreq / 100000;
	int val_after_dp = currentChannelData->rxFreq - val_before_dp * 100000;
	snprintf(buffer, 17, "%d.%05d", val_before_dp, val_after_dp);
	removeUnnecessaryZerosFromVoicePrompts(buffer);
	voicePromptsAppendString(buffer);

	if (currentChannelData->txFreq != currentChannelData->rxFreq)
	{
		voicePromptsAppendPrompt(PROMPT_TRANSMIT);
		val_before_dp = currentChannelData->txFreq / 100000;
		val_after_dp = currentChannelData->txFreq - val_before_dp * 100000;
		snprintf(buffer, 17, "%d.%05d", val_before_dp, val_after_dp);
		removeUnnecessaryZerosFromVoicePrompts(buffer);
		voicePromptsAppendString(buffer);
	}
}

void announceVFOAndFrequency(void)
{
	voicePromptsAppendPrompt(PROMPT_VFO);
	voicePromptsAppendString((nonVolatileSettings.currentVFONumber == 0) ? "A" : "B");
	announceFrequency();
}

void playNextSettingSequence(void)
{
	voicePromptSequenceState++;

	if (voicePromptSequenceState == NUM_PROMPT_SEQUENCES)
	{
		voicePromptSequenceState = 0;
	}

	announceItem(voicePromptSequenceState, PROMPT_THRESHOLD_3);
}

void announceItem(voicePromptItem_t item, audioPromptThreshold_t immediateAnnounceThreshold)
{
	if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
	{
		return;
	}
	voicePromptWasPlaying = voicePromptIsActive;

	voicePromptSequenceState = item;

	voicePromptsInit();

	switch(voicePromptSequenceState)
	{
		case PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ:
			if (menuSystemGetCurrentMenuNumber() == UI_CHANNEL_MODE)
			{
				announceChannelName();
			}
			else
			{
				announceVFOAndFrequency();
			}
			break;
		case PROMPT_SEQUENCE_ZONE:
			announceZoneName();
			break;
		case PROMPT_SEQUENCE_MODE:
			announceRadioMode();
			break;
		case PROMPT_SEQUENCE_CONTACT_TG_OR_PC:
			announceContactNameTgOrPc();
			break;
		case PROMPT_SEQUENCE_TS:
			announceTS();
			break;
		case PROMPT_SEQUENCE_CC:
			announceCC();
			break;
		case PROMPT_SEQUENCE_POWER:
			announcePowerLevel();
			break;
		case PROMPT_SEQUENCE_BATTERY:
			announceBatteryPercentage();
			break;
		default:
			break;
	}
	// Follow-on when voicePromptWasPlaying is enabled on voice prompt level 2 and above
	// Prompts are voiced immediately on voice prompt level 3
	if ((voicePromptWasPlaying &&(nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ||
			(nonVolatileSettings.audioPromptMode >= immediateAnnounceThreshold))
	{
		voicePromptsPlay();
	}
}

void buildTgOrPCDisplayName(char *nameBuf, int bufferLen)
{
	int contactIndex;
	struct_codeplugContact_t contact;
	uint32_t id = (trxTalkGroupOrPcId & 0x00FFFFFF);

	if ((trxTalkGroupOrPcId >> 24) == TG_CALL_FLAG)
	{
		contactIndex = codeplugContactIndexByTGorPC(id, CONTACT_CALLTYPE_TG, &contact);
		if (contactIndex == 0)
		{
			snprintf(nameBuf, bufferLen, "TG %ld", (trxTalkGroupOrPcId & 0x00FFFFFF));
		}
		else
		{
			codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
		}
	}
	else
	{
		contactIndex = codeplugContactIndexByTGorPC(id, CONTACT_CALLTYPE_PC, &contact);
		if (contactIndex == 0)
		{
			dmrIdDataStruct_t currentRec;
			if (dmrIDLookup(id, &currentRec))
			{
				strncpy(nameBuf, currentRec.text, bufferLen);
			}
			else
			{
				// check LastHeard for TA data.
				LinkItem_t *item = lastheardFindInList(id);
				if ((item != NULL) && (strlen(item->talkerAlias) != 0))
				{
					strncpy(nameBuf, item->talkerAlias, bufferLen);
				}
				else
				{
					snprintf(nameBuf, bufferLen, "ID:%ld", id);
				}
			}
		}
		else
		{
			codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
		}
	}
}

void acceptPrivateCall(int id)
{
	uiPrivateCallState = PRIVATE_CALL;
	uiPrivateCallLastID = (id & 0xffffff);
	menuUtilityReceivedPcId = 0;

	setOverrideTGorPC(uiPrivateCallLastID, true);
	announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
}
