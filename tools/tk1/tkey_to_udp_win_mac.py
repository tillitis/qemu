#!/usr/bin/env python

import argparse
import datetime
import hid
import socket
import os
import select
import sys

HID_PACKET_SIZE = 64
FRAME_HEADER_SIZE = 2  # 1 byte type, 1 byte length

def format_bytes_verbose(data, prefix=""):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = " ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{line}")
    return "\n".join(lines)

def read_hidraw(dev):
    hid_data = b''
    while len(hid_data) < HID_PACKET_SIZE:
        try:
            chunk = dev.read(HID_PACKET_SIZE)
            if chunk == b'':  # Nothing to read
                return None
        except BlockingIOError:
            continue  # Non-blocking, no data
        hid_data += chunk

    frame_type, length = hid_data[0], hid_data[1]

    while len(hid_data) < (length+FRAME_HEADER_SIZE):
        try:
            chunk = dev.read(HID_PACKET_SIZE)
            if chunk == b'':  # Nothing to read
                return None
        except BlockingIOError:
            continue  # Non-blocking, no data
        hid_data += chunk

    return hid_data[:(length+FRAME_HEADER_SIZE)] # Limit data to only valid bytes

def recv_framed_udp(sock):
    try:
        data, addr = sock.recvfrom(2048)
        if len(data) < 2:
            return None, addr
        length = data[1]
        expected = 2 + length
        if len(data) < expected:
            return None, addr
        return data[:expected], addr
    except BlockingIOError:
        return None, None

def main():
    parser = argparse.ArgumentParser(description="Forward TKey debug data: HIDRAW <-> UDP")

    parser.add_argument("--list-devices", action="store_true", help="List all HIDRAW devices")

    #parser.add_argument("--hidraw", help="Path to existing HIDRAW device (e.g., /dev/hidraw5)")
    parser.add_argument("--dest-ip", help="Destination IP address")
    parser.add_argument("--dest-port", type=int, help="Destination UDP port")
    parser.add_argument("--listen-ip", help="IP address to listen on")
    parser.add_argument("--listen-port", type=int, help="UDP port to listen on")
    parser.add_argument("--verbose", action="store_true", help="Print hex dumps of forwarded data")
    args = parser.parse_args()

    # Argument --list-devices supplied
    if args.list_devices:
        for dev in hid.enumerate():
            print(f"VID: 0x{dev['vendor_id']:04x}, "
                  f"PID: 0x{dev['product_id']:04x}, "
                  f"Manufacturer: {dev['manufacturer_string']}, "
                  f"Product: {dev['product_string']}, "
                  f"Path: {dev['path'].decode('utf-8')}")
            #print(dev)
        sys.exit(0)

    # Require main arguments only if not using --list-devices
    required = ["dest_ip", "dest_port", "listen_ip", "listen_port"]
    missing = [arg for arg in required if getattr(args, arg) is None]
    if missing:
        parser.error(f"Missing required arguments: {', '.join('--' + m.replace('_', '-') for m in missing)}")

    vid = 0x1209          # Change it for your device
    pid = 0x8885          # Change it for your device
    target_interface = 3  # Change it for your device

    # Find the interface
    for dev in hid.enumerate():
        if (
            dev['vendor_id'] == vid and
            dev['product_id'] == pid and
            dev.get('interface_number') == target_interface
        ):
            binary_hid_path = dev['path']
            break
    else:
        raise RuntimeError("Desired interface not found")

    hid_path = binary_hid_path.decode('utf-8')

    # Open HID device
    hiddev = hid.Device(path=binary_hid_path)
    hiddev.nonblocking = True

    # Setup UDP socket
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setblocking(False)
    udp_sock.bind((args.listen_ip, args.listen_port))

    udp_dest = (args.dest_ip, args.dest_port)

    print(f"Forwarding between {hid_path} <-> UDP {udp_dest}")

    while True:
        try:
            r_ready, _, _ = select.select([udp_sock], [], [], 0.1)

            frame = read_hidraw(hiddev)
            if frame:
                    udp_sock.sendto(frame, udp_dest)
                    if args.verbose:
                        dt = datetime.datetime.now()
                        print(f"{dt} [TKEY -> UDP {udp_dest}] (length: {len(frame)})")
                        print(format_bytes_verbose(frame, prefix="  "))

            for fd in r_ready:

                if fd == udp_sock:
                    frame, addr = recv_framed_udp(fd)
                    if frame:
                        dt = datetime.datetime.now()
                        if args.verbose:
                            print(f"{dt} [UDP {addr} -> TKEY] (length: {len(frame)})")
                            print(format_bytes_verbose(frame, prefix="  "))

                        while len(frame) > 0:
                            # Take up to 64 bytes from the frame
                            chunk = frame[:HID_PACKET_SIZE]
                            frame = frame[HID_PACKET_SIZE:]

                            # Ensure exactly 64 bytes by padding if necessary
                            if len(chunk) < HID_PACKET_SIZE:
                                chunk = chunk.ljust(HID_PACKET_SIZE, b'\x00')

                            # Data must always be prepended with Report ID (0)
                            chunk = b'\x00' + chunk

                            print(f"{dt} [To HIDRAW] (length: {len(chunk)})")
                            print(format_bytes_verbose(chunk, prefix="  "))

                            hiddev.write(chunk)

        except KeyboardInterrupt:
            print("Exiting...")
            hiddev.close()
            udp_sock.close()
            sys.exit(1)
        except Exception as e:
            print(f"Error: {e}")
            hiddev.close()
            udp_sock.close()
            sys.exit(1)

if __name__ == "__main__":
    main()
