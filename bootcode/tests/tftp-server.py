#!/usr/bin/env python3
"""Small read-only TFTP server with blksize, tsize and windowsize (RFC 1350,
2348, 2349, 7440), for testing the firmware's TFTP client (rof #120).

    tests/tftp-server.py ROOT [PORT] [--max-window N]

Serves files below ROOT on PORT (default 6969), one thread per transfer.
Not for production: no write requests, no access control beyond ROOT.
"""
import os
import socket
import struct
import sys
import threading
import time

RRQ, DATA, ACK, ERROR, OACK = 1, 3, 4, 5, 6


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def serve(root, req, peer, max_window):
    parts = req[2:].split(b"\0")
    name, opts = parts[0].decode(errors="replace"), {}
    for i in range(2, len(parts) - 1, 2):
        if parts[i]:
            opts[parts[i].decode().lower()] = parts[i + 1].decode()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("", 0))
    s.settimeout(0.5)
    path = os.path.realpath(os.path.join(root, name.lstrip("/")))
    if not path.startswith(os.path.realpath(root) + os.sep) or not os.path.isfile(path):
        s.sendto(struct.pack("!HH", ERROR, 1) + b"file not found\0", peer)
        log(peer, "not found:", name)
        return
    data = open(path, "rb").read()
    blk, win, oack = 512, 1, []
    if "blksize" in opts:
        blk = max(8, min(int(opts["blksize"]), 65464))
        oack += [b"blksize", str(blk).encode()]
    if "tsize" in opts:
        oack += [b"tsize", str(len(data)).encode()]
    if "windowsize" in opts:
        win = max(1, min(int(opts["windowsize"]), max_window))
        oack += [b"windowsize", str(win).encode()]
    nblocks = len(data) // blk + 1  # the last block is shorter, maybe empty
    t0 = time.time()
    resent = 0

    def wait_ack(expect_max):
        while True:
            pkt, src = s.recvfrom(1500)
            if src != peer or len(pkt) < 4:
                continue
            op, n = struct.unpack("!HH", pkt[:4])
            if op == ERROR:
                raise ConnectionAbortedError(pkt[4:])
            if op == ACK:
                return n

    try:
        if oack:
            for _ in range(5):
                s.sendto(struct.pack("!H", OACK) + b"\0".join(oack) + b"\0", peer)
                try:
                    if wait_ack(0) == 0:
                        break
                except socket.timeout:
                    resent += 1
            else:
                log(peer, "no ACK for the OACK")
                return
        acked = 0  # blocks acknowledged (as a 32-bit count)
        tries = 0
        while acked < nblocks:
            end = min(acked + win, nblocks)
            for b in range(acked + 1, end + 1):
                chunk = data[(b - 1) * blk:b * blk]
                s.sendto(struct.pack("!HH", DATA, b & 0xFFFF) + chunk, peer)
            try:
                n = wait_ack(end)
            except socket.timeout:
                tries += 1
                resent += 1
                if tries > 6:
                    log(peer, "timeout at block", acked)
                    return
                continue
            # map the 16-bit ACK onto the window that was sent
            for b in range(end, acked - 1, -1):
                if b & 0xFFFF == n:
                    if b > acked:
                        tries = 0
                    acked = b
                    break
        dt = time.time() - t0
        log(peer, name, len(data), "bytes in %.3f s, %.2f MB/s" % (dt, len(data) / dt / 1e6),
            "blksize", blk, "window", win, "resent", resent)
    except ConnectionAbortedError as e:
        log(peer, "client error", e)
    finally:
        s.close()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    max_window = 64
    if "--max-window" in sys.argv:
        max_window = int(sys.argv[sys.argv.index("--max-window") + 1])
        args = [a for a in args if a != str(max_window)]
    root = args[0]
    port = int(args[1]) if len(args) > 1 else 6969
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("", port))
    log("serving", root, "on port", port)
    while True:
        req, peer = s.recvfrom(1500)
        if len(req) > 2 and struct.unpack("!H", req[:2])[0] == RRQ:
            threading.Thread(target=serve, args=(root, req, peer, max_window), daemon=True).start()


if __name__ == "__main__":
    main()
