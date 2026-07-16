#!/usr/bin/env python3
"""
probe_02.py — Probe Goodix HID Report 0x02 (Device Capability)

Report 0x02 decoded from HID descriptor:
  Usage Page:    Digitizers (0x0D)
  Usage:         0x55 (Contact Count Maximum)
  Usage:         0x59 (Pad Type)
  Report Size:   4 bits × 2
  Logical Max:   15
  Type:          Feature (Data, Var, Abs)

Pad Type values:
  0 = Touchpad
  1 = Clickpad
  2 = Precision Touchpad (PTP)

Usage:
  python3 probe_02.py [device]   read device capabilities
"""

import sys, os, fcntl, ctypes

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = 8
_IOC_SIZESHIFT = 16
_IOC_DIRSHIFT = 30

def _IOC(dir_, type_, nr, size):
    return (dir_ << _IOC_DIRSHIFT) | (ord(type_) << _IOC_TYPESHIFT) | \
           (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)

def HIDIOCGFEATURE(sz):
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x07, sz)

REPORT_ID = 0x02
PAD_TYPES = {0: "Touchpad", 1: "Clickpad", 2: "Precision Touchpad (PTP)"}

def main():
    device = "/dev/hidraw0"
    if len(sys.argv) > 1:
        device = sys.argv[1]

    if not os.path.exists(device):
        print(f"ERROR: {device} not found"); sys.exit(1)

    print(f"Device:  {device}")
    print(f"Report:  0x{REPORT_ID:02x} (Device Capability)")
    print()

    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, 0])
        try:
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
        except OSError as e:
            print(f"  GET_FEATURE failed: {e}")
            sys.exit(1)

        raw = buf[1]
        contact_max = raw & 0x0F
        pad_type = (raw >> 4) & 0x0F

        print(f"  Raw byte:  0x{raw:02x}")
        print(f"  Contact Max:  {contact_max}  (maximum finger count)")
        print(f"  Pad Type:     {pad_type}  ({PAD_TYPES.get(pad_type, 'unknown')})")
    finally:
        os.close(fd)

if __name__ == "__main__":
    main()
