"""Download a YOLOv8 model and export to NCNN. Run on dev machine, copy models/ to Pi."""

import argparse
from pathlib import Path

import yaml
from ultralytics import YOLO

DEFAULT_CFG = Path(__file__).parent / "config.yaml"
OUT = Path(__file__).parent / "models"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(DEFAULT_CFG), help="config file to read model settings from")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)

    model_name = "yolov8n.pt"
    imgsz = 640

    cfg_path = Path(args.config)
    if cfg_path.exists():
        with open(cfg_path) as f:
            c = yaml.safe_load(f)
        det = c.get("detection", {})
        model_name = det.get("model_name", model_name)
        imgsz = det.get("input_size", imgsz)

    stem = model_name.replace(".pt", "")

    print(f"downloading {model_name}...")
    model = YOLO(model_name)

    print(f"exporting to ncnn (imgsz={imgsz})...")
    model.export(format="ncnn", imgsz=imgsz)

    src = Path(f"{stem}_ncnn_model")
    dst = OUT / f"{stem}_ncnn_model"

    if src.exists():
        if dst.exists():
            import shutil
            shutil.rmtree(dst)
        src.rename(dst)
        print(f"saved to {dst}")
    else:
        print(f"warning: {src} not found, check export output")
        return

    pt = Path(model_name)
    if pt.exists():
        pt.unlink()

    print("done -- copy pi/ folder to your raspberry pi")


if __name__ == "__main__":
    main()
