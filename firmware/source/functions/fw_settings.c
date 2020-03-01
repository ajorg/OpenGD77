/*
 * Copyright (C)2019 	Kai Ludwig, DG4KLU
 * 				and		Roger Clark, VK3KYY / G4KYF
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
#include <user_interface/menuSystem.h>
#include <user_interface/uiLocalisation.h>
#include "fw_settings.h"
#include "fw_trx.h"
#include "fw_codeplug.h"
#include "fw_sound.h"

static const int STORAGE_BASE_ADDRESS 		= 0x6000;

static const int STORAGE_MAGIC_NUMBER 		= 0x4744;

settingsStruct_t nonVolatileSettings;
struct_codeplugChannel_t *currentChannelData;
struct_codeplugChannel_t channelScreenChannelData={.rxFreq=0};
struct_codeplugContact_t contactListContactData;
struct_codeplugChannel_t settingsVFOChannel[2];// VFO A and VFO B from the codeplug.
int contactListContactIndex;
int settingsUsbMode = USB_MODE_CPS;
int settingsCurrentChannelNumber=0;
bool settingsPrivateCallMuteMode = false;

bool settingsSaveSettings(bool includeVFOs)
{
	if (includeVFOs)
	{
		codeplugSetVFO_ChannelData(&settingsVFOChannel[0],0);
		codeplugSetVFO_ChannelData(&settingsVFOChannel[1],1);
	}
	return EEPROM_Write(STORAGE_BASE_ADDRESS, (uint8_t*)&nonVolatileSettings, sizeof(settingsStruct_t));
}

bool settingsLoadSettings(void)
{
	bool readOK = EEPROM_Read(STORAGE_BASE_ADDRESS, (uint8_t*)&nonVolatileSettings, sizeof(settingsStruct_t));
	if (nonVolatileSettings.magicNumber != STORAGE_MAGIC_NUMBER || readOK != true)
	{
		settingsRestoreDefaultSettings();
	}

	codeplugGetVFO_ChannelData(&settingsVFOChannel[0],0);
	codeplugGetVFO_ChannelData(&settingsVFOChannel[1],1);
	settingsInitVFOChannel(0);// clean up any problems with VFO data
	settingsInitVFOChannel(1);

	trxDMRID = codeplugGetUserDMRID();

	if (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE)
	{
		trxSetRxCTCSS(TRX_CTCSS_TONE_NONE);
	}

	currentLanguage = &languages[nonVolatileSettings.languageIndex];

	// Added this parameter without changing the magic number, so need to check for default / invalid numbers
	if (nonVolatileSettings.txTimeoutBeepX5Secs > 4)
	{
		nonVolatileSettings.txTimeoutBeepX5Secs=0;
	}

	// Added this parameter without changing the magic number, so need to check for default / invalid numbers
	if (nonVolatileSettings.beepVolumeDivider>10)
	{
		nonVolatileSettings.beepVolumeDivider = 2;// no reduction in volume
	}
	soundBeepVolumeDivider = nonVolatileSettings.beepVolumeDivider;

	codeplugInitChannelsPerZone();// Initialise the codeplug channels per zone

	return readOK;
}

void settingsInitVFOChannel(int vfoNumber)
{

	// temporary hack in case the code plug has no RxGroup selected
	// The TG needs to come from the RxGroupList
	if (settingsVFOChannel[vfoNumber].rxGroupList == 0)
	{
		settingsVFOChannel[vfoNumber].rxGroupList=1;
	}

	if (settingsVFOChannel[vfoNumber].contact == 0)
	{
		settingsVFOChannel[vfoNumber].contact=1;
	}

/*
	if (settingsVFOChannel[vfoNumber].chMode == RADIO_MODE_ANALOG)
	{
		// In Analog mode, some crucial DMR settings will be invalid.
		// So we need to set them to usable defaults
		settingsVFOChannel[vfoNumber].rxGroupList=1;
		settingsVFOChannel[vfoNumber].rxColor = 1;
		nonVolatileSettings.overrideTG = 9;// Set the override TG to local TG 9
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}

	if (!trxCheckFrequencyInAmateurBand(settingsVFOChannel[vfoNumber].rxFreq))
	{
		settingsVFOChannel[vfoNumber].rxFreq = BAND_UHF_MIN;
	}

	if (!trxCheckFrequencyInAmateurBand(settingsVFOChannel[vfoNumber].txFreq))
	{
		settingsVFOChannel[vfoNumber].txFreq = BAND_UHF_MIN;
	}
*/
}

