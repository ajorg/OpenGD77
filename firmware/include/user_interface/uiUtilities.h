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
#ifndef _MENU_UTILITY_QSO_DATA_H_
#define _MENU_UTILITY_QSO_DATA_H_                    /**< Symbol preventing repeated inclusion */
#include <user_interface/menuSystem.h>
#include <functions/settings.h>
#include "common.h"

#define MAX_ZONE_SCAN_NUISANCE_CHANNELS 16
#define NUM_LASTHEARD_STORED 32
extern const int QSO_TIMER_TIMEOUT;
extern const int TX_TIMER_Y_OFFSET;
extern const int CONTACT_Y_POS;
extern const int FREQUENCY_X_POS;

enum UI_CALL_STATE { NOT_IN_CALL=0, PRIVATE_CALL_ACCEPT, PRIVATE_CALL, PRIVATE_CALL_DECLINED };

typedef struct dmrIdDataStruct
{
	int id;
	char text[20];
} dmrIdDataStruct_t;

#define MIN_ENTRIES_BEFORE_USING_SLICES 40 // Minimal number of available IDs before using slices stuff
#define ID_SLICES 14 // Number of slices in whole DMRIDs DB

typedef struct
{
	uint32_t entries;
	uint8_t  contactLength;
	int32_t  slices[ID_SLICES]; // [0] is min availabel ID, [REGION - 1] is max available ID
	uint32_t IDsPerSlice;

} dmrIDsCache_t;

typedef struct LinkItem
{
    struct LinkItem *prev;
    uint32_t 	id;
    uint32_t 	talkGroupOrPcId;
    char        contact[21];
    char        talkgroup[17];
    char 		talkerAlias[32];// 4 blocks of data. 6 bytes + 7 bytes + 7 bytes + 7 bytes . plus 1 for termination some more for safety.
    char 		locator[7];
    uint32_t	time;// current system time when this station was heard
    struct LinkItem *next;
} LinkItem_t;

enum QSO_DISPLAY_STATE
{
	QSO_DISPLAY_IDLE,
	QSO_DISPLAY_DEFAULT_SCREEN,
	QSO_DISPLAY_CALLER_DATA,
	QSO_DISPLAY_CALLER_DATA_UPDATE
};

typedef enum
{
	SCAN_SCANNING = 0,
	SCAN_SHORT_PAUSED,
	SCAN_PAUSED
} ScanState_t;

extern const int MAX_POWER_SETTING_NUM;
extern const char *POWER_LEVELS[];
extern const char *DMR_FILTER_LEVELS[];
extern const char *ANALOG_FILTER_LEVELS[];
extern const int SCAN_SHORT_PAUSE_TIME;			//time to wait after carrier detected to allow time for full signal detection. (CTCSS or DMR)
extern const int SCAN_TOTAL_INTERVAL;			    //time between each scan step
extern const int SCAN_DMR_SIMPLEX_MIN_INTERVAL;		//minimum time between steps when scanning DMR Simplex. (needs extra time to capture TDMA Pulsing)
extern const int SCAN_FREQ_CHANGE_SETTLING_INTERVAL;//Time after frequency is changed before RSSI sampling starts
extern const int SCAN_SKIP_CHANNEL_INTERVAL;		//This is actually just an implicit flag value to indicate the channel should be skipped
extern ScanState_t scanState;
extern int scanTimer;
extern bool scanActive;
extern bool displaySquelch;
extern int scanDirection;


extern LinkItem_t *LinkHead;
extern int menuDisplayQSODataState;
extern int qsodata_timer;
extern uint32_t menuUtilityReceivedPcId;
extern uint32_t menuUtilityTgBeforePcMode;
extern const uint32_t RSSI_UPDATE_COUNTER_RELOAD;
extern settingsStruct_t originalNonVolatileSettings; // used to store previous settings in options edition related menus.
extern int nuisanceDelete[MAX_ZONE_SCAN_NUISANCE_CHANNELS];
extern int nuisanceDeleteIndex;
extern char freq_enter_digits[12];
extern int freq_enter_idx;

char *chomp(char *str);
int32_t getFirstSpacePos(char *str);
void dmrIDCacheInit(void);
bool dmrIDLookup(int targetId, dmrIdDataStruct_t *foundRecord);
bool contactIDLookup(uint32_t id, int calltype, char *buffer);
void menuUtilityRenderQSOData(void);
void menuUtilityRenderHeader(void);
LinkItem_t *lastheardFindInList(uint32_t id);
void lastheardInitList(void);
bool lastHeardListUpdate(uint8_t *dmrDataBuffer, bool forceOnHotspot);
void lastHeardClearLastID(void);
void drawRSSIBarGraph(void);
void drawDMRMicLevelBarGraph(void);
void setOverrideTGorPC(int tgOrPc, bool privateCall);
void printFrequency(bool isTX, bool hasFocus, uint8_t y, uint32_t frequency, bool displayVFOChannel,bool isScanMode);
void printToneAndSquelch(void);
int snprintRxFreqAndOffset(char *s, size_t n, int rxFreq, int txFreq);
void reset_freq_enter_digits(void);
int read_freq_enter_digits(int startDigit, int endDigit);

#endif
