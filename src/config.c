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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
config_load(GoodhapticConfig *cfg)
{
    cfg->device[0] = '\0';
    cfg->strength  = 50;
    cfg->persist   = 1;
    cfg->stepless  = 0;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f)
        return;

    char line[320];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, "device ", 7) == 0) {
            snprintf(cfg->device, sizeof(cfg->device), "%s", line + 7);
        } else if (strncmp(line, "strength ", 9) == 0) {
            cfg->strength = atoi(line + 9);
        } else if (strncmp(line, "persist ", 8) == 0) {
            cfg->persist = atoi(line + 8) ? 1 : 0;
        } else if (strncmp(line, "stepless ", 9) == 0) {
            cfg->stepless = atoi(line + 9) ? 1 : 0;
        }
    }

    fclose(f);
}

void
config_save(const GoodhapticConfig *cfg)
{
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        perror("config_save: fopen");
        return;
    }

    fprintf(f, "device %s\n", cfg->device);
    fprintf(f, "strength %d\n", cfg->strength);
    fprintf(f, "persist %d\n", cfg->persist);
    fprintf(f, "stepless %d\n", cfg->stepless);
    fclose(f);
}
