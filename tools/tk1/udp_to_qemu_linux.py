#!/usr/bin/env python

import argparse
import datetime
import os
import select
import socket
import sys
import tty

FRAME_HEADER_SIZE = 2  # 1 byte type, 1 byte length

def format_bytes_verbose(data, prefix=""):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = " ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{line}")
    return "\n".join(lines)

def read_pty(fd):
    pty_header_data = b''
    while len(pty_header_data) < FRAME_HEADER_SIZE:
        try:
            chunk = os.read(fd, FRAME_HEADER_SIZE - len(pty_header_data))
            if chunk == b'':  # This means EOF — the slave side is closed
                print("PTY closed, exiting...")
                sys.exit(1)
        except BlockingIOError:
            continue  # Non-blocking, no data
        pty_header_data += chunk

    frame_type, length = pty_header_data[0], pty_header_data[1]

    pty_payload_data = b''
    while len(pty_payload_data) < length:
        try:
            chunk = os.read(fd, length - len(pty_payload_data))
            if chunk == b'':  # This means EOF — the slave side is closed
                print("PTY closed, exiting...")
                sys.exit(1)
        except BlockingIOError:
            continue  # Non-blocking, no data
        pty_payload_data += chunk

    return pty_header_data + pty_payload_data

def recv_framed_udp(sock):
    udp_data = b''
    while len(udp_data) < FRAME_HEADER_SIZE:
        try:
            udp_data, addr = sock.recvfrom(4096)
        except BlockingIOError:
            continue  # Non-blocking, no data

    if len(udp_data) < FRAME_HEADER_SIZE:
        print(f"Incomplete frame header from {addr}, ignoring")
        return None, addr

    frame_type, length = udp_data[0], udp_data[1]
    expected = FRAME_HEADER_SIZE + length

    while len(udp_data) < expected:
        try:
            chunk, addr = sock.recvfrom(4096)
            udp_data += chunk
        except BlockingIOError:
            continue  # Non-blocking, no data

    return udp_data[:expected], addr

def main():
    parser = argparse.ArgumentParser(description="Forward TKey debug data: UDP <-> QEMU PTY")
    parser.add_argument("--pty", required=True, help="Path to QEMU PTY device (e.g., /dev/pts/3)")
    parser.add_argument("--listen-ip", required=True, help="IP address to listen on")
    parser.add_argument("--listen-port", type=int, required=True, help="UDP port to listen on")
    parser.add_argument("--verbose", action="store_true", help="Print hex dumps of forwarded data")
    args = parser.parse_args()

    # Open PTY
    pty_fd = os.open(args.pty, os.O_RDWR | os.O_NONBLOCK)

    # Set PTY to raw mode (disables buffering, canonical mode, etc.)
    tty.setraw(pty_fd)

    # Setup UDP socket
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setblocking(False)
    udp_sock.bind((args.listen_ip, args.listen_port))
    udp_listen = (args.listen_ip, args.listen_port)
    last_udp_peer = None

    print(f"Forwarding between UDP {udp_listen} <-> {args.pty}")

    while True:
        try:
            r_ready, _, _ = select.select([pty_fd, udp_sock], [], [])

            for fd in r_ready:
                if fd == pty_fd :
                    frame = read_pty(pty_fd)
                    if frame:
                        udp_sock.sendto(frame, last_udp_peer)
                        if args.verbose:
                            dt = datetime.datetime.now()
                            print(f"{dt} [QEMU -> UDP {last_udp_peer}] (length: {len(frame)})")
                            print(format_bytes_verbose(frame, prefix="  "))

                if fd == udp_sock:
                    frame, addr = recv_framed_udp(udp_sock)
                    if frame:
                        last_udp_peer = addr
                        os.write(pty_fd, frame)
                        if args.verbose:
                            dt = datetime.datetime.now()
                            print(f"{dt} [UDP {addr} -> QEMU] (length: {len(frame)})")
                            print(format_bytes_verbose(frame, prefix="  "))

        except KeyboardInterrupt:
            print("Exiting...")
            os.close(pty_fd)
            udp_sock.close()
            sys.exit(1)
        except Exception as e:
            print(f"Error: {e}")
            os.close(pty_fd)
            udp_sock.close()
            sys.exit(1)

if __name__ == "__main__":
    main()
