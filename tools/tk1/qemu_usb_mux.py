#!/usr/bin/env python3

import argparse
import fcntl
import os
import pty
import termios
import tty
import select
import sys

# Frame codes
FRAMES = {
    "CDC"  : 0x08,
    "FIDO" : 0x10,
    "CCID" : 0x20,
    "DEBUG": 0x40,
}

# Reverse map: code (int) -> name (str)
FRAME_CODES_TO_NAME = {code: name for name, code in FRAMES.items()}

def format_bytes_verbose(data, prefix=""):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = " ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{line}")
    return "\n".join(lines)

def create_pty(name):
    master, slave = pty.openpty()

    # Set slave PTY to raw mode (disables buffering, canonical mode, etc.)
    tty.setraw(slave)

    # Set master to non-blocking
    flags = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    slave_name = os.ttyname(slave)
    print(f"{name} PTY created at: {slave_name}")
    return master, slave_name

def main():
    parser = argparse.ArgumentParser(description="Open PTY endpoints that adds framing for QEMU communication")
    parser.add_argument("pty_path", help="Path to the QEMU char device PTY (e.g., /dev/pts/X)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose output")
    args = parser.parse_args()

    try:
        pty_fd = os.open(args.pty_path, os.O_RDWR | os.O_NONBLOCK)
    except Exception as e:
        print(f"Failed to open PTY {args.pty_path}: {e}")
        sys.exit(1)

    # Set up framing PTYs
    frame_fds = {}
    fd_to_frame = {}
    for name, code in FRAMES.items():
        fd, path = create_pty(name)
        frame_fds[code] = fd
        fd_to_frame[fd] = code

    # Frame parsing state
    current_frame = None
    expected_length = None
    recv_buffer = bytearray()

    while True:
        try:
            read_fds = [pty_fd] + list(frame_fds.values())
            r_ready, _, _ = select.select(read_fds, [], [])

            for fd in r_ready:
                if fd == pty_fd:
                    try:
                        data = os.read(pty_fd, 64)
                        if data == b'':  # This means EOF — the slave side is closed
                            print("PTY closed, exiting...")
                            sys.exit(1)
                    except BlockingIOError:
                        continue  # Non-blocking, no data

                    for byte in data:
                        if current_frame is None:
                            current_frame = byte
                        elif expected_length is None:
                            expected_length = byte
                            recv_buffer.clear()
                        else:
                            recv_buffer.append(byte)
                            if len(recv_buffer) == expected_length:
                                if current_frame in frame_fds:
                                    os.write(frame_fds[current_frame], recv_buffer)
                                    if args.verbose:
                                        frame_name = FRAME_CODES_TO_NAME.get(current_frame, f"0x{current_frame:02X}")
                                        print(f"[MUX -> {frame_name}] (length: {len(recv_buffer)})")
                                        print(format_bytes_verbose(recv_buffer, prefix="  "))
                                else:
                                    if current_frame == 0x04:
                                        print(f"Got CH552 frame, ignoring...")
                                    else:
                                        print(f"Unknown frame code: 0x{current_frame:02X}")
                                # Reset frame parsing
                                current_frame = None
                                expected_length = None
                                recv_buffer.clear()
                else:
                    # Input from one of the framing PTYs
                    try:
                        data = os.read(fd, 64)
                    except BlockingIOError:
                        continue

                    if data:
                        frame_code = fd_to_frame[fd]
                        frame = bytes([frame_code, len(data)]) + data
                        os.write(pty_fd, frame)
                        if args.verbose:
                            frame_name = FRAME_CODES_TO_NAME.get(frame_code, f"0x{frame_code:02X}")
                            print(f"[{frame_name} -> MUX] (length: {len(data)})")
                            print(format_bytes_verbose(data, prefix="  "))

        except KeyboardInterrupt:
            print("Exiting...")
            break
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()