void settingsRestoreDefaultSettings(void)
{
	nonVolatileSettings.magicNumber=STORAGE_MAGIC_NUMBER;
	nonVolatileSettings.currentChannelIndexInZone = 0;
	nonVolatileSettings.currentChannelIndexInAllZone = 1;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]=0;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE]=0;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_B_MODE]=0;
	nonVolatileSettings.currentZone = 0;
	nonVolatileSettings.backlightMode = BACKLIGHT_MODE_AUTO;
	nonVolatileSettings.backLightTimeout = 0;//0 = never timeout. 1 - 255 time in seconds
	nonVolatileSettings.displayContrast = 0x12;
	nonVolatileSettings.initialMenuNumber=MENU_VFO_MODE;
	nonVolatileSettings.displayBacklightPercentage=100U;// 100% brightness
	nonVolatileSettings.displayBacklightPercentageOff=0U;// 0% brightness
	nonVolatileSettings.displayInverseVideo=false;// Not inverse video
	nonVolatileSettings.useCalibration = true;// enable the new calibration system
	nonVolatileSettings.txFreqLimited = true;// Limit Tx frequency to US Amateur bands
	nonVolatileSettings.txPowerLevel=4;// 1 W
	nonVolatileSettings.overrideTG=0;// 0 = No override
	nonVolatileSettings.txTimeoutBeepX5Secs = 0;
	nonVolatileSettings.beepVolumeDivider = 1;// no reduction in volume
	nonVolatileSettings.micGainDMR = 11;// Normal value used by the official firmware
	nonVolatileSettings.tsManualOverride = 0; // No manual TS override using the Star key
	nonVolatileSettings.keypadTimerLong = 5;
	nonVolatileSettings.keypadTimerRepeat = 3;
	nonVolatileSettings.currentVFONumber = 0;
	nonVolatileSettings.dmrFilterLevel = DMR_FILTER_CC_TS;
	nonVolatileSettings.dmrCaptureTimeout=10;// Default to holding 10 seconds after a call ends
	nonVolatileSettings.analogFilterLevel = ANALOG_FILTER_CTCSS;
	nonVolatileSettings.languageIndex=0;
	nonVolatileSettings.scanDelay=5;// 5 seconds
	nonVolatileSettings.scanModePause = SCAN_MODE_HOLD;
	nonVolatileSettings.squelchDefaults[RADIO_BAND_VHF]		= 10;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.squelchDefaults[RADIO_BAND_220MHz]	= 10;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.squelchDefaults[RADIO_BAND_UHF]		= 10;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.pttToggle = false; // PTT act as a toggle button
	nonVolatileSettings.hotspotType = HOTSPOT_TYPE_OFF;
	nonVolatileSettings.transmitTalkerAlias	= false;
    nonVolatileSettings.privateCalls = true;
	nonVolatileSettings.vfoAScanLow = BAND_VHF_MIN;  // Low frequency limit for VFO A Scanning
	nonVolatileSettings.vfoAScanHigh = BAND_VHF_MAX; // High Frequency limit for VFO A Scanning
	nonVolatileSettings.vfoBScanLow = BAND_UHF_MIN;  // Low frequency limit for VFO B Scanning
	nonVolatileSettings.vfoBScanHigh = BAND_UHF_MAX; // High Frequency limit for VFO B Scanning
	nonVolatileSettings.contactDisplayPriority = CONTACT_DISPLAY_PRIO_CC_DB_TA;
	nonVolatileSettings.splitContact = SPLIT_CONTACT_SINGLE_LINE_ONLY;


	currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];// Set the current channel data to point to the VFO data since the default screen will be the VFO

	settingsSaveSettings(false);
}
