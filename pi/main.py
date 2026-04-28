import time
import signal
import argparse
from pathlib import Path

import cv2
import yaml

from ultralytics import solutions
from camera import create_camera
from logger import EventLogger


def load_config(path):
    with open(path, "r") as f:
        return yaml.safe_load(f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).parent / "config.yaml"))
    args = parser.parse_args()

    cfg = load_config(args.config)
    cam_cfg = cfg["camera"]
    det_cfg = cfg["detection"]
    cnt_cfg = cfg["counter"]
    log_cfg = cfg["logging"]
    disp_cfg = cfg["display"]

    line_frac = cnt_cfg.get("line_position", 0.5)
    fps = cam_cfg.get("fps", 5)
    frame_interval = 1.0 / fps
    model_path = det_cfg.get("model_path", "models/yolov8n_ncnn_model")

    logger = EventLogger(
        csv_dir=log_cfg.get("csv_dir", "logs"),
        device_id=log_cfg.get("device_id", "door-left"),
    )

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

                if disp_cfg.get("show", False):
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break

                dt = time.time() - t0
                if dt < frame_interval:
                    time.sleep(frame_interval - dt)

    except KeyboardInterrupt:
        pass
    finally:
        logger.close()
        if disp_cfg.get("show", False):
            cv2.destroyAllWindows()
        print(f"\n--- done ---\nIN: {prev_in}  OUT: {prev_out}  frames: {n_frames}")


if __name__ == "__main__":
    main()
