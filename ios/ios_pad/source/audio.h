/*
 *   Bluetooth audio connection test (audio extension for Bloopair)
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 */

#pragma once

#include <imports.h>
#include "bt_api.h"
#include <bloopair/ipc.h>

// arm / disarm the test (called from the IPC thread, only touches flags)
void audioArmTest(uint8_t enabled);

// copy the current status and event log
void audioGetStatus(BloopairAudioStatus* out);

// called from the search callback (Bluetooth stack context) for every search event
void audioOnSearchEvent(uint8_t event);

// called for every security event of the Bluetooth stack (pairing etc.).
// returns 1 if the event belongs to the audio test and must not be passed on
int audioOnSecurityEvent(uint8_t event, void* p_data);
