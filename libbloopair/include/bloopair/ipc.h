/*
 *   Copyright (C) 2021-2023 GaryOderNichts
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#define BLOOPAIR_LIB 0x10

#define BLOOPAIR_FUNC_GET_VERSION                   0
#define BLOOPAIR_FUNC_READ_CONSOLE_BDADDR           1
#define BLOOPAIR_FUNC_ADD_CONTROLLER_PAIRING        2
#define BLOOPAIR_FUNC_GET_COMMIT_HASH               3
#define BLOOPAIR_FUNC_GET_CONTROLLER_INFORMATION    4
#define BLOOPAIR_FUNC_READ_RAW_REPORT               5
#define BLOOPAIR_FUNC_APPLY_CONTROLLER_CONFIG       6
#define BLOOPAIR_FUNC_APPLY_CONTROLLER_MAPPING      7
#define BLOOPAIR_FUNC_APPLY_CUSTOM_CONFIGURATION    8
#define BLOOPAIR_FUNC_GET_CONTROLLER_CONFIG         9
#define BLOOPAIR_FUNC_GET_CONTROLLER_MAPPING        10
#define BLOOPAIR_FUNC_GET_CUSTOM_CONFIGURATION      11
#define BLOOPAIR_FUNC_SET_SCAN_MODE                 12
#define BLOOPAIR_FUNC_GET_SCAN_RESULTS              13
#define BLOOPAIR_FUNC_AUDIO_TEST_ARM                14
#define BLOOPAIR_FUNC_AUDIO_TEST_GET_STATUS         15

#define BLOOPAIR_VERSION_MAJOR(v) (((v) >> 16) & 0xff)
#define BLOOPAIR_VERSION_MINOR(v) (((v) >> 8) & 0xff)
#define BLOOPAIR_VERSION_PATCH(v) ((v) & 0xff)
#define BLOOPAIR_VERSION(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))

typedef struct __attribute__ ((__packed__)) {
    uint8_t data[4096];
    uint8_t lib;
    uint8_t func;
    uint16_t unk;
    uint32_t unk1;
} BtrmRequest;

typedef struct __attribute__ ((__packed__)) {
    uint8_t data[4096];
    uint8_t unk[12];
} BtrmResponse;

// structure associated with BLOOPAIR_FUNC_ADD_CONTROLLER_PAIRING
typedef struct {
    uint8_t bd_address[6];
    uint8_t link_key[16];
    uint8_t name[64];
    uint16_t vendor_id;
    uint16_t product_id;
} BloopairPairingData;

// structure associated with BLOOPAIR_FUNC_GET_CONTROLLER_INFORMATION
typedef struct {
    uint8_t controllerType;
    uint16_t vendor_id;
    uint16_t product_id;
} BloopairControllerInformationData;

// structure associated with
// - BLOOPAIR_FUNC_READ_RAW_REPORT
// - BLOOPAIR_FUNC_GET_CONTROLLER_CONFIG
// - BLOOPAIR_FUNC_GET_CONTROLLER_MAPPING
// - BLOOPAIR_FUNC_GET_CUSTOM_CONFIGURATION
typedef struct {
    uint8_t handle;
    uint8_t controllerType;
} BloopairControllerRequestData;

// structure associated with
// - BLOOPAIR_FUNC_APPLY_CONTROLLER_CONFIG
// - BLOOPAIR_FUNC_APPLY_CONTROLLER_MAPPING
// - BLOOPAIR_FUNC_APPLY_CUSTOM_CONFIGURATION
typedef struct {
    uint8_t controllerType;
    uint8_t bd_address[6];
    uint32_t dataSize;
    uint8_t data[];
} BloopairApplyControllerConfigurationData;

// Bluetooth device scan (audio extension)
#define BLOOPAIR_SCAN_MAX_RESULTS   16
#define BLOOPAIR_SCAN_NAME_SIZE     40

#define BLOOPAIR_SCAN_FLAG_NAME_KNOWN   (1 << 0)

// structure associated with BLOOPAIR_FUNC_SET_SCAN_MODE
typedef struct __attribute__ ((__packed__)) {
    uint8_t enabled;
    uint8_t clear;
} BloopairScanModeData;

typedef struct __attribute__ ((__packed__)) {
    uint8_t bd_address[6];
    uint8_t dev_class[3];
    int8_t rssi;
    uint8_t flags;
    uint8_t reserved;
    char name[BLOOPAIR_SCAN_NAME_SIZE];
} BloopairScanEntry;

// structure associated with BLOOPAIR_FUNC_GET_SCAN_RESULTS
typedef struct __attribute__ ((__packed__)) {
    uint8_t enabled;
    uint8_t count;
    BloopairScanEntry entries[BLOOPAIR_SCAN_MAX_RESULTS];
} BloopairScanResults;

// Bluetooth audio connection test (audio extension)
#define BLOOPAIR_AUDIO_LOG_SIZE     32
#define BLOOPAIR_AUDIO_RESP_SIZE    48

// states of the audio connection test
#define BLOOPAIR_AUDIO_STATE_IDLE           0
#define BLOOPAIR_AUDIO_STATE_ARMED          1   // waiting for the next search to finish
#define BLOOPAIR_AUDIO_STATE_CONNECTING     2
#define BLOOPAIR_AUDIO_STATE_CONFIGURING    3
#define BLOOPAIR_AUDIO_STATE_OPEN           4
#define BLOOPAIR_AUDIO_STATE_DONE           5   // got an answer to the AVDTP discover command
#define BLOOPAIR_AUDIO_STATE_FAILED         6
#define BLOOPAIR_AUDIO_STATE_CLOSED         7
#define BLOOPAIR_AUDIO_STATE_PAIRING        8

// events in the audio test log
#define BLOOPAIR_AUDIO_EV_NO_TARGET         1   // no audio device in the scan results
#define BLOOPAIR_AUDIO_EV_TARGET            2   // value: first two address bytes, data: last four
#define BLOOPAIR_AUDIO_EV_SECURITY          3   // data: result of BTM_SetSecurityLevel
#define BLOOPAIR_AUDIO_EV_L2CAP_REGISTER    4   // value: registered psm (0 = failed)
#define BLOOPAIR_AUDIO_EV_CONNECT_REQ       5   // value: channel id (0 = failed)
#define BLOOPAIR_AUDIO_EV_CONNECT_CFM       6   // value: channel id, data: result
#define BLOOPAIR_AUDIO_EV_CONFIG_REQ        7   // value: channel id, data: result
#define BLOOPAIR_AUDIO_EV_CONFIG_IND        8   // value: channel id, data: remote mtu
#define BLOOPAIR_AUDIO_EV_CONFIG_CFM        9   // value: channel id, data: result
#define BLOOPAIR_AUDIO_EV_CHANNEL_OPEN      10  // value: channel id
#define BLOOPAIR_AUDIO_EV_DISCOVER_SENT     11  // data: result of L2CA_DataWrite
#define BLOOPAIR_AUDIO_EV_DATA_IND          12  // value: channel id, data: length
#define BLOOPAIR_AUDIO_EV_DISCONNECT_REQ    13  // value: channel id
#define BLOOPAIR_AUDIO_EV_DISCONNECT_IND    14  // value: channel id, data: ack needed
#define BLOOPAIR_AUDIO_EV_DISCONNECT_CFM    15  // value: channel id, data: result
#define BLOOPAIR_AUDIO_EV_CONNECT_IND       16  // unexpected incoming connection, value: psm
#define BLOOPAIR_AUDIO_EV_NO_BUFFER         17
#define BLOOPAIR_AUDIO_EV_BOND_REQ          18  // data: result of BTM_SecBond (0 = already paired, 1 = started)
#define BLOOPAIR_AUDIO_EV_SEC_EVENT         19  // value: security event id, data: extra info
#define BLOOPAIR_AUDIO_EV_AUTH_CMPL         20  // value: success, data: fail reason | key present << 8
#define BLOOPAIR_AUDIO_EV_PAIRABLE          21  // pairing was enabled

// structure associated with BLOOPAIR_FUNC_AUDIO_TEST_ARM
typedef struct __attribute__ ((__packed__)) {
    uint8_t enabled;
} BloopairAudioTestData;

typedef struct __attribute__ ((__packed__)) {
    uint16_t event;
    uint16_t value;
    uint32_t data;
} BloopairAudioLogEntry;

// structure associated with BLOOPAIR_FUNC_AUDIO_TEST_GET_STATUS
typedef struct __attribute__ ((__packed__)) {
    uint8_t enabled;
    uint8_t state;
    uint16_t cid;
    uint8_t bd_address[6];
    uint16_t responseLength;                        // length of the AVDTP answer
    uint8_t response[BLOOPAIR_AUDIO_RESP_SIZE];     // first bytes of the AVDTP answer
    uint8_t logCount;
    uint8_t reserved;
    BloopairAudioLogEntry log[BLOOPAIR_AUDIO_LOG_SIZE];
} BloopairAudioStatus;
