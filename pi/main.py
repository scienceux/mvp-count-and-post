import time
import signal
import argparse
import subprocess
from pathlib import Path

import cv2
import yaml

from ultralytics import solutions
from camera import create_camera
from logger import EventLogger


def load_config(path):
    with open(path, "r") as f:
        return yaml.safe_load(f)


def load_device_config(cfg):
    """Load pi/device.yaml and apply overrides onto cfg. Creates the file with defaults if missing."""
    path = Path(__file__).parent / "device.yaml"

    if not path.exists():
        defaults = {
            "device_id": cfg.get("logging", {}).get("device_id", "my-device"),
            "event_id": cfg.get("upload", {}).get("event_id", "my-device"),
            "rotation": 0,
            "line_position": cfg.get("counter", {}).get("line_position", 0.5),
        }
        lines = [
            "# Per-device settings — git-ignored, edit this file for each Pi.\n",
            "# Overrides values in config.yaml without touching the shared config.\n",
            "\n",
            f"device_id: \"{defaults['device_id']}\"   # name for this device (CSV filenames, logs)\n",
            f"event_id: \"{defaults['event_id']}\"    # Google Sheets event ID (usually same as device_id)\n",
            f"rotation: {defaults['rotation']}                    # camera rotation in degrees: 0, 90, 180, or 270\n",
            f"line_position: {defaults['line_position']}           # counting line: 0.0=top, 1.0=bottom\n",
        ]
        path.write_text("".join(lines), encoding="utf-8")
        print(f"created {path} with defaults -- edit it for this device")
        return defaults

    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def apply_device_config(cfg, dev):
    """Overlay device-specific values from dev onto cfg in-place."""
    if "device_id" in dev:
        cfg.setdefault("logging", {})["device_id"] = dev["device_id"]
    if "event_id" in dev:
        cfg.setdefault("upload", {})["event_id"] = dev["event_id"]
    if "rotation" in dev:
        cfg.setdefault("camera", {})["rotation"] = dev["rotation"]
    if "line_position" in dev:
        cfg.setdefault("counter", {})["line_position"] = dev["line_position"]


# check if the system clock is NTP-synced; falls back to "estimated" if not or if the check fails
def is_ntp_synced():
    try:
        out = subprocess.check_output(
            ["timedatectl", "show", "--property=NTPSynchronized"],
            text=True
        )
        return "NTPSynchronized=yes" in out
    except Exception:
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).parent / "config.yaml"))
    args = parser.parse_args()

    cfg = load_config(args.config)
    apply_device_config(cfg, load_device_config(cfg))
    cam_cfg = cfg["camera"]
    det_cfg = cfg["detection"]
    cnt_cfg = cfg["counter"]
    log_cfg = cfg["logging"]
    disp_cfg = cfg["display"]
    upl_cfg = cfg.get("upload", {})

    line_frac = cnt_cfg.get("line_position", 0.5)
    fps = cam_cfg.get("fps", 5)
    frame_interval = 1.0 / fps
    model_path = det_cfg.get("model_path", "models/yolov8n_ncnn_model")

    logger = EventLogger(
        csv_dir=log_cfg.get("csv_dir", "logs"),
        device_id=log_cfg.get("device_id", "door-left"),
        upload_url=upl_cfg.get("url") or None,
        event_id=upl_cfg.get("event_id"),
        time_source="ntp" if is_ntp_synced() else "estimated",
    )
    # how often to attempt uploading the queue to the server
    flush_interval = upl_cfg.get("interval_seconds", 30)
    last_flush = time.time()

    running = True

    def on_signal(sig, _):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    prev_in = 0
    prev_out = 0
    n_frames = 0
    cam = create_camera(cfg)

    try:
        with cam:
            ok, first = cam.read()
            if not ok or first is None:
                print("ERROR: can't read from camera")
                return

            # discard frames while the camera adjusts exposure
            warmup = cam_cfg.get("warmup_seconds", 0)
            if warmup > 0:
                print(f"warming up camera for {warmup}s...")
                t_end = time.time() + warmup
                while time.time() < t_end:
                    ok, first = cam.read()
                    if not ok or first is None:
                        break

            h, w = first.shape[:2]
            line_y = int(h * line_frac)

            counter = solutions.ObjectCounter(
                model=model_path,
                region=[(0, line_y), (w, line_y)],
                classes=[0],
                conf=det_cfg.get("confidence", 0.5),
                show=disp_cfg.get("show", False),
                tracker=cnt_cfg.get("tracker", "bytetrack.yaml"),
                verbose=False,
                iou=0.7,
            )

            print(f"counter started | {w}x{h} | line y={line_y} | model={model_path}")
            print(f"device={log_cfg.get('device_id')} | fps={fps} | show={disp_cfg.get('show', False)}")

            counter(first)
            n_frames = 1

            while running:
                t0 = time.time()

                ok, frame = cam.read()
                if not ok or frame is None:
                    if cam_cfg["source"] not in ("usb", "csi"):
                        print("video ended")
                        break
                    time.sleep(0.5)
                    continue

                counter(frame)
                n_frames += 1

                cur_in = counter.in_count
                cur_out = counter.out_count
                d_in = cur_in - prev_in
                d_out = cur_out - prev_out

                for _ in range(d_in):
                    logger.log_event("ENTER")
                for _ in range(d_out):
                    logger.log_event("EXIT")

                if d_in or d_out:
                    print(f"[{n_frames}] IN:{cur_in}(+{d_in}) OUT:{cur_out}(+{d_out})")

                prev_in = cur_in
                prev_out = cur_out

                # periodically upload queued events to the server
                if time.time() - last_flush >= flush_interval:
                    logger.flush_queue()
                    last_flush = time.time()

                if disp_cfg.get("show", False):
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break

                dt = time.time() - t0
                if dt < frame_interval:
                    time.sleep(frame_interval - dt)

    except KeyboardInterrupt:
        pass
    finally:
        # flush any remaining queued events before exiting
        logger.flush_queue()
        logger.close()
        if disp_cfg.get("show", False):
            cv2.destroyAllWindows()
        print(f"\n--- done ---\nIN: {prev_in}  OUT: {prev_out}  frames: {n_frames}")


if __name__ == "__main__":
    main()
