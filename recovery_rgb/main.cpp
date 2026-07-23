/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/file.h>
#include <string>
#include <unistd.h>
#include <vector>

int main(int argc, char* argv[]) {
    const std::string FASTBOOT = "255 25 0";
    const std::string RECOVERY = "255 0 255";
    const std::string FASTBOOT_RUNNING = "running";
    const std::string BRIGHTNESS = "127";

    std::string color = RECOVERY;
    if (argc > 1 && argv[1] == FASTBOOT_RUNNING) {
        color = FASTBOOT;
    }

    const std::vector<std::string> leds = {
        "stick",
        "stick:0",
        "stick:1",
        "stick:2",
        "stick:3",
        "strip"
    };

    for (const char* side : {"left", "right"}) {
        for (const std::string& led : leds) {
            std::string path = "/sys/class/leds/" + std::string(side) + ":" + led;

            if (!access(path.c_str(), F_OK)) {
                android::base::WriteStringToFile(BRIGHTNESS, path + "/brightness");
                android::base::WriteStringToFile(color, path + "/multi_intensity");
            }
        }
    }

    return 0;
}
