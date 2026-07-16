#!/usr/bin/env python3
"""
probe_08.py — Probe Goodix HID Report 0x08 (Click Force Threshold)

Report 0x08 decoded from HID descriptor:
  Usage Page:    Digitizers (0x0D)
  Usage:         0x00B0
  Physical Min:  110 g  (force)
  Physical Max:  190 g  (force)
  Logical Min:   1
  Logical Max:   3
  Report Size:   2 bits (Data) + 6 bits (Const padding)
  Type:          Feature (Data, Var, Abs)

Interpretation: click actuation force threshold (3 levels):
  1 → lightest  (~110g press triggers click feedback)
  2 → medium    (~150g)
  3 → firmest   (~190g)

Usage:
  python3 probe_08.py [device]              read current value (if supported)
  python3 probe_08.py [device] set <1|2|3>  write new value
  python3 probe_08.py [device] try-all      cycle through 1→2→3 and report

If device is omitted, defaults to /dev/hidraw0.
"""

import sys
import os
import struct
import fcntl
import ctypes

# --- HID ioctl constants ---------------------------------------------------

_IOC_NONE  = 0
_IOC_WRITE = 1
_IOC_READ  = 2

_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = 8
_IOC_SIZESHIFT = 16
_IOC_DIRSHIFT  = 30


def _IOC(dir_, type_, nr, size):
    return (dir_ << _IOC_DIRSHIFT) | (ord(type_) << _IOC_TYPESHIFT) | \
           (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)


def HIDIOCGFEATURE(size):
    # Kernel uses _IOC_WRITE|_IOC_READ (bidirectional), not _IOC_READ alone
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x07, size)


def HIDIOCSFEATURE(size):
    # Kernel uses _IOC_WRITE|_IOC_READ (bidirectional), not _IOC_WRITE alone
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x06, size)


REPORT_ID = 0x08

# ---------------------------------------------------------------------------


def report_08_read(device: str) -> int | None:
    """
    Try to read current Report 0x08 value via GET_FEATURE.
    Returns 1-3 on success, None if unsupported (device may not allow reads).
    """
    fd = os.open(device, os.O_RDWR)
    try:
        # Try with 2-byte buffer: [report_id, data_byte]
        for nbytes in (2, 1, 3):
            buf = bytearray(nbytes)
            buf[0] = REPORT_ID
            req = HIDIOCGFEATURE(len(buf))
            try:
                fcntl.ioctl(fd, req, buf)
                val = buf[1] & 0x03
                print(f"  Raw: {[hex(b) for b in buf]}  (buffer={nbytes} bytes)")
                return val
            except OSError:
                continue
        print(f"  GET_FEATURE not supported for Report 0x{REPORT_ID:02x} "
              f"(same as Report 0x09 — hardware does not allow readback)")
        return None
    finally:
        os.close(fd)


def report_08_write(device: str, value: int) -> bool:
    """
    Write value (1-3) to Report 0x08 via SET_FEATURE.
    Returns True on success.
    """
    if value < 1 or value > 3:
        print(f"  ERROR: value must be 1-3, got {value}")
        return False

    fd = os.open(device, os.O_RDWR)
    try:
        # Try exact 2-byte buffer: [report_id, data_byte]
        buf = bytearray([REPORT_ID, value & 0x03])
        req = HIDIOCSFEATURE(len(buf))
        try:
            fcntl.ioctl(fd, req, buf)
            return True
        except OSError as e:
            # Fallback: try with kernel-style struct
            buf2 = struct.pack("BB", REPORT_ID, value & 0x03)
            try:
                fcntl.ioctl(fd, req, buf2)
                return True
            except OSError as e2:
                print(f"  SET_FEATURE failed: {e2}")
                return False
    finally:
        os.close(fd)


# ---------------------------------------------------------------------------

THRESHOLDS = {1: "~110 g (lightest)", 2: "~150 g (medium)", 3: "~190 g (firmest)"}


def main():
    device = "/dev/hidraw0"
    args = sys.argv[1:]

    if args and not args[0] in ("set", "try-all"):
        device = args[0]
        args = args[1:]

    if not os.path.exists(device):
        print(f"ERROR: device {device} not found")
        sys.exit(1)

    print(f"Device:  {device}")
    print(f"Report:  0x{REPORT_ID:02x} (Click Force Threshold)")
    print()

    if args and args[0] == "set":
        if len(args) < 2:
            print("Usage: probe_08.py [device] set <1|2|3>")
            sys.exit(1)
        new_val = int(args[1])

        print(f"=== Write value={new_val} ({THRESHOLDS.get(new_val, '?')}) ===")

        if report_08_write(device, new_val):
            print(f"  SUCCESS: wrote {new_val} to Report 0x08")
            print()
            print(f"  Now press the touchpad — the click feedback threshold")
            print(f"  should feel {'lighter' if new_val == 1 else 'heavier' if new_val == 3 else 'medium'}.")
            print(f"  Try comparing with other values to confirm the effect.")
        else:
            print(f"  FAILED: could not write to Report 0x08")
            sys.exit(1)

    elif args and args[0] == "try-all":
        print("=== Cycling through all 3 levels ===")
        for v in (1, 2, 3):
            print(f"\n--- Setting level {v} ({THRESHOLDS[v]}) ---")
            if report_08_write(device, v):
                print(f"  Wrote {v} — press the touchpad now to feel this level.")
                if v < 3:
                    input(f"  Press Enter to try level {v+1}...")
            else:
                print(f"  FAILED at level {v}")
                break
        print("\n=== Done. Which level felt best? ===")

    else:
        # Read-only mode
        print("=== Read Report 0x08 ===")
        val = report_08_read(device)
        if val is not None:
            print(f"  Current value: {val}  ({THRESHOLDS.get(val, 'unknown')})")
        print()
        print(f"  Commands:")
        print(f"    sudo python3 test/probe_08.py set 1    # lightest click")
        print(f"    sudo python3 test/probe_08.py set 2    # medium click")
        print(f"    sudo python3 test/probe_08.py set 3    # firmest click")
        print(f"    sudo python3 test/probe_08.py try-all  # compare all levels")


if __name__ == "__main__":
    main()
