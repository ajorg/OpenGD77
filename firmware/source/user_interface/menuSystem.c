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
#include <user_interface/menuSystem.h>
#include <user_interface/uiLocalisation.h>
#include <settings.h>
#include <ticks.h>

int menuDisplayLightTimer = -1;

menuItemNewData_t *gMenuCurrentMenuList;
int32_t gMenusCurrentItemIndex; // each menu can re-use this var to hold the position in their display list. To save wasted memory if they each had their own variable
int gMenusStartIndex;// as above
int gMenusEndIndex;// as above


menuControlDataStruct_t menuControlData = { .stackPosition = 0, .stack = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, .itemIndex = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};


/*
 * ---------------------- IMPORTANT ----------------------------
 *
 * The menuFunctions array and the menusData array.....
 *
 * MUST match the enum MENU_SCREENS in menuSystem.h
 *
 * ---------------------- IMPORTANT ----------------------------
 */
const menuItemsList_t * menusData[] = {	NULL,// splash
										NULL,// power off
										NULL,// vfo mode
										NULL,// channel mode
										&menuDataMainMenu,
										&menuDataContact,
										NULL,// zone
										NULL,// Battery
										NULL,// Firmwareinfo
										NULL,// Numerical entry
										NULL,// Tx
										NULL,// RSSI
										NULL,// LastHeard
										NULL,// Options
										NULL,// Display options
										NULL,// Sound options
										NULL,// Credits
										NULL,// Channel Details
										NULL,// hotspot mode
										NULL,// CPS
										NULL,// Quick menu - Channel
										NULL,// Quick menu - VFO
										NULL,// Lock screen
										NULL,// Contact List
										NULL,// Contact Quick List (SK2+#)
										NULL,// Contact List Quick Menu
										NULL,// Contact Details
										NULL,// New Contact
										NULL,// Language
										NULL,// Private Call
								};

const menuFunctionPointer_t menuFunctions[] = { uiSplashScreen,
												uiPowerOff,
												uiVFOMode,
												uiChannelMode,
												menuDisplayMenuList,// display Main menu using the menu display system
												menuDisplayMenuList,// display Contact menu using the menu display system
												menuZoneList,
												menuBattery,
												menuFirmwareInfoScreen,
												menuNumericalEntry,
												menuTxScreen,
												menuRSSIScreen,
												menuLastHeard,
												menuOptions,
												menuDisplayOptions,
												menuSoundOptions,
												menuCredits,
												menuChannelDetails,
												menuHotspotMode,
												uiCPS,
												uiChannelModeQuickMenu,
												uiVFOModeQuickMenu,
                                                menuLockScreen,
												menuContactList,
												menuContactList,
												menuContactListSubMenu,
												menuContactDetails,
												menuContactDetails,
												menuLanguage,
												menuPrivateCall
};

static void menuSystemCheckForFirstEntryAudible(menuStatus_t status)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_BEEP)
	{
		 if (((status & MENU_STATUS_LIST_TYPE) && (gMenusCurrentItemIndex == 0)) || (status & MENU_STATUS_FORCE_FIRST))
		 {
			 nextKeyBeepMelody = (int *)MELODY_KEY_BEEP_FIRST_ITEM;
		 }
		 else if (status & MENU_STATUS_INPUT_TYPE)
		 {
			 nextKeyBeepMelody = (int *)MELODY_ACK_BEEP;
		 }
	}
}

void menuSystemPushNewMenu(int menuNumber)
{
	if (menuControlData.stackPosition < 15)
	{
		uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };
		menuStatus_t status;

		keyboardReset();
		menuControlData.itemIndex[menuControlData.stackPosition] = gMenusCurrentItemIndex;
		menuControlData.stackPosition++;
		menuControlData.stack[menuControlData.stackPosition] = menuNumber;
		gMenusCurrentItemIndex = 0;
		status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
		menuSystemCheckForFirstEntryAudible(status);
	}
}

#if 0 // Unused
void menuSystemPushNewMenuWithQuickFunction(int menuNumber, int quickFunction)
{
	if (menuControlData.stackPosition < 15)
	{
		uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = quickFunction, .events = FUNCTION_EVENT, .hasEvent = false, .time = fw_millis() };
		menuStatus_t status;

		keyboardReset();
		menuControlData.itemIndex[menuControlData.stackPosition] = gMenusCurrentItemIndex;
		menuControlData.stackPosition++;
		menuControlData.stack[menuControlData.stackPosition] = menuNumber;
		gMenusCurrentItemIndex = 0;
		status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
		menuSystemCheckForFirstEntryAudible(status);
	}
}
#endif

