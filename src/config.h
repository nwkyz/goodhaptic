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

#define CONFIG_PATH "/etc/goodhaptic.conf"

typedef struct {
    char device[256];
    int  strength;
    int  threshold;  /* click force threshold: 1=light, 2=medium, 3=firm */
    int  inputmode;          /* input mode: 3=PTP (multi-touch), 0=Mouse (legacy) */
    int  selective_surface;  /* selective reporting: 0=off, 1=on */
    int  selective_button;   /* selective reporting: 0=off, 1=on */
    int  persist;    /* 0 or 1 */
    int  stepless;   /* 0=presets, 1=slider */
} GoodhapticConfig;

/* Read config from CONFIG_PATH.  If the file doesn't exist, fill in
 * defaults: empty device, strength=50, threshold=2, persist=1. */
void config_load(GoodhapticConfig *cfg);

/* Write current config to CONFIG_PATH. */
void config_save(const GoodhapticConfig *cfg);
