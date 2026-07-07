/*
 * Good Haptic — control Goodix pressure-sensing touchpad vibration
 * Copyright (C) 2025–2026  nwkyz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "haptic.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>


int
haptic_set(const char *device, int value)
{
    if (value < 0)  value = 0;
    if (value > 100) value = 100;

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open hidraw");
        return -1;
    }

    unsigned char buf[2];
    /*
     * GXTP5100
     * buf[0] = 0x09
     * buf[1] = strength 0~100
     */
    buf[0] = 0x09;
    buf[1] = value;

    int ret = ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf);
    close(fd);
    return ret;
}


/*
 * haptic_get  —  try to read the current strength via GET_FEATURE.
 *
 * Uses the same report ID 0x09.  Returns 0–100 on success, -1 if the
 * device doesn't support reading this report.
 */
int
haptic_get(const char *device)
{
    int fd = open(device, O_RDWR);
    if (fd < 0)
        return -1;

    unsigned char buf[2] = {0x09, 0};

    int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
    close(fd);

    if (ret < 0)
        return -1;

    int value = buf[1];
    if (value < 0)   value = 0;
    if (value > 100) value = 100;

    return value;
}


/*
 * haptic_scan  —  walk /sys/class/hidraw and collect HID device info
 *
 * For each hidrawN we read the device name from
 *   /sys/class/hidraw/hidrawN/device/uevent
 * or fall back to the hidraw node name itself.
 */
int
haptic_scan(HapticDevice **devices_out)
{
    DIR *d = opendir("/sys/class/hidraw");
    if (!d) return 0;

    HapticDevice *list = NULL;
    int count = 0, cap = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strncmp(de->d_name, "hidraw", 6)) {
            /* build path */
            char sys_path[320];
            snprintf(sys_path, sizeof(sys_path),
                     "/sys/class/hidraw/%s/device/uevent", de->d_name);

            char dev_path[320];
            snprintf(dev_path, sizeof(dev_path),
                     "/dev/%s", de->d_name);

            /* read HID name from uevent */
            char name[256] = {0};
            FILE *f = fopen(sys_path, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    if (!strncmp(line, "HID_NAME=", 9)) {
                        char *v = line + 9;
                        size_t len = strlen(v);
                        while (len > 0 && (v[len-1] == '\n' || v[len-1] == '\r'))
                            v[--len] = '\0';
                        snprintf(name, sizeof(name), "%s", v);
                        break;
                    }
                }
                fclose(f);
            }

            /* fallback */
            if (!name[0])
                snprintf(name, sizeof(name), "%s", de->d_name);

            /* grow array */
            if (count >= cap) {
                cap = cap ? cap * 2 : 8;
                list = realloc(list, (size_t)cap * sizeof(HapticDevice));
            }

            list[count].name = strdup(name);
            list[count].path = strdup(dev_path);
            count++;
        }
    }
    closedir(d);

    *devices_out = list;
    return count;
}


void
haptic_scan_free(HapticDevice *devices, int n)
{
    for (int i = 0; i < n; i++) {
        free(devices[i].name);
        free(devices[i].path);
    }
    free(devices);
}
