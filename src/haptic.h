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
