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

#pragma once

typedef struct {
    char *name;   /* display name, e.g. "GXTP5100:00 27C6:01E7"  */
    char *path;   /* device node,  e.g. "/dev/hidraw0"           */
} HapticDevice;

/* scan /sys/class/hidraw for HID devices, returns count */
int  haptic_scan(HapticDevice **devices);
void haptic_scan_free(HapticDevice *devices, int n);

/* send HID feature report 0x09 to the given hidraw device */
int  haptic_set(const char *device, int value);

/* try to read current strength via GET_FEATURE report 0x09.
 * returns 0–100 on success, -1 if not supported. */
int  haptic_get(const char *device);

/* set click force threshold via feature report 0x08.
 * value: 1-3 (1=light ~110g, 2=medium ~150g, 3=firm ~190g) */
int  haptic_set_threshold(const char *device, int value);

/* read device capability via feature report 0x02.
 * on success fills *contact_max (max finger count) and *pad_type.
 * returns 0 on success, -1 on failure. */
int  haptic_get_capability(const char *device, int *contact_max, int *pad_type);

/* ---- touch / input report (0x04) ---------------------------------- */

#define TOUCH_MAX_FINGERS 5

typedef struct {
    int tip;          /* Tip Switch (Usage 0x42): finger is touching      */
    int confidence;   /* Confidence (Usage 0x47): touch data is valid     */
    int contact_id;   /* Contact ID (Usage 0x51): 0–15                   */
    int x;            /* X position, logical range 0–4149                */
    int y;            /* Y position, logical range 0–2147                */
    int pressure;     /* Tip Pressure, range 0–2000                      */
} TouchFinger;

typedef struct {
    int scan_time;                         /* Scan Time (Usage 0x56)      */
    int contact_count;                     /* Contact Count (Usage 0x54)  */
    int button;                            /* Button 1 (ClickPad)         */
    TouchFinger fingers[TOUCH_MAX_FINGERS];
} TouchReport;

/* open hidraw device for reading input reports (blocking read).
 * returns fd on success, -1 on error. */
/* read max X / max Y from the HID report descriptor for report 0x04.
 * fills *max_x and *max_y on success, returns 0.  returns -1 on failure. */
int  haptic_get_resolution(const char *device, int *max_x, int *max_y);

int  haptic_open_input(const char *device);

/* read one touch report from fd.  filters out non-0x04 reports
 * (e.g. 0x01 mouse) automatically.  returns 0 on success, -1 on
 * error or if the device was closed. */
int  haptic_read_touch(int fd, TouchReport *report);
