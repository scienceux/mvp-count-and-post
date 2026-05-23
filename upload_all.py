# Flash firmware to multiple ESP32s at once.
# Usage: python upload_all.py COM6 COM7 COM8
#        python upload_all.py --parallel 4 COM6 COM7 ...
#        python upload_all.py   (auto-detects ports)

import argparse
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

try:
    import serial.tools.list_ports as list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False


def detect_ports() -> list[str]:
    if not HAS_SERIAL:
        print(
            "ERROR: pyserial not found. Install it with:\n"
            "    pip install pyserial\n"
            "Or pass port names explicitly: python upload_all.py COM6 COM7",
            file=sys.stderr,
        )
        sys.exit(1)

    ports = []
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        mfr  = (p.manufacturer or "").lower()
        # common ESP32-S3 / Silicon Labs / CP210x / CH34x identifiers
        if any(k in desc or k in mfr for k in ("cp210", "ch34", "usb serial", "usb-serial", "xiao", "esp")):
            ports.append(p.device)

    if not ports:
        # Fall back to listing everything so the user can pick
        all_ports = [p.device for p in list_ports.comports()]
        if all_ports:
            print(
                "No ESP32-like ports detected automatically.\n"
                "All available ports: " + ", ".join(all_ports) + "\n"
                "Pass them explicitly: python upload_all.py " + " ".join(all_ports),
                file=sys.stderr,
            )
        else:
            print("No COM ports found. Make sure your devices are plugged in.", file=sys.stderr)
        sys.exit(1)

    return ports


print_lock = threading.Lock()


def stream_prefix(stream, prefix: str):
    for raw in stream:
        line = raw.rstrip("\n")
        with print_lock:
            print(f"[{prefix}] {line}", flush=True)


def upload_to_port(port: str) -> tuple[str, int]:
    cmd = ["pio", "run", "-t", "upload", "--upload-port", port]

    with print_lock:
        print(f"[{port}] Starting upload: {' '.join(cmd)}", flush=True)

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    stream_prefix(proc.stdout, port)
    proc.wait()

    status = "OK" if proc.returncode == 0 else f"FAILED (exit {proc.returncode})"
    with print_lock:
        print(f"[{port}] Upload {status}", flush=True)

    return port, proc.returncode


def build_firmware() -> bool:
    print("Building...")
    proc = subprocess.Popen(
        ["pio", "run"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    stream_prefix(proc.stdout, "BUILD")
    proc.wait()
    if proc.returncode != 0:
        print("\nBuild FAILED. Fix compile errors before uploading.", file=sys.stderr)
        return False
    print("\nBuild OK.\n")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ports", nargs="*")
    parser.add_argument("--parallel", type=int, default=0)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    ports = args.ports if args.ports else detect_ports()
    workers = args.parallel if args.parallel > 0 else len(ports)

    if not args.skip_build:
        if not build_firmware():
            sys.exit(1)

    print(f"Uploading to {len(ports)} device(s): {', '.join(ports)}")
    print(f"Running up to {workers} upload(s) in parallel.\n")

    results = {}
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(upload_to_port, p): p for p in ports}
        for fut in as_completed(futures):
            port, code = fut.result()
            results[port] = code

    print("\n=== Upload Summary ===")
    ok   = [p for p, c in results.items() if c == 0]
    fail = [p for p, c in results.items() if c != 0]
    for p in ok:
        print(f"  OK     {p}")
    for p in fail:
        print(f"  FAILED {p}")
    print(f"\n{len(ok)}/{len(ports)} succeeded.")

    sys.exit(0 if not fail else 1)


if __name__ == "__main__":
    main()
