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
#ifndef _FW_MENUSYSTEM_H_
#define _FW_MENUSYSTEM_H_
#include "main.h"
#include "sound.h"
#include "settings.h"
#include <functions/voicePrompts.h>

typedef enum
{
	NO_EVENT = 0,
	KEY_EVENT = 0x01,
	BUTTON_EVENT = 0x02,
	FUNCTION_EVENT = 0x04,
	ROTARY_EVENT = 0x08
} uiEventInput_t;

typedef struct
{
	uint32_t        buttons;
	keyboardCode_t  keys;
	uint32_t        rotary;
	uint16_t        function;
	uiEventInput_t  events;
	bool            hasEvent;
	uint32_t        time;
} uiEvent_t;

#if defined(PLATFORM_RD5R)
#define MENU_ENTRY_HEIGHT 10
#else
#define MENU_ENTRY_HEIGHT 16
#endif

#define MENU_MAX_DISPLAYED_ENTRIES 3

// Short press event
#define BUTTONCHECK_SHORTUP(e, sk) (((e)->keys.key == 0) && ((e)->buttons & sk ## _SHORT_UP))
// Long press event
#define BUTTONCHECK_LONGDOWN(e, sk) (((e)->keys.key == 0) && ((e)->buttons & sk ## _LONG_DOWN))
// SK*/ORANGE button is down, regardless event status
#define BUTTONCHECK_DOWN(e, sk) (((e)->buttons & sk))


extern bool uiChannelModeScanActive;
extern int menuDisplayLightTimer;
extern int uiPrivateCallState;
extern int uiPrivateCallLastID;

typedef enum
{
	MENU_STATUS_SUCCESS     = 0,
	MENU_STATUS_ERROR       = (1 << 0),
	MENU_STATUS_LIST_TYPE   = (1 << 1),
	MENU_STATUS_INPUT_TYPE  = (1 << 2),
	MENU_STATUS_FORCE_FIRST = (1 << 3)
} menuStatus_t;

typedef menuStatus_t (*menuFunctionPointer_t)(uiEvent_t *, bool); // Typedef for menu function pointers.  Functions are passed the key, the button and the event data. Event can be a Key or a button or both. Last arg is for when the function is only called to initialise and display its screen.
typedef struct menuControlDataStruct
{
	int currentMenuNumber;
	int stackPosition;
	int stack[16];
	int itemIndex[16];
} menuControlDataStruct_t;


typedef struct menuItemNewData
{
       const int stringOffset; // String offset in stringsTable_t
       const int menuNum;
} menuItemNewData_t;

typedef struct menuItemsList
{
	const int	numItems;
	const menuItemNewData_t *items;
} menuItemsList_t;

extern menuControlDataStruct_t menuControlData;

void menuDisplayTitle(const char *title);
void menuDisplayEntry(int loopOffset, int focusedItem,const char *entryText);
int menuGetMenuOffset(int maxMenuEntries, int loopOffset);

void uiChannelModeUpdateScreen(int txTimeSecs);
void uiChannelModeColdStart(void);
void uiVFOModeUpdateScreen(int txTimeSecs);
void uiVFOModeStopScanning(void);
bool uiVFOModeIsScanning(void);
void uiChannelModeStopScanning(void);
bool uiChannelModeIsScanning(void);

typedef enum
{
	CPS2UI_COMMAND_CLEARBUF = 0,
	CPS2UI_COMMAND_PRINT,
	CPS2UI_COMMAND_RENDER_DISPLAY,
	CPS2UI_COMMAND_BACKLIGHT,
	CPS2UI_COMMAND_GREEN_LED,
	CPS2UI_COMMAND_RED_LED,
	CPS2UI_COMMAND_END
} uiCPSCommand_t;

void uiCPSUpdate(uiCPSCommand_t command, int x, int y, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted, char *szMsg);

void menuInitMenuSystem(void);
void menuSystemLanguageHasChanged(void);
void displayLightTrigger(void);
void displayLightOverrideTimeout(int timeout);
void menuSystemPushNewMenu(int menuNumber);
#if 0 // Unused
void menuSystemPushNewMenuWithQuickFunction(int menuNumber, int quickFunction);
#endif

void menuSystemSetCurrentMenu(int menuNumber);
int menuSystemGetCurrentMenuNumber(void);
int menuSystemGetPreviousMenuNumber(void);
int menuSystemGetRootMenuNumber(void);

