#!/usr/bin/env python3

import argparse
import ctypes
import fcntl
import os
import select
import struct
import sys
import time

# UHID constants
UHID_DESTROY             = 0x1
UHID_START               = 0x2
UHID_STOP                = 0x3
UHID_OPEN                = 0x4
UHID_CLOSE               = 0x5
UHID_OUTPUT              = 0x6
UHID_GET_REPORT          = 0x9
UHID_GET_REPORT_REPLY    = 0xA
UHID_CREATE2             = 0xB
UHID_INPUT2              = 0xC
UHID_SET_REPORT          = 0xD
UHID_SET_REPORT_REPLY    = 0xE

# UHID device
UHID_DEV = "/dev/uhid"

FIDO_HID_DESCRIPTOR = bytes([
        0x06, 0xD0, 0xF1,                 # Usage Page (FIDO Alliance Page)
        0x09, 0x01,                       # Usage (U2F Authenticator Device)
        0xA1, 0x01,                       #   Collection (Application)
        # 7
        0x09, 0x20,                       #     Usage (Input Report Data)
        0x15, 0x00,                       #     Logical Minimum (0)
        0x26, 0xFF, 0x00,                 #     Logical Maximum (255)
        0x75, 0x08,                       #     Report Size (8)
        0x95, 0x40,                       #     Report Count (64)
        0x81, 0x02,                       #     Input (Data, Variable, Absolute)
        # 20
        0x09, 0x21,                       #     Usage (Output Report Data)
        0x15, 0x00,                       #     Logical Minimum (0)
        0x26, 0xFF, 0x00,                 #     Logical Maximum (255)
        0x75, 0x08,                       #     Report Size (8)
        0x95, 0x40,                       #     Report Count (64)
        0x91, 0x02,                       #     Output (Data, Variable, Absolute)
        # 33
        0x09, 0x07,                       #     Usage (7, Reserved)
        0x15, 0x00,                       #     Logical Minimum (0)
        0x26, 0xFF, 0x00,                 #     Logical Maximum (255)
        0x75, 0x08,                       #     Report Size (8)
        0x95, 0x08,                       #     Report Count (8)
        0xB1, 0x02,                       #     Feature (2) (???)
        # 46
        0xC0                              #   End Collection */
        # 47
])

def format_bytes_verbose(data, prefix="  "):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = " ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{line}")
    return "\n".join(lines)

class UHIDCreate2(ctypes.Structure):
    _pack_ = 1  # Equivalent to __attribute__((__packed__))
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("name", ctypes.c_char * 128),
        ("phys", ctypes.c_char * 64),
        ("uniq", ctypes.c_char * 64),
        ("rd_size", ctypes.c_uint16),
        ("bus", ctypes.c_uint16),
        ("vendor", ctypes.c_uint32),
        ("product", ctypes.c_uint32),
        ("version", ctypes.c_uint32),
        ("country", ctypes.c_uint32),
        ("rd_data", ctypes.c_uint8 * 4096),
    ]

def uhid_create(fd, args):
    req = UHIDCreate2()
    ctypes.memset(ctypes.addressof(req), 0x0, ctypes.sizeof(req))
    req.type = UHID_CREATE2
    req.name = b"FIDO2 token emulator"
    req.phys = args.pty_path.encode('utf-8')
    req.uniq = b""
    req.rd_size = len(FIDO_HID_DESCRIPTOR)
    req.bus = 0x03  # BUS_USB
    req.vendor = 0x1207
    req.product = 0x8887
    req.version = 0x100
    req.country = 0
    # Copy descriptor into rd_data
    for i in range(len(FIDO_HID_DESCRIPTOR)):
        req.rd_data[i] = FIDO_HID_DESCRIPTOR[i]
    if args.verbose:
        print("Creating UHID device")
    os.write(fd, bytes(req))
    if args.verbose:
        print("Descriptor written:")
        print(format_bytes_verbose(FIDO_HID_DESCRIPTOR, prefix="  "))

def parse_event(data, args):
    if data[0] == UHID_START:
        if args.verbose:
            print("[UHID_START]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return b''
    elif data[0] == UHID_STOP:
        if args.verbose:
            print("[UHID_STOP]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return b''
    elif data[0] == UHID_OPEN:
        if args.verbose:
            print("[UHID_OPEN]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return b''
    elif data[0] == UHID_CLOSE:
        if args.verbose:
            print("[UHID_CLOSE]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return b''
    elif data[0] == UHID_OUTPUT:
        if args.verbose:
            print("[UHID_OUTPUT]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return data[1+4:1+4+64]
    else:
        if args.verbose:
            print("[Unknown UHID event]")
            print(format_bytes_verbose(data[:5], prefix="  "))
        return b''

def main():
    parser = argparse.ArgumentParser(description="FIDO2 token emulator using UHID")
    parser.add_argument("pty_path", help="Path to QEMU FIDO PTY (e.g., /dev/pts/X)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose output")
    args = parser.parse_args()

    # Open UHID device
    try:
        uhid_fd = os.open(UHID_DEV, os.O_RDWR | os.O_NONBLOCK)
    except Exception as e:
        print(f"Failed to open {UHID_DEV}: {e}")
        sys.exit(1)

    # Configure UHID
    uhid_create(uhid_fd, args)

    # Open PTY
    try:
        pty_fd = os.open(args.pty_path, os.O_RDWR | os.O_NONBLOCK)
    except Exception as e:
        print(f"Failed to open PTY {args.pty_path}: {e}")
        sys.exit(1)

    if args.verbose:
        print("Bridging to PTY")

    while True:
        try:
            r_ready, _, _ = select.select([uhid_fd, pty_fd], [], [])

            for fd in r_ready:
                if fd == pty_fd:
                    try:
                        data = os.read(pty_fd, 64)
                        if data == b'':  # This means EOF — the slave side is closed
                            print("PTY closed, exiting...")
                            sys.exit(1)
                    except BlockingIOError:
                        continue  # Non-blocking, no data

                    report_data = struct.pack("<IH64s", UHID_INPUT2, 64, data.ljust(64, b'\0'))
                    os.write(uhid_fd, report_data)
                    if args.verbose:
                        print(f"[PTY -> UHID] (length: {len(data)})")
                        print(format_bytes_verbose(data, prefix="  "))

                elif fd == uhid_fd:
                    try:
                        data = os.read(uhid_fd, 1+4+64)
                    except BlockingIOError:
                        continue

                    pty_data = parse_event(data, args)
                    if any(pty_data):
                        os.write(pty_fd, pty_data.ljust(64, b'\0'))
                        if args.verbose:
                            print(f"[UHID -> PTY] (length: {len(pty_data)})")
                            print(format_bytes_verbose(pty_data, prefix="  "))

        except KeyboardInterrupt:
            print("Exiting...")
            break
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()

