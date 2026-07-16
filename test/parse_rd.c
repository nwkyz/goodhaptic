/*
 * parse_rd.c — Read and dump HID Report Descriptor for a hidraw device.
 *
 * Build:  gcc -o parse_rd parse_rd.c
 * Usage:  ./parse_rd [/dev/hidraw0]
 *
 * Extracts report size and field layout from the HID report descriptor
 * using the hidraw ioctl.  Parses the raw descriptor bytes to show
 * Report ID, Report Size, Report Count, Usage, Logical Min/Max for
 * each field, grouped by report ID.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

/* ---- minimal HID descriptor parser --------------------------------- */

typedef struct {
    unsigned char *buf;
    int            size;
    int            pos;
} RDParser;

/* Global state items for the parser */
static unsigned int  usage_page   = 0;
static unsigned int  report_size  = 0;
static unsigned int  report_count = 0;
static int           logical_min  = 0;
static int           logical_max  = 0;
static int           report_id    = 0;
static int           in_report    = 0;  /* 1=input, 2=output, 3=feature */
static int           is_collection = 0;

static int  item_unsigned(RDParser *p, int sz);
static int  item_signed(RDParser *p, int sz);

static int
item_unsigned(RDParser *p, int sz)
{
    unsigned int v = 0;
    for (int i = 0; i < sz && p->pos + i < p->size; i++)
        v |= (unsigned int)p->buf[p->pos + i] << (i * 8);
    p->pos += sz;
    return (int)v;
}

static int
item_signed(RDParser *p, int sz)
{
    int v = item_unsigned(p, sz);
    if (sz == 1) v = (signed char)v;
    else if (sz == 2) v = (short)v;
    return v;
}

typedef struct {
    int id;
    int bit_offset;
    int bit_size;
    int count;
    int logical_min;
    int logical_max;
    unsigned int usage_page;
    unsigned int usage;
    const char *type;  /* Input, Output, Feature */
} ReportField;

static ReportField fields[128];
static int nfields = 0;

int main(int argc, char *argv[])
{
    const char *device = "/dev/hidraw0";
    if (argc > 1) device = argv[1];

    int fd = open(device, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    /* get descriptor size */
    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0) {
        /* fallback: read from sysfs */
        close(fd);
        char syspath[320];
        snprintf(syspath, sizeof(syspath),
                 "/sys/class/hidraw/%s/device/report_descriptor",
                 strrchr(device, '/') ? strrchr(device, '/') + 1 : device);
        FILE *f = fopen(syspath, "rb");
        if (!f) { perror("open report_descriptor"); return 1; }
        fseek(f, 0, SEEK_END);
        desc_size = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *buf = malloc(desc_size);
        fread(buf, 1, desc_size, f);
        fclose(f);

        printf("Report Descriptor (%d bytes) from sysfs\n\n", desc_size);

        /* simple parse */
        int pos = 0;
        int bit_offset = 0;
        int current_report_id = 0;
        int current_type = 0;  /* 1=input, 2=output, 3=feature */

        while (pos < desc_size) {
            unsigned char item = buf[pos];
            int bSize = item & 0x03;
            int bType = (item >> 2) & 0x03;
            int bTag  = (item >> 4) & 0x0f;

            if (bSize == 3) bSize = 4;
            int data_size = bSize;
            pos++;

            int data = 0;
            for (int i = 0; i < data_size && pos + i < desc_size; i++)
                data |= (int)buf[pos + i] << (i * 8);
            pos += data_size;

            switch (bType) {
            case 0: /* Main */
                switch (bTag) {
                case 8: /* Input */
                    current_type = 1;
                    if (data & 0x01) { /* Constant */
                        bit_offset += report_size * report_count;
                    } else {
                        for (unsigned int i = 0; i < report_count && nfields < 128; i++) {
                            fields[nfields].id = current_report_id;
                            fields[nfields].bit_offset = bit_offset;
                            fields[nfields].bit_size = report_size;
                            fields[nfields].count = 1;
                            fields[nfields].logical_min = logical_min;
                            fields[nfields].logical_max = logical_max;
                            fields[nfields].usage_page = usage_page;
                            fields[nfields].usage = 0;
                            fields[nfields].type = "Input";
                            bit_offset += report_size;
                            nfields++;
                        }
                    }
                    break;
                case 9: /* Output */
                    current_type = 2;
                    bit_offset += report_size * report_count;
                    break;
                case 10: /* Feature */
                    current_type = 3;
                    bit_offset = 0; /* new report */
                    break;
                case 11: /* Collection */
                case 12: /* End Collection */
                    break;
                }
                break;
            case 1: /* Global */
                switch (bTag) {
                case 0: /* Usage Page */
                    usage_page = (unsigned int)data;
                    break;
                case 1: /* Logical Minimum */
                    logical_min = (bSize == 1) ? (signed char)data :
                                  (bSize == 2) ? (short)data : data;
                    break;
                case 2: /* Logical Maximum */
                    logical_max = (bSize == 1) ? (signed char)data :
                                  (bSize == 2) ? (short)data : data;
                    break;
                case 3: /* Physical Minimum */ break;
                case 4: /* Physical Maximum */ break;
                case 7: /* Report Size */
                    report_size = (unsigned int)data;
                    break;
                case 8: /* Report ID */
                    current_report_id = data;
                    bit_offset = 0;
                    break;
                case 9: /* Report Count */
                    report_count = (unsigned int)data;
                    break;
                }
                break;
            case 2: /* Local */
                switch (bTag) {
                case 0: /* Usage */
                    if (nfields > 0 && fields[nfields-1].usage == 0)
                        fields[nfields-1].usage = (unsigned int)data;
                    break;
                }
                break;
            }
        }

        /* print fields grouped by report ID */
        int last_id = -1;
        int total_bits[16] = {0};
        for (int i = 0; i < nfields; i++) {
            if (fields[i].id != last_id) {
                if (last_id >= 0)
                    printf("  --> total: %d bits (%d bytes)\n\n",
                           total_bits[last_id], (total_bits[last_id] + 7) / 8);
                last_id = fields[i].id;
                printf("--- Report 0x%02x (%s) ---\n", fields[i].id, fields[i].type);
            }
            printf("  bit %3d  sz %2d  pg 0x%04x  usage 0x%04x  "
                   "log %d..%d\n",
                   fields[i].bit_offset, fields[i].bit_size,
                   fields[i].usage_page, fields[i].usage,
                   fields[i].logical_min, fields[i].logical_max);
            total_bits[fields[i].id] = fields[i].bit_offset + fields[i].bit_size;
        }
        if (last_id >= 0)
            printf("  --> total: %d bits (%d bytes)\n\n",
                   total_bits[last_id], (total_bits[last_id] + 7) / 8);

        free(buf);
    } else {
        /* use ioctl */
        unsigned char *buf = malloc(desc_size);
        if (ioctl(fd, HIDIOCGRDESC, buf) < 0) {
            perror("HIDIOCGRDESC");
            free(buf);
            close(fd);
            return 1;
        }
        /* same parser logic */
        /* ... abbreviated for now ... */
        printf("Descriptor size: %d bytes (ioctl)\n", desc_size);
        free(buf);
    }

    close(fd);
    return 0;
}
