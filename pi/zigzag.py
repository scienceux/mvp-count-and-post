import time
import signal
import argparse
import subprocess
from pathlib import Path

import cv2
import yaml
from ultralytics import YOLO

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
            "left_line_position": cfg.get("zigzag", {}).get("left_line_position", 0.35),
            "center_line_position": cfg.get("zigzag", {}).get("center_line_position", 0.5),
            "right_line_position": cfg.get("zigzag", {}).get("right_line_position", 0.65),
        }
        lines = [
            "# Per-device settings -- git-ignored, edit this file for each Pi.\n",
            "# Overrides values in config.yaml without touching the shared config.\n",
            "\n",
            f"device_id: \"{defaults['device_id']}\"   # name for this device (CSV filenames, logs)\n",
            f"event_id: \"{defaults['event_id']}\"    # Google Sheets event ID (usually same as device_id)\n",
            f"rotation: {defaults['rotation']}                    # camera rotation in degrees: 0, 90, 180, or 270\n",
            "\n",
            "# zigzag line positions: fractions across frame width\n",
            f"left_line_position: {defaults['left_line_position']}\n",
            f"center_line_position: {defaults['center_line_position']}\n",
            f"right_line_position: {defaults['right_line_position']}\n",
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

    zz = cfg.setdefault("zigzag", {})
    if "left_line_position" in dev:
        zz["left_line_position"] = dev["left_line_position"]
    if "center_line_position" in dev:
        zz["center_line_position"] = dev["center_line_position"]
    if "right_line_position" in dev:
        zz["right_line_position"] = dev["right_line_position"]


def is_ntp_synced():
    try:
        out = subprocess.check_output(
            ["timedatectl", "show", "--property=NTPSynchronized"],
            text=True
        )
        return "NTPSynchronized=yes" in out
    except Exception:
        return False


def _line_cross_event(prev_x, cur_x, line_x, label):
    if prev_x < line_x <= cur_x:
        return f"cross{label}_toright"
    if prev_x > line_x >= cur_x:
        return f"cross{label}_toleft"
    return None


def _draw_lines(frame, x_positions):
    colors = {
        "leftside": (0, 255, 255),
        "center": (0, 255, 0),
        "rightside": (255, 255, 0),
    }
    h = frame.shape[0]
    for label, x in x_positions.items():
        cv2.line(frame, (x, 0), (x, h), colors[label], 2)
        cv2.putText(
            frame,
            label,
            (x + 5, 24),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            colors[label],
            1,
            cv2.LINE_AA,
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).parent / "config.yaml"))
    args = parser.parse_args()

    cfg = load_config(args.config)
    apply_device_config(cfg, load_device_config(cfg))

    cam_cfg = cfg["camera"]
    det_cfg = cfg["detection"]
    cnt_cfg = cfg.get("counter", {})
    zz_cfg = cfg.get("zigzag", {})
    log_cfg = cfg["logging"]
    disp_cfg = cfg.get("display", {})
    upl_cfg = cfg.get("upload", {})

    fps = cam_cfg.get("fps", 5)
    frame_interval = 1.0 / fps
    conf = det_cfg.get("confidence", 0.5)
    model_path = det_cfg.get("model_path", "models/yolov8n_ncnn_model")
    tracker_cfg = cnt_cfg.get("tracker", "bytetrack.yaml")

    left_pos = zz_cfg.get("left_line_position", 0.35)
    center_pos = zz_cfg.get("center_line_position", 0.5)
    right_pos = zz_cfg.get("right_line_position", 0.65)
    exit_missing_frames = int(zz_cfg.get("exit_missing_frames", 15))
    debounce_frames = int(zz_cfg.get("crossing_debounce_frames", 2))

    if not (0.0 <= left_pos <= 1.0 and 0.0 <= center_pos <= 1.0 and 0.0 <= right_pos <= 1.0):
        raise ValueError("zigzag line positions must be in [0.0, 1.0]")
    if not (left_pos < center_pos < right_pos):
        raise ValueError("zigzag lines must satisfy left < center < right")

    logger = EventLogger(
        csv_dir=log_cfg.get("csv_dir", "logs"),
        device_id=log_cfg.get("device_id", "door-left"),
        upload_url=upl_cfg.get("url") or None,
        event_id=upl_cfg.get("event_id"),
        time_source="ntp" if is_ntp_synced() else "estimated",
    )

    flush_interval = upl_cfg.get("interval_seconds", 30)
    last_flush = time.time()

    model = YOLO(model_path, task="detect")

    running = True

    def on_signal(sig, _):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    # object_id -> state
    active = {}
    n_frames = 0
    cam = create_camera(cfg)

    try:
        with cam:
            ok, first = cam.read()
            if not ok or first is None:
                print("ERROR: can't read from camera")
                return

            warmup = cam_cfg.get("warmup_seconds", 0)
            if warmup > 0:
                print(f"warming up camera for {warmup}s...")
                t_end = time.time() + warmup
                while time.time() < t_end:
                    ok, first = cam.read()
                    if not ok or first is None:
                        break

            h, w = first.shape[:2]
            x_positions = {
                "leftside": int(w * left_pos),
                "center": int(w * center_pos),
                "rightside": int(w * right_pos),
            }

            print(
                f"zigzag started | {w}x{h} | "
                f"lines=({x_positions['leftside']},{x_positions['center']},{x_positions['rightside']}) | "
                f"model={model_path}"
            )
            print(
                f"device={log_cfg.get('device_id')} | fps={fps} | "
                f"exit_missing_frames={exit_missing_frames} | show={disp_cfg.get('show', False)}"
            )

            frame = first

            while running:
                t0 = time.time()
                n_frames += 1

                results = model.track(
                    frame,
                    conf=conf,
                    classes=[0],
                    tracker=tracker_cfg,
                    persist=True,
                    verbose=False,
                )
                result = results[0]
                boxes = result.boxes

                seen_ids = set()
                if boxes is not None and boxes.id is not None and len(boxes.id) > 0:
                    ids = boxes.id.int().cpu().tolist()
                    xyxy = boxes.xyxy.cpu().tolist()
                    for object_id, (x1, _y1, x2, _y2) in zip(ids, xyxy):
                        center_x = float((x1 + x2) / 2.0)
                        seen_ids.add(object_id)

                        state = active.get(object_id)
                        if state is None:
                            active[object_id] = {
                                "first_seen": n_frames,
                                "last_seen": n_frames,
                                "last_x": center_x,
                                "last_cross_frame": {
                                    "leftside": -1000000,
                                    "center": -1000000,
                                    "rightside": -1000000,
                                },
                            }
                            logger.log_event("enterframe", person_id=object_id)
                            continue

                        prev_x = state["last_x"]
                        for label, line_x in x_positions.items():
                            event = _line_cross_event(prev_x, center_x, line_x, label)
                            if event is None:
                                continue
                            if (n_frames - state["last_cross_frame"][label]) <= debounce_frames:
                                continue
                            logger.log_event(event, person_id=object_id)
                            state["last_cross_frame"][label] = n_frames

                        state["last_x"] = center_x
                        state["last_seen"] = n_frames

                to_exit = []
                for object_id, state in active.items():
                    if object_id in seen_ids:
                        continue
                    if (n_frames - state["last_seen"]) >= exit_missing_frames:
                        to_exit.append(object_id)

                for object_id in to_exit:
                    logger.log_event("exitframe", person_id=object_id)
                    del active[object_id]

                if to_exit:
                    print(f"[{n_frames}] exits={len(to_exit)} active={len(active)}")

                if time.time() - last_flush >= flush_interval:
                    logger.flush_queue()
                    last_flush = time.time()

                if disp_cfg.get("show", False):
                    annotated = result.plot()
                    _draw_lines(annotated, x_positions)
                    cv2.imshow("zigzag", annotated)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break

                ok, frame = cam.read()
                if not ok or frame is None:
                    if cam_cfg.get("source") not in ("usb", "csi"):
                        print("video ended")
                        break
                    time.sleep(0.5)
                    continue

                dt = time.time() - t0
                if dt < frame_interval:
                    time.sleep(frame_interval - dt)

    except KeyboardInterrupt:
        pass
    finally:
        logger.flush_queue()
        logger.close()
        if disp_cfg.get("show", False):
            cv2.destroyAllWindows()
        print(f"\n--- done ---\nframes: {n_frames} active_remaining: {len(active)}")


if __name__ == "__main__":
    main()