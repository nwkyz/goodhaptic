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
 * haptic_set_threshold  —  set click force threshold via feature report 0x08
 *
 * Controls how much finger pressure is needed to trigger haptic click feedback.
 * Three levels: 1 = light (~110 g), 2 = medium (~150 g), 3 = firm (~190 g).
 */
int
haptic_set_threshold(const char *device, int value)
{
    if (value < 1) value = 1;
    if (value > 3) value = 3;

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open hidraw");
        return -1;
    }

    unsigned char buf[2];
    buf[0] = 0x08;
    buf[1] = value;

    int ret = ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf);
    close(fd);
    return ret;
}


/*
 * haptic_get_capability  —  read device capability via feature report 0x02
 *
 * Returns max contact count (Usage 0x55) and pad type (Usage 0x59).
 * Pad type: 0=touchpad, 1=clickpad, 2=PTP.
 */
int
haptic_get_capability(const char *device, int *contact_max, int *pad_type)
{
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open hidraw");
        return -1;
    }

    unsigned char buf[2] = {0x02, 0};
    int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
    close(fd);

    if (ret < 0) {
        fprintf(stderr, "capability: GET_FEATURE 0x02 failed\n");
        return -1;
    }

    /* 2 fields × 4 bits each: upper nibble = pad type, lower nibble = count */
    *contact_max = buf[1] & 0x0F;
    *pad_type    = (buf[1] >> 4) & 0x0F;

    return 0;
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


/*
 * haptic_get_resolution  —  parse the HID report descriptor and extract
 * Logical Maximum for X (Usage 0x30) and Y (Usage 0x31) inside Report 0x04.
 *
 * Reads the descriptor from sysfs.  Returns 0 on success, -1 on failure.
 */
int
haptic_get_resolution(const char *device, int *max_x, int *max_y)
{
    /* build sysfs path from device node, e.g. /dev/hidraw0 →
     * /sys/class/hidraw/hidraw0/device/report_descriptor */
    const char *base = strrchr(device, '/');
    base = base ? base + 1 : device;

    char syspath[320];
    snprintf(syspath, sizeof(syspath),
             "/sys/class/hidraw/%s/device/report_descriptor", base);

    FILE *f = fopen(syspath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) { fclose(f); return -1; }

    unsigned char *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    int found_x = 0, found_y = 0;
    int cur_report_id = 0;
    unsigned int cur_usage_page = 0;
    int cur_logical_max = 0;
    int pos = 0;

    while (pos < sz) {
        unsigned char item = buf[pos];
        int bSize = item & 0x03;
        int bType = (item >> 2) & 0x03;
        int bTag  = (item >> 4) & 0x0f;
        if (bSize == 3) bSize = 4;

        int data = 0;
        pos++;
        for (int i = 0; i < bSize && pos + i < sz; i++)
            data |= (int)buf[pos + i] << (i * 8);
        pos += bSize;

        /* only care about Global items for our state tracking */
        if (bType == 1) { /* Global */
            switch (bTag) {
            case 0: /* Usage Page */
                cur_usage_page = (unsigned int)data;
                break;
            case 2: /* Logical Maximum */
                cur_logical_max = (bSize == 1) ? (signed char)data :
                                  (bSize == 2) ? (short)data : data;
                break;
            case 8: /* Report ID */
                cur_report_id = data;
                break;
            }
        } else if (bType == 2 && bTag == 0) { /* Local – Usage */
            if (cur_report_id == 4 && cur_usage_page == 0x01) {
                if (data == 0x30 && !found_x) {
                    *max_x  = cur_logical_max;
                    found_x = 1;
                } else if (data == 0x31 && !found_y) {
                    *max_y  = cur_logical_max;
                    found_y = 1;
                }
            }
        }

        if (found_x && found_y) break;
    }

    free(buf);
    return (found_x && found_y) ? 0 : -1;
}


/*
 * haptic_open_input  —  open hidraw device for reading input reports.
 *
 * Opens the device read-only.  The caller is responsible for closing
 * the returned fd.
 */
int
haptic_open_input(const char *device)
{
    int fd = open(device, O_RDONLY);
    if (fd < 0)
        perror("haptic_open_input");
    return fd;
}


/*
 * haptic_read_touch  —  read one Report 0x04 from the hidraw device.
 *
 * Blocks until a 0x04 report arrives (0x01 mouse reports are skipped).
 *
 * Report 0x04 layout (Goodix GXTP5100 — decoded from HID report descriptor):
 *
 *   Offset  Size   Field
 *   ------  ----   -----
 *    0      1      Report ID = 0x04
 *
 *   Contacts 0–4 at offsets 1, 8, 15, 22, 29  (7 bytes each):
 *     +0    1      Flags: [Confidence(bit0) | Tip(bit1) | Reserved(bit2-3) | ContactID(bits4-7)]
 *     +1    2      X position  (uint16 LE, Usage 0x30, logical 0–4149)
 *     +3    2      Y position  (uint16 LE, Usage 0x31, logical 0–2147)
 *     +5    2      Tip Pressure (uint16 LE, Usage 0x30 on Digitizer page, 0–2000)
 *
 *   36      2      Scan Time    (uint16 LE, Usage 0x56)
 *   38      1      Contact Count (uint8,     Usage 0x54)
 *   39      1      Button + padding (bit 0 = ClickPad, bits 1–7 const)
 *
 *   Total: 40 bytes.
 *
 * Returns 0 on success, -1 on error / device closed.
 */
int
haptic_read_touch(int fd, TouchReport *report)
{
    unsigned char buf[64];
    ssize_t n;

    memset(report, 0, sizeof(*report));

    for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0)
            return -1;
        if (n < 40)
            continue; /* need full report */

        /* only parse Precision Touchpad reports; skip mouse (0x01) */
        if (buf[0] != 0x04)
            continue;

        /* --- global fields at the END of the report --- */
        report->scan_time     = buf[36] | (buf[37] << 8);
        report->contact_count = buf[38];
        report->button        = buf[39] & 0x01;

        /* --- 5 contacts at the BEGINNING of the report --- */
        int offset = 1;
        for (int i = 0; i < TOUCH_MAX_FINGERS; i++) {
            unsigned char flags = buf[offset];

            /* descriptor order: Confidence(0x47) → bit 0, Tip(0x42) → bit 1 */
            report->fingers[i].confidence =  flags       & 0x01;
            report->fingers[i].tip        = (flags >> 1) & 0x01;
            report->fingers[i].contact_id = (flags >> 4) & 0x0F;

            report->fingers[i].x = buf[offset + 1]
                                 | (buf[offset + 2] << 8);
            report->fingers[i].y = buf[offset + 3]
                                 | (buf[offset + 4] << 8);
            report->fingers[i].pressure = buf[offset + 5]
                                        | (buf[offset + 6] << 8);

            offset += 7;
        }

        return 0;
    }
}
