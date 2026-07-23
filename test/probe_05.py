#!/usr/bin/env python3
"""
probe_05.py — Probe Goodix HID Report 0x05 (Selective Reporting)

Report 0x05 decoded from HID descriptor (offset 0x2D0):
  Usage Page:    Digitizers (0x0D)
  Usage:         0x57 (Surface Switch), 0x58 (Button Switch)
  Logical Min:   0, Logical Max: 3 (per field)
  Report Size:   1 bit × 2 fields + 14 bits const = 2 data bytes
  Buffer:        3 bytes [report_id, data_byte, padding]
  Type:          Feature (Data, Var, Abs)

  Bit layout of data_byte:
    bit 0 = Surface Switch (0x57): 0=off, 1=on
    bit 1 = Button Switch  (0x58): 0=off, 1=on
    bits 2-7 = constant padding

  Effect matrix:
    Surface=0 Button=0 → no input at all
    Surface=0 Button=1 → buttons only, no touch
    Surface=1 Button=0 → touch only, no buttons
    Surface=1 Button=1 → both (default)

Usage:
  python3 probe_05.py [device]            read current config
  python3 probe_05.py [device] write <surface> <button>
    surface: 0=off, 1=on
    button:  0=off, 1=on
    e.g.: probe_05.py write 1 1  → default (both on)
          probe_05.py write 0 0  → silent (no input)
          probe_05.py write 1 0  → touch only, no buttons
          probe_05.py write 0 1  → buttons only, no touch
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

def HIDIOCSFEATURE(sz):
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x06, sz)

REPORT_ID = 0x05


def read_config(device):
    """Read Report 0x05 via GET_FEATURE."""
    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, 0, 0])
        try:
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
            surface = buf[1] & 0x01
            button  = (buf[1] >> 1) & 0x01
            print(f"  Raw:       [{buf[0]:#04x}, {buf[1]:#04x}, {buf[2]:#04x}]")
            print(f"  Surface Switch (0x57): {surface}  ({'ON' if surface else 'OFF'})")
            print(f"  Button Switch  (0x58): {button}  ({'ON' if button else 'OFF'})")

            if surface == 0 and button == 0:
                print(f"  → Silent mode (no input)")
            elif surface == 0 and button == 1:
                print(f"  → Button only, no touch")
            elif surface == 1 and button == 0:
                print(f"  → Touch only, no buttons")
            else:
                print(f"  → Default (both on)")

            return surface, button
        except OSError as e:
            print(f"  GET_FEATURE failed: {e}")
            return None
    finally:
        os.close(fd)


def write_config(device, surface, button):
    """Write Report 0x05 via SET_FEATURE."""
    if surface not in (0, 1) or button not in (0, 1):
        print(f"  ERROR: surface and button must be 0 or 1")
        return False

    fd = os.open(device, os.O_RDWR)
    try:
        data_byte = (button << 1) | surface
        buf = bytearray([REPORT_ID, data_byte, 0])  # 3 bytes: id, data, padding
        try:
            fcntl.ioctl(fd, HIDIOCSFEATURE(len(buf)), buf)
            print(f"  SET_FEATURE: OK")
            print(f"  Data byte:  0x{data_byte:02x} (Surface={surface}, Button={button})")
            return True
        except OSError as e:
            print(f"  SET_FEATURE failed: {e}")
            return False
    finally:
        os.close(fd)


def main():
    device = "/dev/hidraw0"
    args = sys.argv[1:]

    if args and args[0] not in ("write",):
        device = args[0]
        args = args[1:]

    if not os.path.exists(device):
        print(f"ERROR: {device} not found"); sys.exit(1)

    print(f"Device:  {device}")
    print(f"Report:  0x{REPORT_ID:02x} (Selective Reporting: Surface + Button Switch)")
    print()

    if args and args[0] == "write":
        if len(args) < 3:
            print("Usage: probe_05.py [device] write <surface> <button>")
            print("  surface: 0=off, 1=on")
            print("  button:  0=off, 1=on")
            sys.exit(1)
        surface = int(args[1])
        button  = int(args[2])
        print(f"=== Write: Surface={surface}, Button={button} ===")
        write_config(device, surface, button)
    else:
        print("=== Read Report 0x05 ===")
        read_config(device)
        print()
        print("  Write: probe_05.py write <surface> <button>")
        print("         surface=0/1  button=0/1")


if __name__ == "__main__":
    main()
