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
    cfg->threshold = 2;
    cfg->inputmode         = 3;   /* default: PTP */
    cfg->selective_surface = 1;   /* default: on */
    cfg->selective_button  = 1;   /* default: on */
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
        } else if (strncmp(line, "threshold ", 10) == 0) {
            cfg->threshold = atoi(line + 10);
            if (cfg->threshold < 1) cfg->threshold = 1;
            if (cfg->threshold > 3) cfg->threshold = 3;
        } else if (strncmp(line, "persist ", 8) == 0) {
            cfg->persist = atoi(line + 8) ? 1 : 0;
        } else if (strncmp(line, "stepless ", 9) == 0) {
            cfg->stepless = atoi(line + 9) ? 1 : 0;
        } else if (strncmp(line, "selective_surface ", 18) == 0) {
            cfg->selective_surface = atoi(line + 18) ? 1 : 0;
        } else if (strncmp(line, "selective_button ", 17) == 0) {
            cfg->selective_button = atoi(line + 17) ? 1 : 0;
        } else if (strncmp(line, "inputmode ", 10) == 0) {
            cfg->inputmode = atoi(line + 10);
            if (cfg->inputmode != 0 && cfg->inputmode != 3)
                cfg->inputmode = 3;
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
    fprintf(f, "threshold %d\n", cfg->threshold);
    fprintf(f, "inputmode %d\n", cfg->inputmode);
    fprintf(f, "selective_surface %d\n", cfg->selective_surface);
    fprintf(f, "selective_button %d\n", cfg->selective_button);
    fprintf(f, "persist %d\n", cfg->persist);
    fprintf(f, "stepless %d\n", cfg->stepless);
    fclose(f);
}
