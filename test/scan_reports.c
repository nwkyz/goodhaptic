/*
 * scan_reports.c — Systematically test which Goodix HID reports are
 *                  readable via GET_FEATURE.
 *
 * Build:  gcc -o scan_reports scan_reports.c
 * Usage:  sudo ./scan_reports [device]
 *
 * Output legend:
 *   OK = GET_FEATURE succeeded (shows raw bytes)
 *   -- = GET_FEATURE failed (EINVAL, device does not support read)
 *
 * SAFE: only reads, never writes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

typedef struct {
    int  id;
    int  data_bytes;
    const char *name;
} ReportInfo;

static const ReportInfo reports[] = {
    {0x02,    1, "Device capability"},
    {0x03,    2, "Digitizer config"},
    {0x05,    2, "Surface/Button config"},
    {0x06,  256, "Vendor command buffer"},
    {0x07,    2, "Unknown (broken)"},
    {0x08,    1, "Click force threshold"},
    {0x09,    1, "Haptic intensity"},
    {0x0B,   66, "Vendor status"},
    {0x0C,  736, "Vendor large config"},
    {0x0D,    4, "Vendor firmware info"},
    {0, 0, NULL}
};

int main(int argc, char *argv[])
{
    const char *device = "/dev/hidraw0";
    if (argc > 1) device = argv[1];

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: cannot open %s (need root?)\n", device);
        return 1;
    }

    printf("Device: %s\n\n", device);
    printf("  %-4s  %-30s  %s\n", "ID", "Name", "Read");
    printf("  %-4s  %-30s  %s\n", "----", "------------------------------", "----");

    for (const ReportInfo *r = reports; r->name; r++) {
        printf("0x%02x  %-30s  ", r->id, r->name);

        int total = r->data_bytes + 1;
        unsigned char *buf = calloc(1, total);
        buf[0] = (unsigned char)r->id;

        if (ioctl(fd, HIDIOCGFEATURE(total), buf) >= 0) {
            /* print first 8 bytes max */
            int show = total < 8 ? total : 8;
            printf("OK  [");
            for (int i = 0; i < show; i++) {
                printf("0x%02x", buf[i]);
                if (i < show - 1) printf(", ");
            }
            if (total > 8) printf(", ...");
            printf("]");
        } else {
            printf("--  (not supported)");
        }
        printf("\n");

        free(buf);
    }

    close(fd);
    return 0;
}
