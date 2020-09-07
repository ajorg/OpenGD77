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
#include <ticks.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>

//static const int LAST_HEARD_NUM_LINES_ON_DISPLAY = 3;
static bool displayLHDetails = false;
static menuStatus_t menuLastHeardExitCode = MENU_STATUS_SUCCESS;
uint32_t selectedID;

static void handleEvent(uiEvent_t *ev);
static void menuLastHeardDisplayTA(uint8_t y, char *text, uint32_t time, uint32_t now, uint32_t TGorPC, size_t maxLen, bool displayDetails, bool itemIsSelected, bool isFirstRun);

menuStatus_t menuLastHeard(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		gMenusStartIndex = LinkHead->id;// reuse this global to store the ID of the first item in the list
		displayLHDetails = false;
		displayLightTrigger();
		gMenusCurrentItemIndex = 0;

		menuLastHeardUpdateScreen(true, displayLHDetails,true);
		m = ev->time;
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuLastHeardExitCode = MENU_STATUS_SUCCESS;

		// do live update by checking if the item at the top of the list has changed
		if ((gMenusStartIndex != LinkHead->id) || (menuDisplayQSODataState == QSO_DISPLAY_CALLER_DATA))
		{
			displayLightTrigger();
			gMenusStartIndex = LinkHead->id;
			if (gMenusCurrentItemIndex == 0)
			{
				menuLastHeardUpdateScreen(true, displayLHDetails,false);
			}
		}

		if (ev->hasEvent)
		{
			m = ev->time;
			handleEvent(ev);
		}
		else
		{
			// Just refresh the list while displaying details, it's all about elapsed time
			if (displayLHDetails && ((ev->time - m) > (1000U * 60U)))
			{
				m = ev->time;
				menuLastHeardUpdateScreen(true, true,false);
			}
		}

	}
	return menuLastHeardExitCode;
}

void menuLastHeardUpdateScreen(bool showTitleOrHeader, bool displayDetails, bool isFirstRun)
{
	dmrIdDataStruct_t foundRecord;
	int numDisplayed = 0;
	LinkItem_t *item = LinkHead;
	uint32_t now = fw_millis();
	bool invertColour;

	ucClearBuf();
	if (showTitleOrHeader)
	{
		menuDisplayTitle(currentLanguage->last_heard);
	}
	else
	{
		menuUtilityRenderHeader();
	}

	// skip over the first gMenusCurrentItemIndex in the listing
	for(int i = 0; i < gMenusCurrentItemIndex; i++)
	{
		item = item->next;
	}

	while((item != NULL) && (item->id != 0) && (numDisplayed < 4))
	{
		if (numDisplayed == 0)
		{
			invertColour = true;
#if defined(PLATFORM_RD5R)
			ucFillRect(0, 15, 128, 10, false);
#else
			ucFillRect(0, 16, 128, 16, false);
#endif
			selectedID = item->id;
		}
		else
		{
			invertColour = false;
		}

		if (dmrIDLookup(item->id, &foundRecord))
		{
			menuLastHeardDisplayTA(16 + (numDisplayed * MENU_ENTRY_HEIGHT), foundRecord.text, item->time, now, item->talkGroupOrPcId, 20, displayDetails,invertColour, isFirstRun);
		}
		else
		{
			if (item->talkerAlias[0] != 0x00)
			{
				menuLastHeardDisplayTA(16 + (numDisplayed * MENU_ENTRY_HEIGHT), item->talkerAlias, item->time, now, item->talkGroupOrPcId, 32, displayDetails,invertColour, isFirstRun);
			}
			else
			{
				char buffer[17];

				snprintf(buffer, 17, "ID:%ld", item->id);
				buffer[16] = 0;
				menuLastHeardDisplayTA(16 + (numDisplayed * MENU_ENTRY_HEIGHT), buffer, item->time, now, item->talkGroupOrPcId, 17, displayDetails,invertColour, isFirstRun);
			}
		}

		numDisplayed++;

		item = item->next;
	}
	ucRender();
	menuDisplayQSODataState = QSO_DISPLAY_IDLE;
}

static void handleEvent(uiEvent_t *ev)
{
	bool isDirty = false;
	displayLightTrigger();


	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		if (gMenusCurrentItemIndex < (numLastHeard - 1))
		{
			isDirty = true;
			gMenusCurrentItemIndex++;
			menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
		}
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		if (gMenusCurrentItemIndex > 0)
		{
			isDirty = true;
			gMenusCurrentItemIndex--;
			menuLastHeardExitCode |= MENU_STATUS_LIST_TYPE;
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		setOverrideTGorPC(selectedID, true);
		announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
		menuSystemPopAllAndDisplayRootMenu();
		return;
	}

	// Toggles LH simple/details view on SK2 press
	if (!displayLHDetails && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		isDirty = true;
		displayLHDetails = true;
	}
	if (displayLHDetails && (ev->events == BUTTON_EVENT) &&  !(ev->buttons & BUTTON_SK2))
	{
		isDirty = true;
		displayLHDetails = false;
	}

	if (isDirty)
	{
		bool voicePromptsWerePlaying = voicePromptIsActive;
		if ((nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1) && voicePromptIsActive)
		{
			voicePromptsTerminate();
		}

		menuLastHeardUpdateScreen(true, displayLHDetails, false);// This will also setup the voice prompt

		if ((nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1) && voicePromptsWerePlaying)
		{
			voicePromptsPlay();
		}
	}
	else
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
			return;
		}
	}
}