void menuSystemPopPreviousMenu(void)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };
	menuStatus_t status;

	keyboardReset();
	menuControlData.itemIndex[menuControlData.stackPosition] = 0;
	menuControlData.stackPosition -= (menuControlData.stackPosition > 0) ? 1 : 0; // Avoid crashing if something goes wrong.
	gMenusCurrentItemIndex = menuControlData.itemIndex[menuControlData.stackPosition];
	status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
	menuSystemCheckForFirstEntryAudible(status);
}

void menuSystemPopAllAndDisplayRootMenu(void)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };
	menuStatus_t status;

	keyboardReset();
	memset(menuControlData.itemIndex, 0, sizeof(menuControlData.itemIndex));
	menuControlData.stackPosition = 0;
	gMenusCurrentItemIndex = 0;
	status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
	menuSystemCheckForFirstEntryAudible(status);

}

void menuSystemPopAllAndDisplaySpecificRootMenu(int newRootMenu, bool resetKeyboard)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };
	menuStatus_t status;

	if (resetKeyboard)
	{
		keyboardReset();
	}
	memset(menuControlData.itemIndex, 0, sizeof(menuControlData.itemIndex));
	menuControlData.stack[0]  = newRootMenu;
	menuControlData.stackPosition = 0;
	gMenusCurrentItemIndex = 0;
	status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
	menuSystemCheckForFirstEntryAudible(status);
}

void menuSystemSetCurrentMenu(int menuNumber)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };
	menuStatus_t status;

	keyboardReset();
	menuControlData.stack[menuControlData.stackPosition] = menuNumber;
	gMenusCurrentItemIndex = menuControlData.itemIndex[menuControlData.stackPosition];
	status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);
	menuSystemCheckForFirstEntryAudible(status);
}

int menuSystemGetCurrentMenuNumber(void)
{
	return menuControlData.stack[menuControlData.stackPosition];
}

int menuSystemGetPreviousMenuNumber(void)
{
	if (menuControlData.stackPosition >= 1)
	{
		return menuControlData.stack[menuControlData.stackPosition - 1];
	}

	return -1;
}

int menuSystemGetRootMenuNumber(void)
{
	return menuControlData.stack[0];
}


void menuSystemCallCurrentMenuTick(uiEvent_t *ev)
{
	menuStatus_t status;

	status = menuFunctions[menuControlData.stack[menuControlData.stackPosition]](ev, false);
	if (ev->hasEvent)
	{
		menuSystemCheckForFirstEntryAudible(status);
	}
}

void displayLightTrigger(void)
{
	if ((menuSystemGetCurrentMenuNumber() != UI_TX_SCREEN) &&
			(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
					|| ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL) && displayIsBacklightLit())))
	{
		if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
		{
			menuDisplayLightTimer = nonVolatileSettings.backLightTimeout * 1000;
		}
		displayEnableBacklight(true);
	}
}

// use -1 to force LED on all the time
void displayLightOverrideTimeout(int timeout)
{
	int prevTimer = menuDisplayLightTimer;

	menuDisplayLightTimer = timeout;

	if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
	{
		// Backlight is OFF, or timeout override (-1) as just been set
		if ((displayIsBacklightLit() == false) || ((timeout == -1) && (prevTimer != -1)))
		{
			displayEnableBacklight(true);
		}
	}
}

void menuInitMenuSystem(void)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = fw_millis() };

	menuDisplayLightTimer = -1;
	menuControlData.stack[menuControlData.stackPosition] = UI_SPLASH_SCREEN;// set the very first screen as the splash screen
	gMenusCurrentItemIndex = 0;
	menuFunctions[menuControlData.stack[menuControlData.stackPosition]](&ev, true);// Init and display this screen
}

void menuSystemLanguageHasChanged(void)
{
	// Force full update of menuChannelMode() on next call (if isFirstRun arg. is true)
	uiChannelModeColdStart();
}

const menuItemNewData_t mainMenuItems[] =
{
	{ 3,  MENU_ZONE_LIST },
	{ 4,  MENU_RSSI_SCREEN },
	{ 5,  MENU_BATTERY },
	{ 6,  MENU_CONTACTS_MENU },
	{ 7,  MENU_LAST_HEARD },
	{ 8,  MENU_FIRMWARE_INFO },
	{ 9,  MENU_OPTIONS },
	{ 10, MENU_DISPLAY },
	{ 11, MENU_SOUND },
	{ 12, MENU_CHANNEL_DETAILS},
	{ 13, MENU_LANGUAGE},
	{ 2,  MENU_CREDITS },
};

const menuItemsList_t menuDataMainMenu =
{
	.numItems = 12,
	.items = mainMenuItems
};

