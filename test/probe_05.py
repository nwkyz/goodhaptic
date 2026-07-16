#!/usr/bin/env python3
"""
probe_05.py — Probe Goodix HID Report 0x05 (Surface/Button Config)

Report 0x05 decoded from HID descriptor:
  Usage Page:    Digitizers (0x0D)
  Usage:         0x57, 0x58
  Logical Min:   0, Logical Max: 3
  Report Size:   1 bit × 2 fields + 14 bits const = 2 data bytes
  Buffer:        3 bytes [report_id, byte0, byte1]
  Type:          Feature (Data, Var, Abs)

Usage:
  python3 probe_05.py [device]   read current config
"""

import sys, os, fcntl

_IOC_WRITE = 1
_IOC_READ  = 2
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = 8
_IOC_SIZESHIFT = 16
_IOC_DIRSHIFT = 30

def _IOC(dir_, type_, nr, size):
    return (dir_ << _IOC_DIRSHIFT) | (ord(type_) << _IOC_TYPESHIFT) | \
           (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)

def HIDIOCGFEATURE(sz):
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x07, sz)

REPORT_ID = 0x05

def main():
    device = "/dev/hidraw0"
    if len(sys.argv) > 1:
        device = sys.argv[1]

    if not os.path.exists(device):
        print(f"ERROR: {device} not found"); sys.exit(1)

    print(f"Device:  {device}")
    print(f"Report:  0x{REPORT_ID:02x} (Surface/Button Config)")
    print()

    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, 0, 0])
        try:
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
        except OSError as e:
            print(f"  GET_FEATURE failed: {e}")
            sys.exit(1)

        raw_a = buf[1] & 0x01
        raw_b = (buf[1] >> 1) & 0x01
        print(f"  Raw:       [{buf[0]:#04x}, {buf[1]:#04x}, {buf[2]:#04x}]")
        print(f"  Usage 0x57:  {raw_a}  (range 0-3)")
        print(f"  Usage 0x58:  {raw_b}  (range 0-3)")
    finally:
        os.close(fd)

if __name__ == "__main__":
    main()