static void menuLastHeardDisplayTA(uint8_t y, char *text, uint32_t time, uint32_t now, uint32_t TGorPC, size_t maxLen, bool displayDetails, bool itemIsSelected, bool isFirstRun)
{
	char buffer[37]; // Max: TA 27 (in 7bit format) + ' [' + 6 (Maidenhead)  + ']' + NULL
	char tg_Buffer[17];
	char timeBuffer[17];
	uint32_t tg = (TGorPC & 0xFFFFFF);
	bool isPC = ((TGorPC >> 24) == PC_CALL_FLAG);

	// Do TG and Time stuff first as its always needed for the Voice prompts

	snprintf(tg_Buffer, 17,"%s %lu", (isPC ? "PC" : "TG"), tg);// PC or TG
	tg_Buffer[16] = 0;
	snprintf(timeBuffer, 5, "%ld", (((now - time) / 1000U) / 60U));// Time
	timeBuffer[5] = 0;

	if (itemIsSelected && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1))
	{
		if (voicePromptIsActive)
		{
			voicePromptsTerminate();
		}

		voicePromptsInit();
		if (isFirstRun)
		{
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendLanguageString(&currentLanguage->last_heard);
			voicePromptsAppendLanguageString(&currentLanguage->menu);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
		}
	}

	if (!displayDetails) // search for callsign + first name
	{
		if (strlen(text) >= 5)
		{
			int32_t  cpos;

			if ((cpos = getFirstSpacePos(text)) != -1) // Callsign found
			{
				// Check if second part is 'DMR ID:'
				// In this case, don't go further
				if (strncmp((text + cpos + 1), "DMR ID:", 7) == 0)
				{
					if (cpos > 15)
					{
						cpos = 16;
					}

					memcpy(buffer, text, cpos);
					buffer[cpos] = 0;

					ucPrintCore(0,y , chomp(buffer), FONT_SIZE_3,TEXT_ALIGN_CENTER, itemIsSelected);
				}
				else // Nope, look for first name
				{
					uint32_t npos;
					char nameBuf[17];
					char outputBuf[17];

					memset(nameBuf, 0, sizeof(nameBuf));

					// Look for the second part, aka first name
					if ((npos = getFirstSpacePos(text + cpos + 1)) != -1)
					{
						// Store callsign
						memcpy(buffer, text, cpos);
						buffer[cpos] = 0;

						// Store 'first name'
						memcpy(nameBuf, (text + cpos + 1), npos);
						nameBuf[npos] = 0;

						snprintf(outputBuf, 17, "%s %s", chomp(buffer), chomp(nameBuf));
						outputBuf[16] = 0;

						ucPrintCore(0,y, chomp(outputBuf), FONT_SIZE_3,TEXT_ALIGN_CENTER, itemIsSelected);
					}
					else
					{
						// Store callsign
						memcpy(buffer, text, cpos);
						buffer[cpos] = 0;

						memcpy(nameBuf, (text + cpos + 1), strlen(text) - cpos - 1);
						nameBuf[16] = 0;

						snprintf(outputBuf, 17, "%s %s", chomp(buffer), chomp(nameBuf));
						outputBuf[16] = 0;

						ucPrintCore(0,y, chomp(outputBuf), FONT_SIZE_3,TEXT_ALIGN_CENTER, itemIsSelected);
					}
				}
			}
			else
			{
				// No space found, use a chainsaw
				memcpy(buffer, text, 16);
				buffer[16] = 0;

				ucPrintCore(0, y, chomp(buffer), FONT_SIZE_3,TEXT_ALIGN_CENTER, itemIsSelected);
			}
		}
		else // short callsign
		{
			memcpy(buffer, text, strlen(text));
			buffer[strlen(text)] = 0;

			ucPrintCore(0, y, chomp(buffer), FONT_SIZE_3,TEXT_ALIGN_CENTER, itemIsSelected);
		}

		if (itemIsSelected && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1))
		{
			voicePromptsAppendString(chomp(buffer));
			voicePromptsAppendString("  ");

			snprintf(buffer, 37, "%ld ", tg);
			if (isPC)
			{
				voicePromptsAppendLanguageString(&currentLanguage->private_call);
				if (tg != trxDMRID)
				{
					voicePromptsAppendString(buffer);
				}
			}
			else
			{
				voicePromptsAppendPrompt(PROMPT_TALKGROUP);
				voicePromptsAppendString(buffer);
			}

			voicePromptsAppendString(timeBuffer);
			voicePromptsAppendPrompt(PROMPT_MINUTES);
			voicePromptsAppendString("   ");// Add some blank sound at the end of the callsign, to allow time for follow-on scrolling
		}
	}
	else
	{
		ucPrintCore(0, y, tg_Buffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, itemIsSelected);

#if defined(PLATFORM_RD5R)
		ucPrintCore((DISPLAY_SIZE_X - (3 * 6)), y, "min", FONT_SIZE_1, TEXT_ALIGN_LEFT, itemIsSelected);
#else
		ucPrintCore((DISPLAY_SIZE_X - (3 * 6)), (y + 6), "min", FONT_SIZE_1, TEXT_ALIGN_LEFT, itemIsSelected);
#endif
		ucPrintCore((DISPLAY_SIZE_X - (strlen(timeBuffer) * 8) - (3 * 6) - 1), y, timeBuffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, itemIsSelected);
	}

	if (isFirstRun)
	{
		voicePromptsPlay();
	}
}
