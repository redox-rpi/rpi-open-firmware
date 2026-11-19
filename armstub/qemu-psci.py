#!/usr/bin/env python3
"""PSCI test of armstub8 in QEMU raspi3b (rof #40).

Boots a direct-boot Redox (rpi-boot-stub kernel8.img and its initramfs)
through our armstub8 the way our VPU loader starts it: no -kernel, so all
four cores start at 0 in EL3 and run the stub; the devicetree gets a /psci
node as boot64 adds it. Then it logs in on the serial line and runs the
command given with --cmd (default: poweroff). Pass when the kernel reports
PSCI 1.0 through SMC, prints kstop or kreset, and QEMU exits through the PM
watchdog reset (-no-reboot) within the timeout.

  qemu-psci.py STUB.bin KERNEL8.img INITRAMFS DTB [--cmd poweroff|reset]

DTB is a Pi 3B+ devicetree with console on the PL011 (e.g. the qemu.dtb
that redox-rpi's scripts/fw-direct-boot.sh writes). Needs fdtput and
qemu-system-aarch64. The login is the image's test login.
"""
import argparse, os, re, shutil, socket, subprocess, sys, tempfile, time

DTB_ADDR, INITRD_ADDR, MEM = 0x2000000, 0x2100000, 0x3c000000
ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")


def fdtput(dtb, *args):
    subprocess.run(["fdtput", dtb, *args], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stub"), ap.add_argument("kernel"), ap.add_argument("initramfs"), ap.add_argument("dtb")
    ap.add_argument("--cmd", default="poweroff", choices=["poweroff", "reset"])
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--log", default="qemu-psci.log")
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="qemu-psci.")
    stub = bytearray(open(a.stub, "rb").read())
    stub[0xF8:0xFC] = DTB_ADDR.to_bytes(4, "little")  # dtb_ptr32, as boot64 does
    open(f"{tmp}/stub.bin", "wb").write(stub)
    dtb = f"{tmp}/psci.dtb"
    shutil.copy(a.dtb, dtb)
    size = os.path.getsize(a.initramfs)
    fdtput(dtb, "-t", "x", "/memory@0", "reg", "0", hex(MEM))
    fdtput(dtb, "-t", "x", "/chosen", "linux,initrd-start", hex(INITRD_ADDR))
    fdtput(dtb, "-t", "x", "/chosen", "linux,initrd-end", hex(INITRD_ADDR + size))
    fdtput(dtb, "-t", "s", "/chosen", "bootargs", "RPI_FB=1280x720")  # -append needs -kernel
    if stub[0x100:0x104] == b"PSCI":
        fdtput(dtb, "-c", "/psci")
        fdtput(dtb, "-t", "s", "/psci", "compatible", "arm,psci-1.0", "arm,psci-0.2")
        fdtput(dtb, "-t", "s", "/psci", "method", "smc")
    else:
        print("stub has no PSCI marker at 0x100: no /psci node")

    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    port = srv.getsockname()[1]
    srv.close()
    qemu = ["qemu-system-aarch64", "-M", "raspi3b", "-smp", "4", "-m", "1024", "-nographic",
            "-monitor", "none", "-no-reboot",
            "-serial", f"tcp:127.0.0.1:{port},server=on,wait=on", "-serial", "null",
            "-device", f"loader,file={tmp}/stub.bin,addr=0x0",
            "-device", f"loader,file={a.kernel},addr=0x80000",
            "-device", f"loader,file={dtb},addr={hex(DTB_ADDR)}",
            "-device", f"loader,file={a.initramfs},addr={hex(INITRD_ADDR)}"]
    p = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=1)
            break
        except OSError:
            time.sleep(0.2)
    else:
        print("no serial socket"); p.kill(); return 2
    s.settimeout(0.5)
    log = open(a.log, "wb")
    buf = b""
    t0 = time.monotonic()

    def wait(pat, secs):
        nonlocal buf
        end = time.monotonic() + secs
        while time.monotonic() < end:
            try:
                d = s.recv(4096)
                if not d:
                    return False
                log.write(d); log.flush()
                buf += d
            except socket.timeout:
                pass
            if re.search(pat, ANSI.sub(b"", buf)):
                return True
        return False

    ok = wait(rb"redox login: ?$", a.timeout)
    psci = re.search(rb"PSCI (\d+\.\d+) using smc", buf)
    print(f"login prompt: {ok} after {time.monotonic() - t0:.0f} s; kernel PSCI line: {psci.group(0).decode() if psci else 'none'}")
    if ok:
        s.sendall(b"root\r"); wait(rb"assword: ?$", 20)
        s.sendall(b"password\r"); wait(rb"# ?$", 30)
        buf = b""
        cmd = b"poweroff\r" if a.cmd == "poweroff" else b"echo reset > /scheme/sys/kstop\r"
        s.sendall(cmd)
        want = rb"kstop" if a.cmd == "poweroff" else rb"kreset"
        seen = wait(want, 60)
        print(f"{want.decode()} printed: {seen}")
    try:
        rc = p.wait(timeout=30)
        exited = True
    except subprocess.TimeoutExpired:
        exited = False
        p.kill()
        rc = None
    wait(rb"$^", 1)
    err = re.search(rb"PSCI \w+ failed: \S+", buf)
    print(f"QEMU exited by itself: {exited} (rc {rc}); PSCI error: {err.group(0).decode() if err else 'none'}")
    shutil.rmtree(tmp, ignore_errors=True)
    passed = ok and psci and exited and not err
    print("PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
