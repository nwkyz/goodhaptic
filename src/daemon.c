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
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#define SOCK_PATH "/run/goodhaptic/sock"
#define BACKLOG    8

static int running = 1;

static void sig_handler(int sig) { running = 0; }

/* Apply saved config to hardware (if persist is on). */
static void apply_config(const GoodhapticConfig *cfg)
{
    if (!cfg->persist || !cfg->device[0])
        return;
    if (haptic_set(cfg->device, cfg->strength) < 0)
        fprintf(stderr, "apply: %s value=%d failed\n",
                cfg->device, cfg->strength);
}

/*
 * Process one command line.  Returns a static response string.
 *
 *   STRENGTH <0-100>   → haptic_set + config_save
 *   PERSIST=0|1        → config_save
 *   DEVICE=<path>      → config_save
 *
 * The device argument is the device to write to on STRENGTH commands
 * (taken from config, updated on DEVICE=).
 */
static const char *
handle_command(const char *line, const char **device, GoodhapticConfig *cfg)
{
    /* strip trailing whitespace */
    char buf[512];
    char *end = NULL;

    snprintf(buf, sizeof(buf), "%s", line);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';
    if (len == 0)
        return "OK\n";

    /* STRENGTH <n> */
    if (strncmp(buf, "STRENGTH ", 9) == 0) {
        long val = strtol(buf + 9, &end, 10);
        if (end > buf + 9 && *end == '\0' && val >= 0 && val <= 100) {
            cfg->strength = (int)val;
            if (haptic_set(*device, cfg->strength) < 0)
                return "ERR write failed\n";
            config_save(cfg);
            return "OK\n";
        }
        return "ERR bad value\n";
    }

    /* PERSIST=0|1 */
    if (strncmp(buf, "PERSIST=", 8) == 0) {
        cfg->persist = atoi(buf + 8) ? 1 : 0;
        config_save(cfg);
        return "OK\n";
    }

    /* DEVICE=<path> */
    if (strncmp(buf, "DEVICE=", 7) == 0) {
        strncpy(cfg->device, buf + 7, sizeof(cfg->device) - 1);
        cfg->device[sizeof(cfg->device) - 1] = '\0';
        *device = cfg->device;
        config_save(cfg);
        return "OK\n";
    }

    /* STEPLESS=0|1 */
    if (strncmp(buf, "STEPLESS=", 9) == 0) {
        cfg->stepless = atoi(buf + 9) ? 1 : 0;
        config_save(cfg);
        return "OK\n";
    }

    return "ERR unknown command\n";
}

int main(void)
{
    GoodhapticConfig cfg;
    config_load(&cfg);

    /* auto-detect first device if none saved */
    if (!cfg.device[0]) {
        HapticDevice *devs = NULL;
        int n = haptic_scan(&devs);
        if (n > 0) {
            snprintf(cfg.device, sizeof(cfg.device), "%s", devs[0].path);
        }
        haptic_scan_free(devs, n);
    }

    /* create config file with defaults on first run */
    if (access(CONFIG_PATH, F_OK) != 0)
        config_save(&cfg);

    const char *device = cfg.device;

    /* boot-time restore */
    apply_config(&cfg);

    /* unlink stale socket */
    unlink(SOCK_PATH);

    /* ensure runtime directory exists */
    mkdir("/run/goodhaptic", 0755);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv);
        return 1;
    }

    /* world-writable so GUI can connect without auth */
    chmod(SOCK_PATH, 0666);

    if (listen(srv, BACKLOG) < 0) {
        perror("listen");
        close(srv);
        return 1;
    }

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sig_handler;
        /* no SA_RESTART — accept() returns EINTR on SIGTERM */
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT,  &sa, NULL);
    }

    while (running) {
        int cli = accept(srv, NULL, NULL);
        if (cli < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        /* read and handle one command per connection */
        char buf[512];
        ssize_t n = recv(cli, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            const char *resp = handle_command(buf, &device, &cfg);
            send(cli, resp, strlen(resp), 0);
        }

        close(cli);
    }

    close(srv);
    unlink(SOCK_PATH);
    return 0;
}
