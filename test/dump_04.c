/*
 * dump_04.c — Read and hex-dump Report 0x04 input reports from hidraw.
 *
 * Build:  gcc -o dump_04 dump_04.c
 * Usage:  sudo ./dump_04 [/dev/hidraw0] [count]
 *
 * Prints raw hex bytes for each 0x04 report received.
 * Touch the pad with 1–2 fingers while running to see the pattern.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    const char *device = "/dev/hidraw0";
    int count = 20;

    if (argc > 1) device = argv[1];
    if (argc > 2) count  = atoi(argv[2]);

    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Reading from %s (press Ctrl-C to stop early)\n\n", device);
    printf("%-5s  %-4s  %s\n", "#", "Sz", "Raw hex bytes");
    printf("%-5s  %-4s  %s\n", "-----", "----", "--------------");

    for (int i = 0; i < count; i++) {
        unsigned char buf[128];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            perror("read");
            break;
        }

        /* only show report 0x04 (skip 0x01 mouse reports) */
        if (buf[0] != 0x04)
            continue;

        printf("%-5d  %-4zd  ", i, n);
        for (ssize_t j = 0; j < n && j < 64; j++) {
            printf("%02x ", buf[j]);
            if ((j + 1) % 16 == 0 && j + 1 < n)
                printf("\n                ");
        }
        printf("\n");
    }

    close(fd);
    return 0;
}