void menuSystemPopPreviousMenu(void);
void menuSystemPopAllAndDisplayRootMenu(void);
void menuSystemPopAllAndDisplaySpecificRootMenu(int newRootMenu, bool resetKeyboard);

void menuSystemCallCurrentMenuTick(uiEvent_t *ev);
int menuGetKeypadKeyValue(uiEvent_t *ev, bool digitsOnly);
void menuUpdateCursor(int pos, bool moved, bool render);
void moveCursorLeftInString(char *str, int *pos, bool delete);
void moveCursorRightInString(char *str, int *pos, int max, bool insert);

void menuBatteryInit(void);
void menuBatteryPushBackVoltage(int32_t voltage);

void menuLockScreenPop(void);

void menuLastHeardUpdateScreen(bool showTitleOrHeader, bool displayDetails, bool isFirstRun);

void menuClearPrivateCall(void);
void menuAcceptPrivateCall(int id);

void menuHotspotRestoreSettings(void);

void menuSystemMenuIncrement(int32_t *O, int32_t M);
void menuSystemMenuDecrement(int32_t *O, int32_t M);

uint16_t cssGetTone(int32_t index, CSSTypes_t type);
void cssIncrement(uint16_t *tone, int32_t *index, CSSTypes_t *type, bool loop, bool stayInCSSType);


#if defined(PLATFORM_GD77S)
void heartBeatActivityForGD77S(uiEvent_t *ev);
#endif


/*
 * ---------------------- IMPORTANT ----------------------------
 *
 * These enums must match the menuFunctions array in menuSystem.c
 *
 * ---------------------- IMPORTANT ----------------------------
 */
enum MENU_SCREENS { UI_SPLASH_SCREEN=0,
					UI_POWER_OFF,
					UI_VFO_MODE,
					UI_CHANNEL_MODE,
					MENU_MAIN_MENU,
					MENU_CONTACTS_MENU,
					MENU_ZONE_LIST,
					MENU_BATTERY,
					MENU_FIRMWARE_INFO,
					MENU_NUMERICAL_ENTRY,
					UI_TX_SCREEN,
					MENU_RSSI_SCREEN,
					MENU_LAST_HEARD,
					MENU_OPTIONS,
					MENU_DISPLAY,
					MENU_SOUND,
					MENU_CREDITS,
					MENU_CHANNEL_DETAILS,
					UI_HOTSPOT_MODE,
					UI_CPS,
					UI_CHANNEL_QUICK_MENU,
					UI_VFO_QUICK_MENU,
					UI_LOCK_SCREEN,
					MENU_CONTACT_LIST,
					MENU_CONTACT_QUICKLIST,
					MENU_CONTACT_LIST_SUBMENU,
					MENU_CONTACT_DETAILS,
					MENU_CONTACT_NEW,
					MENU_LANGUAGE,
					UI_PRIVATE_CALL,
					NUM_MENU_ENTRIES
};

enum QUICK_FUNCTIONS {  QUICK_FUNCTIONS_MENU_PLACEHOLDER = 20,   // All values lower than this are used as menu entries
						START_SCANNING,
						INC_BRIGHTNESS,
						DEC_BRIGHTNESS,
						TOGGLE_TORCH
};

// This is used to store current position in menus. The system keeps track of its value, e.g entering in
// a submenu, it will be restored exiting that submenu. It's up to the menuFunction() to override its
// value when isFirstRun == true.
extern int32_t gMenusCurrentItemIndex;

extern int gMenusStartIndex;
extern int gMenusEndIndex;
extern menuItemNewData_t *gMenuCurrentMenuList;

extern const char menuStringTable[32][17];

extern const menuItemsList_t menuDataMainMenu;
extern const menuItemsList_t menuDataContact;
extern const menuItemsList_t menuDataContactContact;
extern const menuItemsList_t * menusData[];

menuStatus_t uiVFOMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiVFOModeQuickMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiChannelMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiChannelModeQuickMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiCPS(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiSplashScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiPowerOff(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuZoneList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuDisplayMenuList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuBattery(uiEvent_t *event, bool isFirstRun);

menuStatus_t menuFirmwareInfoScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuNumericalEntry(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuTxScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuRSSIScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLastHeard(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuDisplayOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuSoundOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuCredits(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuChannelDetails(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuHotspotMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLockScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactListSubMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactDetails(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLanguage(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuPrivateCall(uiEvent_t *event, bool isFirstRun);

#endif