const menuItemNewData_t contractMenuItems[] =
{
	{ 15, MENU_CONTACT_LIST },
	{ 14, MENU_CONTACT_NEW },
};

const menuItemsList_t menuDataContact =
{
	.numItems = 2,
	.items = contractMenuItems
};

void menuDisplayTitle(const char *title)
{
	ucDrawFastHLine(0, 13, DISPLAY_SIZE_X, true);
	ucPrintCore(0, 3, title, FONT_SIZE_2, TEXT_ALIGN_CENTER, false);
}

void menuDisplayEntry(int loopOffset, int focusedItem,const char *entryText)
{
#if defined(PLATFORM_RD5R)
	const int MENU_START_Y = 25;
	const int HIGHLIGHT_START_Y = 24;
	const int MENU_SPACING_Y = FONT_SIZE_3_HEIGHT+2;
#else
	const int MENU_START_Y = 32;
	const int HIGHLIGHT_START_Y = 32;
	const int MENU_SPACING_Y = FONT_SIZE_3_HEIGHT;
#endif

	bool focused = (focusedItem == gMenusCurrentItemIndex);

	if (focused)
	{
		ucFillRoundRect(0, HIGHLIGHT_START_Y +  (loopOffset * MENU_SPACING_Y), DISPLAY_SIZE_X, MENU_SPACING_Y, 2, true);
	}

	ucPrintCore(0, MENU_START_Y + (loopOffset * MENU_SPACING_Y), entryText, FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);

}

int menuGetMenuOffset(int maxMenuEntries, int loopOffset)
{
	int offset = gMenusCurrentItemIndex + loopOffset;

	if (offset < 0)
	{
		if ((maxMenuEntries == 1) && (maxMenuEntries < MENU_MAX_DISPLAYED_ENTRIES))
		{
			offset = MENU_MAX_DISPLAYED_ENTRIES - 1;
		}
		else
		{
			offset = maxMenuEntries + offset;
		}
	}

	if (maxMenuEntries < MENU_MAX_DISPLAYED_ENTRIES)
	{
		if (loopOffset == 1)
		{
			offset = MENU_MAX_DISPLAYED_ENTRIES - 1;
		}
	}
	else
	{
		if (offset >= maxMenuEntries)
		{
			offset = offset - maxMenuEntries;
		}
	}

	return offset;
}

/*
 * Returns 99 if key is unknown, or not numerical when digitsOnly is true
 */
int menuGetKeypadKeyValue(uiEvent_t *ev, bool digitsOnly)
{
	uint32_t keypadKeys[] =
	{
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_LEFT, KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_STAR, KEY_HASH
	};

	for (int i = 0; i < ((sizeof(keypadKeys) / sizeof(keypadKeys[0])) - (digitsOnly ? 6 : 0 )); i++)
	{
		if (KEYCHECK_PRESS(ev->keys, keypadKeys[i]))
		{
				return i;
		}
	}

	return 99;
}

void menuUpdateCursor(int pos, bool moved, bool render)
{
#if defined(PLATFORM_RD5R)
	const int MENU_CURSOR_Y = 32;
#else
	const int MENU_CURSOR_Y = 46;
#endif

	static uint32_t lastBlink = 0;
	static bool     blink = false;
	uint32_t        m = fw_millis();

	if (moved)
	{
		blink = true;
	}

	if (moved || (m - lastBlink) > 500)
	{
		ucDrawFastHLine(pos * 8, MENU_CURSOR_Y, 8, blink);

		blink = !blink;
		lastBlink = m;

		if (render)
		{
			ucRenderRows(MENU_CURSOR_Y / 8, MENU_CURSOR_Y / 8 + 1);
		}
	}
}

void moveCursorLeftInString(char *str, int *pos, bool delete)
{
	int nLen = strlen(str);

	if (*pos > 0)
	{
		*pos -=1;

		if (delete)
		{
			for (int i = *pos; i <= nLen; i++)
			{
				str[i] = str[i + 1];
			}
		}
	}
}

void moveCursorRightInString(char *str, int *pos, int max, bool insert)
{
	int nLen = strlen(str);

	if (*pos < strlen(str))
	{
		if (insert)
		{
			if (nLen < max)
			{
				for (int i = nLen; i > *pos; i--)
				{
					str[i] = str[i - 1];
				}
				str[*pos] = ' ';
			}
		}

		if (*pos < max-1)
		{
			*pos += 1;
		}
	}
}

void menuSystemMenuIncrement(int32_t *currentItem, int32_t numItems)
{
	*currentItem = (*currentItem + 1) % numItems;
}

void menuSystemMenuDecrement(int32_t *currentItem, int32_t numItems)
{
	*currentItem = (*currentItem + numItems - 1) % numItems;
}
