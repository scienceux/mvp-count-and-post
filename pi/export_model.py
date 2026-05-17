"""Download YOLOv8 models and export to NCNN. Run on dev machine, copy models/ to Pi."""

import argparse
import shutil
from pathlib import Path

import yaml
from ultralytics import YOLO

PI_DIR = Path(__file__).parent
OUT = PI_DIR / "models"

DEFAULT_CONFIGS = [
    PI_DIR / "config.yaml",      # door / enter-exit counter  (yolov8n)
    PI_DIR / "row_config.yaml",  # row occupancy counter       (yolov8s)
]


def export_one(cfg_path: Path):
    model_name = "yolov8n.pt"
    imgsz = 640

    if cfg_path.exists():
        with open(cfg_path) as f:
            c = yaml.safe_load(f)
        det = c.get("detection", {})
        model_name = det.get("model_name", model_name)
        imgsz = det.get("input_size", imgsz)
    else:
        print(f"warning: config {cfg_path} not found, skipping")
        return

    stem = model_name.replace(".pt", "")
    dst = OUT / f"{stem}_ncnn_model"

    if dst.exists():
        print(f"skipping {model_name} -- {dst} already exists")
        return

    print(f"\n--- {cfg_path.name}: downloading {model_name} ---")
    model = YOLO(model_name)

    print(f"exporting to ncnn (imgsz={imgsz})...")
    model.export(format="ncnn", imgsz=imgsz)

    src = Path(f"{stem}_ncnn_model")
    if src.exists():
        src.rename(dst)
        print(f"saved to {dst}")
    else:
        print(f"warning: {src} not found, check export output")
        return

    pt = Path(model_name)
    if pt.exists():
        pt.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--config",
        nargs="+",
        default=None,
        help="one or more config files to export (default: all configs)",
    )
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)

    configs = [Path(c) for c in args.config] if args.config else DEFAULT_CONFIGS

    for cfg in configs:
        export_one(cfg)

    print("\ndone -- copy pi/ folder to your raspberry pi")


if __name__ == "__main__":
    main()
