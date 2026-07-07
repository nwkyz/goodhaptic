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
    int  persist;   /* 0 or 1 */
    int  stepless;  /* 0=presets, 1=slider */
} GoodhapticConfig;

/* Read config from CONFIG_PATH.  If the file doesn't exist, fill in
 * defaults: empty device, strength=50, persist=1. */
void config_load(GoodhapticConfig *cfg);

/* Write current config to CONFIG_PATH. */
void config_save(const GoodhapticConfig *cfg);
