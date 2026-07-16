/*
 * probe_08.c — Probe Goodix HID Report 0x08 (Click Force Threshold)
 *
 * Report 0x08 decoded from HID descriptor:
 *   Usage Page:    Digitizers (0x0D)
 *   Usage:         0x00B0
 *   Physical Min:  110 g  (force)
 *   Physical Max:  190 g  (force)
 *   Logical Min:   1
 *   Logical Max:   3
 *   Report Size:   2 bits (Data) + 6 bits (Const padding)
 *   Type:          Feature (Data, Var, Abs)
 *
 * Interpretation: click actuation force threshold (3 levels):
 *   1 → lightest  (~110 g press triggers click feedback)
 *   2 → medium    (~150 g)
 *   3 → firmest   (~190 g)
 *
 * NOTE: GET_FEATURE (read) is NOT supported on this device for report 0x08
 * (same limitation as report 0x09). Only SET_FEATURE (write) works.
 *
 * Build:
 *   gcc -o probe_08 probe_08.c
 *
 * Usage:
 *   ./probe_08 [device]              attempt read (likely unsupported)
 *   ./probe_08 [device] set <1|2|3>  write new value
 *   ./probe_08 [device] try-all      cycle through 1→2→3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#define REPORT_ID  0x08

static const char *threshold_label(int v)
{
    switch (v) {
    case 1:  return "~110 g — lightest press triggers click";
    case 2:  return "~150 g — medium";
    case 3:  return "~190 g — firmest press needed";
    default: return "unknown";
    }
}

/*
 * Try to read Report 0x08 via GET_FEATURE.
 * Likely returns -1 because the device does not support readback.
 */
static int report_08_read(const char *device)
{
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "  open(%s): %s\n", device, strerror(errno));
        return -1;
    }

    unsigned char buf[2] = {REPORT_ID, 0};
    int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
    close(fd);

    if (ret < 0) {
        fprintf(stderr, "  GET_FEATURE not supported for Report 0x%02x "
                        "(hardware does not allow readback)\n", REPORT_ID);
        return -1;
    }

    int value = buf[1] & 0x03;
    printf("  Raw: [0x%02x, 0x%02x]\n", buf[0], buf[1]);
    return value;
}

/*
 * Write value (1-3) to Report 0x08 via SET_FEATURE.
 * Returns 0 on success, -1 on failure.
 */
static int report_08_write(const char *device, int value)
{
    if (value < 1 || value > 3) {
        fprintf(stderr, "  ERROR: value must be 1-3, got %d\n", value);
        return -1;
    }

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "  open(%s): %s\n", device, strerror(errno));
        return -1;
    }

    unsigned char buf[2] = {REPORT_ID, value & 0x03};
    int ret = ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf);
    close(fd);

    if (ret < 0) {
        fprintf(stderr, "  SET_FEATURE failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *device = "/dev/hidraw0";
    int argi = 1;

    /* Parse optional device argument */
    if (argi < argc &&
        strcmp(argv[argi], "set") != 0 &&
        strcmp(argv[argi], "try-all") != 0) {
        device = argv[argi++];
    }

    printf("Device:  %s\n", device);
    printf("Report:  0x%02x (Click Force Threshold)\n", REPORT_ID);
    printf("\n");

    if (argi < argc && strcmp(argv[argi], "set") == 0) {
        argi++;
        if (argi >= argc) {
            fprintf(stderr, "Usage: %s [device] set <1|2|3>\n", argv[0]);
            return 1;
        }
        int new_val = atoi(argv[argi]);

        printf("=== Write value=%d (%s) ===\n",
               new_val, threshold_label(new_val));

        if (report_08_write(device, new_val) == 0) {
            printf("  SUCCESS: wrote %d to Report 0x08\n", new_val);
            printf("\n");
            printf("  Now press the touchpad — the click feedback threshold\n");
            printf("  should feel %s.\n",
                   new_val == 1 ? "lighter" : new_val == 3 ? "heavier" : "medium");
            printf("  Try comparing with other values to confirm the effect.\n");
        } else {
            printf("  FAILED: could not write to Report 0x08\n");
            return 1;
        }

    } else if (argi < argc && strcmp(argv[argi], "try-all") == 0) {
        printf("=== Cycling through all 3 levels ===\n");
        for (int v = 1; v <= 3; v++) {
            printf("\n--- Setting level %d (%s) ---\n",
                   v, threshold_label(v));
            if (report_08_write(device, v) == 0) {
                printf("  Wrote %d — press the touchpad now to feel this level.\n", v);
                if (v < 3) {
                    printf("  Press Enter to try level %d...", v + 1);
                    getchar();
                }
            } else {
                printf("  FAILED at level %d\n", v);
                break;
            }
        }
        printf("\n=== Done. Which level felt best? ===\n");

    } else {
        /* Read-only mode */
        printf("=== Read Report 0x08 ===\n");
        int val = report_08_read(device);
        if (val >= 0) {
            printf("  Current value: %d  (%s)\n",
                   val, threshold_label(val));
        }
        printf("\n");
        printf("  Commands:\n");
        printf("    sudo test/probe_08 set 1    # lightest click\n");
        printf("    sudo test/probe_08 set 2    # medium click\n");
        printf("    sudo test/probe_08 set 3    # firmest click\n");
        printf("    sudo test/probe_08 try-all  # compare all levels\n");
    }

    return 0;
}
