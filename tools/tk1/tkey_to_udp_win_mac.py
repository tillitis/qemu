#!/usr/bin/env python

import argparse
import datetime
import hid
import socket
import os
import queue
import select
import signal
import sys
import threading
import time

HID_PACKET_SIZE = 64
FRAME_HEADER_SIZE = 2  # 1 byte type, 1 byte length

# Queue for outgoing HID writes
hid_write_queue = queue.Queue(maxsize=8)
stop_event = threading.Event()

def handle_sigint(signum, frame):
    stop_event.set()

def format_bytes_verbose(data, prefix=""):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = " ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{line}")
    return "\n".join(lines)

def read_hidraw(dev):
    hid_data = b''
    chunk = dev.read(HID_PACKET_SIZE)
    if chunk == b'': # catch if nothing is returned
        return None
    hid_data += chunk

    frame_type, length = hid_data[0], hid_data[1]

    while len(hid_data) < (length+FRAME_HEADER_SIZE):
        chunk = dev.read(HID_PACKET_SIZE)
        if chunk == b'':  # Try again with next frame
            continue
        hid_data += chunk

    return hid_data[:(length+FRAME_HEADER_SIZE)] # Limit data to only valid bytes

def hid_reader(hiddev, udp_sock, udp_dest, args):
    while not stop_event.is_set():
        try:
            frame = read_hidraw(hiddev)
            if not frame:
                continue

            if args.no_header:
                frame = frame[2:]

            udp_sock.sendto(frame, udp_dest)

            if args.verbose:
                dt = datetime.datetime.now()
                print(f"{dt} [TKEY -> UDP {udp_dest}] (length: {len(frame)})")
                print(format_bytes_verbose(frame, prefix="  "))

        except (OSError, ValueError) as e:
            print(f"HID reader exception: {e}")
            break

        except Exception as e:
            if stop_event.is_set():
                break
            print(f"Error: HID reader: {e}")
            stop_event.set() 

def hid_writer(dev):
    while not stop_event.is_set():
        try:
            chunk = hid_write_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        if chunk is None:
            break

        try:
            dev.write(chunk)
        except OSError as e:
            print(f"HID write failed: {e}")
            time.sleep(0.01)
            continue

        # HID write interval pacing
        time.sleep(0.002)

def recv_framed_udp(sock, args):
    try:
        data, addr = sock.recvfrom(2048)
        if len(data) < 2:
            return None, addr

        if args.no_header:
            return data, addr

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
    parser.add_argument("--no-header", action="store_true", help="Remove USB header before sending over UDP")
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
        raise RuntimeError("HID [debug] interface not found")

    hid_path = binary_hid_path.decode('utf-8')

    # Open HID device
    hiddev = hid.Device(path=binary_hid_path)
    hiddev.nonblocking = False 

    # Setup UDP socket
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setblocking(False)
    udp_sock.bind((args.listen_ip, args.listen_port))

    udp_dest = (args.dest_ip, args.dest_port)

    signal.signal(signal.SIGINT, handle_sigint)

    # Start writer thread
    threading.Thread(target=hid_writer, args=(hiddev,), daemon=True).start()
    threading.Thread(target=hid_reader, args=(hiddev, udp_sock, udp_dest, args), daemon=True).start()

    print(f"Forwarding between {hid_path} <-> UDP {udp_dest} ")
    print(f"Use Ctrl+C to exit")

    while not stop_event.is_set():
        try:
            r_ready, _, _ = select.select([udp_sock], [], [], 0.1)

            for fd in r_ready:
                if fd == udp_sock:
                    frame, addr = recv_framed_udp(fd, args)
                    if not frame:
                        continue

                    dt = datetime.datetime.now()
                    if args.verbose:
                        print(f"{dt} [UDP {addr} -> TKEY] (length: {len(frame)})")
                        print(format_bytes_verbose(frame, prefix="  "))

                    if args.no_header:
                        # pre-pend header, needed by loopback app.
                        # Assume a full frame
                        frame = b'\x10\x40' + frame

                    while len(frame) > 0:
                        # Take up to 64 bytes from the frame
                        chunk = frame[:HID_PACKET_SIZE]
                        frame = frame[HID_PACKET_SIZE:]

                        # Ensure exactly 64 bytes by padding if necessary
                        if len(chunk) < HID_PACKET_SIZE:
                            chunk = chunk.ljust(HID_PACKET_SIZE, b'\x00')

                        # Data must always be prepended with Report ID (0)
                        chunk = b'\x00' + chunk

                        print(f"{dt} [To HID-FIDO] (length: {len(chunk)})")
                        print(format_bytes_verbose(chunk, prefix="  "))

                        try:
                            hid_write_queue.put(chunk, timeout=0.1)
                        except queue.Full:
                            print("HID queue full, dropping frame.")
                            break

        except KeyboardInterrupt:
            stop_event.set() 

        except Exception as e:
            print(f"Error: {e}")
            stop_event.set()

    print("Stopping...")
    stop_event.set()
    hid_write_queue.put(None)
    hiddev.close()
    udp_sock.close()
    sys.exit(0)

if __name__ == "__main__":
    main()
