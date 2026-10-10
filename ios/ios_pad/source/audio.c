/*
 *   Bluetooth audio connection test (audio extension for Bloopair)
 *
 *   Once armed, the next finished device search (sync button) pairs with the first audio
 *   device found by the scan, opens an L2CAP channel on the AVDTP PSM (0x19), sends an
 *   AVDTP "discover" command, logs the answer and disconnects again. No audio is sent.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "audio.h"
#include "scan.h"

#define AVDTP_PSM               0x0019
#define AVDTP_SEC_SERVICE_ID    35
#define AVDTP_LOCAL_MTU         672
#define TX_OFFSET               32      // generous space in front of the payload for headers
#define BT_HDR_SIZE             8

// functions of the Bluetooth stack in IOS-PAD (addresses in imports.sym)
extern uint16_t L2CA_Register(uint16_t psm, const void* p_cb_info);
extern uint16_t L2CA_ConnectReq(uint16_t psm, uint8_t* p_bd_addr);
extern uint8_t L2CA_ConfigReq(uint16_t cid, void* p_cfg);
extern uint8_t L2CA_ConfigRsp(uint16_t cid, void* p_cfg);
extern uint8_t L2CA_DataWrite(uint16_t cid, BT_HDR* p_data);
extern uint8_t L2CA_DisconnectReq(uint16_t cid);
extern uint8_t L2CA_DisconnectRsp(uint16_t cid);
extern void BTM_SetPairableMode(uint8_t allow_pairing, uint8_t connect_only_paired);
extern uint8_t BTM_SecBond(uint8_t* p_bd_addr, uint8_t pin_len, uint8_t* p_pin, uint32_t* trusted_mask);
extern uint8_t BTM_SetSecurityLevel(uint8_t is_originator, const char* p_name, uint8_t service_id,
                                    uint16_t sec_level, uint16_t psm, uint32_t mx_proto_id, uint32_t mx_chan_id);

// tL2CAP_CFG_INFO is 0x48 bytes, only the fields we need are accessed (offsets taken from the HID host)
typedef struct {
    uint8_t raw[0x48];
} __attribute__ ((aligned(4))) L2capCfg;

#define CFG_RESULT(c)           (*(volatile uint16_t*) &(c)->raw[0x00])
#define CFG_MTU_PRESENT(c)      ((c)->raw[0x02])
#define CFG_MTU(c)              (*(volatile uint16_t*) &(c)->raw[0x04])
#define CFG_FLUSH_PRESENT(c)    ((c)->raw[0x20])

// security events of the Bluetooth application layer (BTA_DM_SP_CFM_REQ_EVT is in bt_api.h)
#define BTA_DM_AUTH_CMPL_EVT    3
#define BTA_DM_LINK_DOWN_EVT    6

#define BTA_DM_LINK_UP_EVT      5

// state of the security manager (addresses derived from the IOS-PAD binary)
#define BTM_PAIRING_DISABLED    (*(volatile uint8_t*) 0x1214fe79)
#define BTM_PAIRING_STATE       (*(volatile uint8_t*) 0x1214fe8f)

// start of tBTA_DM_AUTH_CMPL (layout confirmed in the IOS-PAD binary)
typedef struct {
    BD_ADDR bd_addr;
    BD_NAME bd_name;
    uint8_t key_present;
    LINK_KEY key;
    uint8_t key_type;
    uint8_t success;
    uint8_t fail_reason;
} AuthCmpl;

// tL2CAP_APPL_INFO, same layout the HID host registers
typedef struct {
    void (*connectInd)(uint8_t* bd_addr, uint32_t lcid, uint32_t psm, uint32_t id);
    void (*connectCfm)(uint32_t lcid, uint32_t result);
    void (*connectPnd)(uint32_t lcid);
    void (*configInd)(uint32_t lcid, L2capCfg* cfg);
    void (*configCfm)(uint32_t lcid, L2capCfg* cfg);
    void (*disconnectInd)(uint32_t lcid, uint32_t ackNeeded);
    void (*disconnectCfm)(uint32_t lcid, uint32_t result);
    void (*qosViolationInd)(uint8_t* bd_addr);
    void (*dataInd)(uint32_t lcid, BT_HDR* p_buf);
    void (*congestionStatus)(uint32_t lcid, uint32_t congested);
    void (*txComplete)(uint32_t lcid, uint32_t numSdu);
} L2capApplInfo;

static BloopairAudioStatus status;
static uint8_t l2capRegistered;
static uint8_t cfgLocalDone;
static uint8_t cfgRemoteDone;

static void logEvent(uint16_t event, uint16_t value, uint32_t data)
{
    if (status.logCount >= BLOOPAIR_AUDIO_LOG_SIZE) {
        return;
    }

    BloopairAudioLogEntry* entry = &status.log[status.logCount];
    entry->event = event;
    entry->value = value;
    entry->data = data;
    status.logCount++;
}

static void fail(void)
{
    status.state = BLOOPAIR_AUDIO_STATE_FAILED;
    status.enabled = 0;
}

static void sendDiscover(uint16_t cid)
{
    BT_HDR* p_buf = (BT_HDR*) GKI_getbuf(BT_HDR_SIZE + TX_OFFSET + 8);
    if (!p_buf) {
        logEvent(BLOOPAIR_AUDIO_EV_NO_BUFFER, cid, 0);
        fail();
        return;
    }

    uint8_t* data = ((uint8_t*) p_buf) + BT_HDR_SIZE + TX_OFFSET;
    p_buf->event = 0;
    p_buf->layer_specific = 0;
    p_buf->offset = TX_OFFSET;
    p_buf->len = 2;

    // AVDTP signalling: transaction label 1, single packet, command / signal id "discover"
    data[0] = 0x10;
    data[1] = 0x01;

    uint8_t res = L2CA_DataWrite(cid, p_buf);
    logEvent(BLOOPAIR_AUDIO_EV_DISCOVER_SENT, cid, res);
}

static void checkChannelOpen(uint16_t cid)
{
    if (!cfgLocalDone || !cfgRemoteDone || status.state != BLOOPAIR_AUDIO_STATE_CONFIGURING) {
        return;
    }

    status.state = BLOOPAIR_AUDIO_STATE_OPEN;
    logEvent(BLOOPAIR_AUDIO_EV_CHANNEL_OPEN, cid, 0);
    sendDiscover(cid);
}

static void cbConnectInd(uint8_t* bd_addr, uint32_t lcid, uint32_t psm, uint32_t id)
{
    // we never expect incoming connections, just log them
    logEvent(BLOOPAIR_AUDIO_EV_CONNECT_IND, psm & 0xFFFF, lcid & 0xFFFF);
}

static void cbConnectCfm(uint32_t lcid, uint32_t result)
{
    uint16_t cid = lcid & 0xFFFF;
    uint16_t res = result & 0xFFFF;
    logEvent(BLOOPAIR_AUDIO_EV_CONNECT_CFM, cid, res);

    if (res != 0) {
        fail();
        return;
    }

    status.cid = cid;
    status.state = BLOOPAIR_AUDIO_STATE_CONFIGURING;
    cfgLocalDone = 0;
    cfgRemoteDone = 0;

    L2capCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    CFG_MTU_PRESENT(&cfg) = 1;
    CFG_MTU(&cfg) = AVDTP_LOCAL_MTU;

    uint8_t cfgRes = L2CA_ConfigReq(cid, &cfg);
    logEvent(BLOOPAIR_AUDIO_EV_CONFIG_REQ, cid, cfgRes);
}

static void cbConfigInd(uint32_t lcid, L2capCfg* cfg)
{
    uint16_t cid = lcid & 0xFFFF;
    logEvent(BLOOPAIR_AUDIO_EV_CONFIG_IND, cid, CFG_MTU_PRESENT(cfg) ? CFG_MTU(cfg) : 0);

    // accept the remote configuration as it is
    CFG_RESULT(cfg) = 0;
    CFG_MTU_PRESENT(cfg) = 0;
    CFG_FLUSH_PRESENT(cfg) = 0;
    L2CA_ConfigRsp(cid, cfg);

    cfgRemoteDone = 1;
    checkChannelOpen(cid);
}

static void cbConfigCfm(uint32_t lcid, L2capCfg* cfg)
{
    uint16_t cid = lcid & 0xFFFF;
    uint16_t res = CFG_RESULT(cfg);
    logEvent(BLOOPAIR_AUDIO_EV_CONFIG_CFM, cid, res);

    if (res != 0) {
        fail();
        return;
    }

    cfgLocalDone = 1;
    checkChannelOpen(cid);
}

static void cbDisconnectInd(uint32_t lcid, uint32_t ackNeeded)
{
    uint16_t cid = lcid & 0xFFFF;
    logEvent(BLOOPAIR_AUDIO_EV_DISCONNECT_IND, cid, ackNeeded & 0xFF);

    if (ackNeeded & 0xFF) {
        L2CA_DisconnectRsp(cid);
    }

    if (status.state != BLOOPAIR_AUDIO_STATE_DONE && status.state != BLOOPAIR_AUDIO_STATE_FAILED) {
        status.state = BLOOPAIR_AUDIO_STATE_CLOSED;
    }
}

static void cbDisconnectCfm(uint32_t lcid, uint32_t result)
{
    logEvent(BLOOPAIR_AUDIO_EV_DISCONNECT_CFM, lcid & 0xFFFF, result & 0xFFFF);
}

static void cbDataInd(uint32_t lcid, BT_HDR* p_buf)
{
    uint16_t cid = lcid & 0xFFFF;
    uint16_t len = p_buf->len;
    logEvent(BLOOPAIR_AUDIO_EV_DATA_IND, cid, len);

    uint16_t copyLen = len > BLOOPAIR_AUDIO_RESP_SIZE ? BLOOPAIR_AUDIO_RESP_SIZE : len;
    memcpy(status.response, ((uint8_t*) p_buf) + BT_HDR_SIZE + p_buf->offset, copyLen);
    status.responseLength = len;
    GKI_freebuf(p_buf);

    // we got what we came for, close the channel again
    status.state = BLOOPAIR_AUDIO_STATE_DONE;
    status.enabled = 0;
    L2CA_DisconnectReq(cid);
    logEvent(BLOOPAIR_AUDIO_EV_DISCONNECT_REQ, cid, 0);
}

static void cbCongestion(uint32_t lcid, uint32_t congested)
{
}

static const L2capApplInfo applInfo = {
    cbConnectInd,
    cbConnectCfm,
    NULL,
    cbConfigInd,
    cbConfigCfm,
    cbDisconnectInd,
    cbDisconnectCfm,
    NULL,
    cbDataInd,
    cbCongestion,
    NULL,
};

// open the L2CAP channel on the AVDTP psm (the device has to be paired already)
static void connectL2cap(void)
{
    // tell the security manager how to treat connections to the AVDTP psm
    uint8_t secRes = BTM_SetSecurityLevel(1, "AVDTP", AVDTP_SEC_SERVICE_ID,
        BTM_SEC_OUT_AUTHENTICATE | BTM_SEC_OUT_ENCRYPT, AVDTP_PSM, 0, 0);
    logEvent(BLOOPAIR_AUDIO_EV_SECURITY, 0, secRes);

    if (!l2capRegistered) {
        uint16_t psm = L2CA_Register(AVDTP_PSM, &applInfo) & 0xFFFF;
        logEvent(BLOOPAIR_AUDIO_EV_L2CAP_REGISTER, psm, 0);
        if (psm == 0) {
            fail();
            return;
        }

        l2capRegistered = 1;
    }

    status.state = BLOOPAIR_AUDIO_STATE_CONNECTING;
    uint16_t cid = L2CA_ConnectReq(AVDTP_PSM, status.bd_address) & 0xFFFF;
    status.cid = cid;
    logEvent(BLOOPAIR_AUDIO_EV_CONNECT_REQ, cid, 0);
    if (cid == 0) {
        fail();
    }
}

static void startConnect(void)
{
    uint8_t addr[6];
    if (!scanFindFirstAudio(addr)) {
        logEvent(BLOOPAIR_AUDIO_EV_NO_TARGET, 0, 0);
        fail();
        return;
    }

    memcpy(status.bd_address, addr, 6);
    logEvent(BLOOPAIR_AUDIO_EV_TARGET, (addr[0] << 8) | addr[1],
        (addr[2] << 24) | (addr[3] << 16) | (addr[4] << 8) | addr[5]);

    // make sure the stack accepts pairing, then pair like a phone would (dedicated bonding)
    BTM_SetPairableMode(1, 0);
    logEvent(BLOOPAIR_AUDIO_EV_PAIRABLE, 0, 0);

    status.state = BLOOPAIR_AUDIO_STATE_PAIRING;
    uint8_t bondRes = BTM_SecBond(status.bd_address, 0, NULL, NULL);
    logEvent(BLOOPAIR_AUDIO_EV_BOND_REQ, 0, bondRes);

    if (bondRes == 0) {
        // already paired
        connectL2cap();
    } else if (bondRes != 1) {
        fail();
    }

    // otherwise wait for the pairing result, see audioOnSecurityEvent
}

void audioArmTest(uint8_t enabled)
{
    if (enabled) {
        memset(&status, 0, sizeof(status));
        status.enabled = 1;
        status.state = BLOOPAIR_AUDIO_STATE_ARMED;
    } else {
        status.enabled = 0;
        if (status.state == BLOOPAIR_AUDIO_STATE_ARMED) {
            status.state = BLOOPAIR_AUDIO_STATE_IDLE;
        }
    }
}

void audioGetStatus(BloopairAudioStatus* out)
{
    memcpy(out, &status, sizeof(status));
}

void audioOnSearchEvent(uint8_t event)
{
    if (status.state != BLOOPAIR_AUDIO_STATE_ARMED) {
        return;
    }

    // the search is over, now the radio is free to connect
    if (event != BTA_DM_INQ_CMPL_EVT) {
        return;
    }

    startConnect();
}

int audioOnSecurityEvent(uint8_t event, void* p_data)
{
    if (status.state < BLOOPAIR_AUDIO_STATE_CONNECTING && status.state != BLOOPAIR_AUDIO_STATE_PAIRING) {
        return 0;
    }

    if (status.state == BLOOPAIR_AUDIO_STATE_FAILED || status.state == BLOOPAIR_AUDIO_STATE_CLOSED) {
        return 0;
    }

    uint32_t info = 0;
    if (event == BTA_DM_SP_CFM_REQ_EVT) {
        const tBTA_DM_SP_CFM_REQ* req = (const tBTA_DM_SP_CFM_REQ*) p_data;
        info = (req->just_works << 24) | (req->loc_io_caps << 16) | (req->rmt_io_caps << 8) | req->rmt_auth_req;
    } else if (event == BTA_DM_LINK_DOWN_EVT) {
        info = ((const uint8_t*) p_data)[6];
    }

    logEvent(BLOOPAIR_AUDIO_EV_SEC_EVENT, event, info);

    if (event == BTA_DM_LINK_UP_EVT || event == BTA_DM_AUTH_CMPL_EVT) {
        logEvent(BLOOPAIR_AUDIO_EV_BTM_STATE, (BTM_PAIRING_DISABLED << 8) | BTM_PAIRING_STATE, event);
    }

    if (event == BTA_DM_LINK_UP_EVT && status.state == BLOOPAIR_AUDIO_STATE_PAIRING) {
        // the console switches pairing off again when the search ends, make sure it is on
        // now that the link is up and the pairing is about to begin
        BTM_SetPairableMode(1, 0);
        logEvent(BLOOPAIR_AUDIO_EV_PAIRABLE, 1, 0);
    }

    if (event != BTA_DM_AUTH_CMPL_EVT) {
        return 0;
    }

    const AuthCmpl* auth = (const AuthCmpl*) p_data;
    if (memcmp(auth->bd_addr, status.bd_address, 6) != 0) {
        return 0;
    }

    logEvent(BLOOPAIR_AUDIO_EV_AUTH_CMPL, auth->success, auth->fail_reason | (auth->key_present << 8));

    if (status.state == BLOOPAIR_AUDIO_STATE_PAIRING) {
        if (auth->success) {
            connectL2cap();
        } else {
            fail();
        }
    }

    // this pairing was started by us, the original code should not see it
    return 1;
}
