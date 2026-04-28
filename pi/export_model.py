"""Download YOLOv8n and export to NCNN. Run on dev machine, copy models/ to Pi."""

from pathlib import Path

import yaml
from ultralytics import YOLO

MODEL = "yolov8n.pt"
OUT = Path(__file__).parent / "models"
CFG = Path(__file__).parent / "config.yaml"


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    imgsz = 640
    if CFG.exists():
        with open(CFG) as f:
            c = yaml.safe_load(f)
        imgsz = c.get("detection", {}).get("input_size", 640)

    print(f"downloading {MODEL}...")
    model = YOLO(MODEL)

    print(f"exporting to ncnn (imgsz={imgsz})...")
    model.export(format="ncnn", imgsz=imgsz)

    src = Path(MODEL.replace(".pt", "_ncnn_model"))
    dst = OUT / "yolov8n_ncnn_model"

    if src.exists():
        if dst.exists():
            import shutil
            shutil.rmtree(dst)
        src.rename(dst)
        print(f"saved to {dst}")
    else:
        print(f"warning: {src} not found, check export output")
        return

    pt = Path(MODEL)
    if pt.exists():
        pt.unlink()

    print("done -- copy pi/ folder to your raspberry pi")


if __name__ == "__main__":
    main()
