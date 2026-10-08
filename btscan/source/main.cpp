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

static void writeResultsFile(const BloopairScanResults& results)
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

    fclose(f);
}

int main(int argc, char** argv)
{
    WHBProcInit();
    WHBLogConsoleInit();

    // padscore has to be initialized for the sync button to work
    KPADInit();

    WHBLogPrintf("BtScan 0.1.0 - Bluetooth device scan");
    WHBLogPrintf("");

    IOSHandle handle = Bloopair_Open();
    bool active = handle >= 0 && Bloopair_IsActive(handle);
    if (!active) {
        WHBLogPrintf("Bloopair is not active!");
        WHBLogPrintf("Make sure 30_bloopair.rpx is in the setup modules folder");
        WHBLogPrintf("and restart the console.");
    } else {
        IOSError res = Bloopair_SetScanMode(handle, TRUE, TRUE);
        if (res < 0) {
            WHBLogPrintf("Failed to enable scan mode (%d)", (int) res);
            WHBLogPrintf("Is this the build with the audio extension?");
            active = false;
        } else {
            WHBLogPrintf("1) Put your headphones into pairing mode");
            WHBLogPrintf("2) Press the SYNC button on the console");
            WHBLogPrintf("3) Wait a few seconds, found devices show up below");
            WHBLogPrintf("Press HOME to quit. Results are saved to:");
            WHBLogPrintf("  %s", OUTPUT_FILE);
            WHBLogPrintf("");
        }
    }

    BloopairScanResults shown;
    memset(&shown, 0, sizeof(shown));

    while (WHBProcIsRunning()) {
        if (active) {
            BloopairScanResults results;
            if (Bloopair_GetScanResults(handle, &results) >= 0) {
                bool changed = false;
                for (uint32_t i = 0; i < results.count && i < BLOOPAIR_SCAN_MAX_RESULTS; i++) {
                    const BloopairScanEntry& cur = results.entries[i];
                    bool isNew = i >= shown.count;
                    bool nameChanged = !isNew && memcmp(shown.entries[i].name, cur.name, sizeof(cur.name)) != 0;
                    if (isNew || nameChanged) {
                        WHBLogPrintf("%s", formatEntry(cur).c_str());
                        changed = true;
                    }
                }

                if (changed) {
                    writeResultsFile(results);
                    shown = results;
                }
            }
        }

        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(250));
    }

    if (handle >= 0) {
        if (active) {
            // restore normal controller-only behaviour
            Bloopair_SetScanMode(handle, FALSE, FALSE);
        }
        Bloopair_Close(handle);
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
