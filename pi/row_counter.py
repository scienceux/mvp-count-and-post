import sys
import time
import signal
import argparse
from pathlib import Path
from datetime import datetime

import cv2
import yaml
from ultralytics import YOLO

from camera import create_camera
from logger import EventLogger


def load_config(path):
    with open(path, "r") as f:
        return yaml.safe_load(f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).parent / "row_config.yaml"))
    args = parser.parse_args()

    cfg = load_config(args.config)
    det_cfg = cfg["detection"]
    roi_cfg = cfg["roi"]
    log_cfg = cfg["logging"]
    disp_cfg = cfg["display"]
    upl_cfg = cfg.get("upload", {})
    interval = cfg.get("interval", 30)
    device_id = log_cfg.get("device_id", "row-A")

    model = YOLO(det_cfg["model_path"], task="detect")
    conf = det_cfg.get("confidence", 0.4)

    roi_frac = (roi_cfg["x1"], roi_cfg["y1"], roi_cfg["x2"], roi_cfg["y2"])

    logger = EventLogger(
        csv_dir=log_cfg.get("csv_dir", "logs"),
        device_id=device_id,
        upload_url=upl_cfg.get("url") or None,
        event_id=upl_cfg.get("event_id"),
    )

    running = True

    def on_signal(sig, _):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    snap = 0
    cam = create_camera(cfg)

    try:
        with cam:
            ok, first = cam.read()
            if not ok or first is None:
                print("ERROR: can't read from camera")
                return

            # discard frames while the camera adjusts exposure
            warmup = cfg["camera"].get("warmup_seconds", 0)
            if warmup > 0:
                print(f"warming up camera for {warmup}s...")
                t_end = time.time() + warmup
                while time.time() < t_end:
                    ok, first = cam.read()
                    if not ok or first is None:
                        break

            h, w = first.shape[:2]
            rx1 = int(w * roi_frac[0])
            ry1 = int(h * roi_frac[1])
            rx2 = int(w * roi_frac[2])
            ry2 = int(h * roi_frac[3])

            print(f"row counter started | {w}x{h} | roi=({rx1},{ry1})-({rx2},{ry2})")
            print(f"device={device_id} | interval={interval}s")

            frame = first

            while running:
                results = model(frame, conf=conf, classes=[0], verbose=False)
                boxes = results[0].boxes

                count = 0
                for box in boxes:
                    x1, y1, x2, y2 = box.xyxy[0].tolist()
                    cx = (x1 + x2) / 2
                    cy = (y1 + y2) / 2
                    if rx1 <= cx <= rx2 and ry1 <= cy <= ry2:
                        count += 1

                snap += 1
                now = datetime.now()
                ts = now.strftime("%Y-%m-%d %H:%M:%S")

                logger.log_event("OCCUPANCY", count=count)
                logger.flush_queue()

                print(f"[{ts}] #{snap} count={count}")

                if disp_cfg.get("show", False):
                    annotated = results[0].plot()
                    cv2.rectangle(annotated, (rx1, ry1), (rx2, ry2), (0, 255, 0), 2)
                    cv2.putText(annotated, f"ROI count: {count}", (rx1, ry1 - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    cv2.imshow("row counter", annotated)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break

                # wait for next snapshot, checking running flag every 100ms
                for _ in range(int(interval * 10)):
                    if not running:
                        break
                    time.sleep(0.1)

                if not running:
                    break

                # grab next frame
                ok, frame = cam.read()
                if not ok or frame is None:
                    if cfg["camera"]["source"] not in ("usb", "csi"):
                        print("video ended")
                        break
                    time.sleep(1)
                    continue

    except KeyboardInterrupt:
        pass
    finally:
        logger.flush_queue()
        logger.close()
        if disp_cfg.get("show", False):
            cv2.destroyAllWindows()
        print(f"\n--- done ---\nsnapshots: {snap}")


if __name__ == "__main__":
    main()
