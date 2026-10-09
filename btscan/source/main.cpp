/*
 *   BtScan - lists Bluetooth devices found by the console's sync button.
 *   Part of the Bloopair audio extension.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#include <bloopair/bloopair.h>

#define OUTPUT_DIR  "/vol/external01/wiiu/bloopair"
#define OUTPUT_FILE OUTPUT_DIR "/btscan.txt"

static const char* majorClassName(uint8_t majorClass)
{
    switch (majorClass) {
    case 1:  return "Computer";
    case 2:  return "Phone";
    case 3:  return "Network";
    case 4:  return "Audio/Video";
    case 5:  return "Peripheral";
    case 6:  return "Imaging";
    case 7:  return "Wearable";
    case 8:  return "Toy";
    default: return "Other";
    }
}

static std::string formatEntry(const BloopairScanEntry& e)
{
    char line[160];
    uint32_t cod = ((uint32_t) e.dev_class[2] << 16) | ((uint32_t) e.dev_class[1] << 8) | e.dev_class[0];
    const char* name = (e.flags & BLOOPAIR_SCAN_FLAG_NAME_KNOWN) ? e.name : "(name unknown)";

    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X  CoD 0x%06X  %s  rssi %d  %s",
        e.bd_address[0], e.bd_address[1], e.bd_address[2],
        e.bd_address[3], e.bd_address[4], e.bd_address[5],
        (unsigned) cod, majorClassName(e.dev_class[1] & 0x1F), (int) e.rssi, name);

    return line;
}

static const char* audioStateName(uint8_t state)
{
    switch (state) {
    case BLOOPAIR_AUDIO_STATE_IDLE:        return "idle";
    case BLOOPAIR_AUDIO_STATE_ARMED:       return "armed (waiting for the SYNC search to finish)";
    case BLOOPAIR_AUDIO_STATE_CONNECTING:  return "connecting";
    case BLOOPAIR_AUDIO_STATE_CONFIGURING: return "configuring channel";
    case BLOOPAIR_AUDIO_STATE_OPEN:        return "channel open";
    case BLOOPAIR_AUDIO_STATE_DONE:        return "DONE - got an answer from the device";
    case BLOOPAIR_AUDIO_STATE_FAILED:      return "FAILED";
    case BLOOPAIR_AUDIO_STATE_CLOSED:      return "closed";
    case BLOOPAIR_AUDIO_STATE_PAIRING:     return "pairing";
    default:                               return "?";
    }
}

static std::string describeAudioEvent(const BloopairAudioLogEntry& e)
{
    char buf[160];
    unsigned v = e.value;
    unsigned d = e.data;

    switch (e.event) {
    case BLOOPAIR_AUDIO_EV_NO_TARGET:
        return "no audio device found in the scan results";
    case BLOOPAIR_AUDIO_EV_TARGET:
        snprintf(buf, sizeof(buf), "target %02X:%02X:%02X:%02X:%02X:%02X",
            v >> 8, v & 0xFF, d >> 24, (d >> 16) & 0xFF, (d >> 8) & 0xFF, d & 0xFF);
        return buf;
    case BLOOPAIR_AUDIO_EV_SECURITY:
        snprintf(buf, sizeof(buf), "BTM_SetSecurityLevel returned %u", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_L2CAP_REGISTER:
        snprintf(buf, sizeof(buf), "L2CAP psm registered: 0x%04X (0 = failed)", v);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONNECT_REQ:
        snprintf(buf, sizeof(buf), "connect request sent, channel 0x%04X (0 = failed)", v);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONNECT_CFM:
        snprintf(buf, sizeof(buf), "connect confirm: channel 0x%04X result 0x%04X (0 = ok)", v, d);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONFIG_REQ:
        snprintf(buf, sizeof(buf), "config request sent, channel 0x%04X, returned %u", v, d);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONFIG_IND:
        snprintf(buf, sizeof(buf), "device sent its config, mtu %u", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONFIG_CFM:
        snprintf(buf, sizeof(buf), "config confirm: result 0x%04X (0 = ok)", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_CHANNEL_OPEN:
        return "AVDTP channel is open";
    case BLOOPAIR_AUDIO_EV_DISCOVER_SENT:
        snprintf(buf, sizeof(buf), "AVDTP discover sent, write result %u", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_DATA_IND:
        snprintf(buf, sizeof(buf), "received %u bytes from the device", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_DISCONNECT_REQ:
        return "closing the channel";
    case BLOOPAIR_AUDIO_EV_DISCONNECT_IND:
        snprintf(buf, sizeof(buf), "channel closed by the stack (ack needed: %u)", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_DISCONNECT_CFM:
        snprintf(buf, sizeof(buf), "disconnect confirm, result 0x%04X", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_CONNECT_IND:
        snprintf(buf, sizeof(buf), "unexpected incoming connection, psm 0x%04X", v);
        return buf;
    case BLOOPAIR_AUDIO_EV_NO_BUFFER:
        return "no buffer available";
    case BLOOPAIR_AUDIO_EV_PAIRABLE:
        return "pairing enabled";
    case BLOOPAIR_AUDIO_EV_BOND_REQ:
        snprintf(buf, sizeof(buf), "pairing started, returned %u (0 = already paired, 1 = started)", d);
        return buf;
    case BLOOPAIR_AUDIO_EV_SEC_EVENT: {
        const char* name = "other";
        switch (v) {
        case 2:  name = "PIN requested (legacy pairing)"; break;
        case 3:  name = "authentication complete"; break;
        case 4:  name = "authorization request"; break;
        case 5:  name = "link up"; break;
        case 6:  name = "link down"; break;
        case 10: name = "confirm request (simple pairing)"; break;
        case 11: name = "passkey notification"; break;
        }
        if (v == 10) {
            snprintf(buf, sizeof(buf), "security event %u: %s, just works %u, local io %u, remote io %u, remote auth req %u",
                v, name, d >> 24, (d >> 16) & 0xFF, (d >> 8) & 0xFF, d & 0xFF);
        } else {
            snprintf(buf, sizeof(buf), "security event %u: %s (info 0x%X)", v, name, d);
        }
        return buf;
    }
    case BLOOPAIR_AUDIO_EV_AUTH_CMPL:
        snprintf(buf, sizeof(buf), "pairing result: success %u, fail reason 0x%02X, key received %u",
            v, d & 0xFF, (d >> 8) & 1);
        return buf;
    default:
        snprintf(buf, sizeof(buf), "event %u value %u data %u", (unsigned) e.event, v, d);
        return buf;
    }
}

// decode the answer to the AVDTP discover command
static std::string describeAvdtpResponse(const BloopairAudioStatus& st)
{
    std::string out;
    char buf[128];

    out += "AVDTP answer (hex):";
    uint32_t shown = st.responseLength < BLOOPAIR_AUDIO_RESP_SIZE ? st.responseLength : BLOOPAIR_AUDIO_RESP_SIZE;
    for (uint32_t i = 0; i < shown; i++) {
        snprintf(buf, sizeof(buf), " %02X", st.response[i]);
        out += buf;
    }

    if (st.responseLength >= 2) {
        uint8_t msgType = st.response[0] & 3;
        if (msgType == 2) {
            out += "\n  -> accepted";
            for (uint32_t i = 2; i + 1 < shown; i += 2) {
                snprintf(buf, sizeof(buf), "\n  stream endpoint %u: %s %s, %s",
                    st.response[i] >> 2,
                    (st.response[i + 1] >> 4) == 0 ? "audio" : "other media",
                    ((st.response[i + 1] >> 3) & 1) ? "sink" : "source",
                    ((st.response[i] >> 1) & 1) ? "in use" : "free");
                out += buf;
            }
        } else if (msgType == 3) {
            out += "\n  -> rejected by the device";
        }
    }

    return out;
}

static void writeResultsFile(const BloopairScanResults& results, const std::string& audioText)
{
    mkdir(OUTPUT_DIR, 0777);

    FILE* f = fopen(OUTPUT_FILE, "w");
    if (!f) {
        return;
    }

    fprintf(f, "BtScan results (%u devices)\n", (unsigned) results.count);
    for (uint32_t i = 0; i < results.count && i < BLOOPAIR_SCAN_MAX_RESULTS; i++) {
        fprintf(f, "%s\n", formatEntry(results.entries[i]).c_str());
    }

    if (!audioText.empty()) {
        fprintf(f, "\nAudio connection test:\n%s\n", audioText.c_str());
    }

    fclose(f);
}

int main(int argc, char** argv)
{
    WHBProcInit();
    WHBLogConsoleInit();

    // padscore has to be initialized for the sync button to work
    KPADInit();

    WHBLogPrintf("BtScan 0.2.0 - Bluetooth scan and audio connection test");
    WHBLogPrintf("");

    IOSHandle handle = Bloopair_Open();
    bool active = handle >= 0 && Bloopair_IsActive(handle);
    if (!active) {
        WHBLogPrintf("Bloopair is not active!");
        WHBLogPrintf("Make sure 30_bloopair.rpx is in the setup modules folder");
        WHBLogPrintf("and restart the console.");
    } else {
        IOSError res = Bloopair_SetScanMode(handle, TRUE, TRUE);
        if (res >= 0) {
            res = Bloopair_ArmAudioTest(handle, TRUE);
        }

        if (res < 0) {
            WHBLogPrintf("Failed to enable the test (%d)", (int) res);
            WHBLogPrintf("Is this the build with the audio extension?");
            active = false;
        } else {
            WHBLogPrintf("1) Put your headphones into pairing mode");
            WHBLogPrintf("2) Press the SYNC button on the console ONCE");
            WHBLogPrintf("3) Wait. After the search the console pairs and");
            WHBLogPrintf("   connects to the first audio device found.");
            WHBLogPrintf("Press HOME to quit. Everything is also saved to:");
            WHBLogPrintf("  %s", OUTPUT_FILE);
            WHBLogPrintf("");
        }
    }

    BloopairScanResults shown;
    memset(&shown, 0, sizeof(shown));

    BloopairScanResults results;
    memset(&results, 0, sizeof(results));

    BloopairAudioStatus audio;
    memset(&audio, 0, sizeof(audio));

    uint32_t shownLogCount = 0;
    uint8_t shownState = 0xFF;
    bool shownResponse = false;
    std::string audioText;

    while (WHBProcIsRunning()) {
        if (active) {
            bool changed = false;

            if (Bloopair_GetScanResults(handle, &results) >= 0) {
                for (uint32_t i = 0; i < results.count && i < BLOOPAIR_SCAN_MAX_RESULTS; i++) {
                    const BloopairScanEntry& cur = results.entries[i];
                    bool isNew = i >= shown.count;
                    bool nameChanged = !isNew && memcmp(shown.entries[i].name, cur.name, sizeof(cur.name)) != 0;
                    if (isNew || nameChanged) {
                        WHBLogPrintf("%s", formatEntry(cur).c_str());
                        changed = true;
                    }
                }

                shown = results;
            }

            if (Bloopair_GetAudioStatus(handle, &audio) >= 0) {
                for (uint32_t i = shownLogCount; i < audio.logCount && i < BLOOPAIR_AUDIO_LOG_SIZE; i++) {
                    std::string line = describeAudioEvent(audio.log[i]);
                    WHBLogPrintf("[audio] %s", line.c_str());
                    audioText += line + "\n";
                    changed = true;
                }
                shownLogCount = audio.logCount;

                if (audio.state != shownState) {
                    shownState = audio.state;
                    WHBLogPrintf("[audio] state: %s", audioStateName(audio.state));
                    audioText += std::string("state: ") + audioStateName(audio.state) + "\n";
                    changed = true;
                }

                if (!shownResponse && audio.responseLength > 0) {
                    shownResponse = true;
                    std::string response = describeAvdtpResponse(audio);
                    WHBLogPrintf("%s", response.c_str());
                    audioText += response + "\n";
                    changed = true;
                }
            }

            if (changed) {
                writeResultsFile(results, audioText);
            }
        }

        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(250));
    }

    if (handle >= 0) {
        if (active) {
            // restore normal controller-only behaviour
            Bloopair_ArmAudioTest(handle, FALSE);
            Bloopair_SetScanMode(handle, FALSE, FALSE);
        }
        Bloopair_Close(handle);
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
