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
#include <sys/select.h>
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
        fprintf(stderr, "apply: %s strength=%d failed\n",
                cfg->device, cfg->strength);
    if (haptic_set_threshold(cfg->device, cfg->threshold) < 0)
        fprintf(stderr, "apply: %s threshold=%d failed\n",
                cfg->device, cfg->threshold);
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
/*
 * Process one command line.  Returns a static response string, or NULL
 * when the command has already handled the response itself (streaming
 * mode – currently only MONITOR).  The caller must not send anything
 * when NULL is returned, just close the socket.
 */
static const char *
handle_command(const char *line, const char **device, GoodhapticConfig *cfg,
               int cli)
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

    /* CAPABILITY — read device info from Report 0x02 */
    if (strncmp(buf, "CAPABILITY", 10) == 0 && (buf[10] == '\0' || buf[10] == '\n')) {
        int contact_max = 0, pad_type = 0;
        if (haptic_get_capability(*device, &contact_max, &pad_type) < 0)
            return "ERR read failed\n";
        static char capbuf[32];
        snprintf(capbuf, sizeof(capbuf), "OK %d %d\n", contact_max, pad_type);
        return capbuf;
    }

    /* RESOLUTION — read max X / max Y from the HID report descriptor */
    if (strncmp(buf, "RESOLUTION", 10) == 0 && (buf[10] == '\0' || buf[10] == '\n')) {
        int max_x = 0, max_y = 0;
        if (haptic_get_resolution(*device, &max_x, &max_y) < 0)
            return "ERR read failed\n";
        static char resbuf[32];
        snprintf(resbuf, sizeof(resbuf), "OK %d %d\n", max_x, max_y);
        return resbuf;
    }

    /* MONITOR — stream touch/input reports from the device.
     * Keeps the connection open and pushes parsed Report 0x04 data
     * until the client disconnects or the daemon is stopped. */
    if (strncmp(buf, "MONITOR", 7) == 0 && (buf[7] == '\0' || buf[7] == '\n')) {
        int tfd = haptic_open_input(*device);
        if (tfd < 0)
            return "ERR open input failed\n";

        /* initial OK so the client knows streaming starts */
        if (send(cli, "OK\n", 3, MSG_NOSIGNAL) < 0) {
            close(tfd);
            return NULL;
        }

        TouchReport report;
        char line[640];

        while (running) {
            /* poll with a 500 ms timeout so we can react to SIGTERM */
            fd_set fds;
            struct timeval tv = {0, 500000};
            FD_ZERO(&fds);
            FD_SET(tfd, &fds);

            int sel = select(tfd + 1, &fds, NULL, NULL, &tv);
            if (sel < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (sel == 0)
                continue; /* timeout – check running flag */

            if (haptic_read_touch(tfd, &report) < 0)
                break;

            /* serialise: T <scan> <count> <btn> [<tip> <conf> <id> <x> <y> <p>]*5 */
            int len = snprintf(line, sizeof(line),
                               "T %d %d %d",
                               report.scan_time,
                               report.contact_count,
                               report.button);

            for (int i = 0; i < TOUCH_MAX_FINGERS; i++) {
                len += snprintf(line + len, sizeof(line) - len,
                                " %d %d %d %d %d %d",
                                report.fingers[i].tip,
                                report.fingers[i].confidence,
                                report.fingers[i].contact_id,
                                report.fingers[i].x,
                                report.fingers[i].y,
                                report.fingers[i].pressure);
            }
            len += snprintf(line + len, sizeof(line) - len, "\n");

            if (send(cli, line, len, MSG_NOSIGNAL) < 0)
                break; /* client disconnected */
        }

        close(tfd);
        return NULL; /* already handled – caller must just close */
    }

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

    /* THRESHOLD <1-3> */
    if (strncmp(buf, "THRESHOLD ", 10) == 0) {
        long val = strtol(buf + 10, &end, 10);
        if (end > buf + 10 && *end == '\0' && val >= 1 && val <= 3) {
            cfg->threshold = (int)val;
            if (haptic_set_threshold(*device, cfg->threshold) < 0)
                return "ERR write failed\n";
            config_save(cfg);
            return "OK\n";
        }
        return "ERR bad value\n";
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
            const char *resp = handle_command(buf, &device, &cfg, cli);
            if (resp)
                send(cli, resp, strlen(resp), MSG_NOSIGNAL);
        }

        close(cli);
    }

    close(srv);
    unlink(SOCK_PATH);
    return 0;
}
