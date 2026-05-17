# pi people counter

Two tools in one folder:

- **main.py** -- enter/exit door counter (YOLOv8n + ByteTrack, tracks people crossing a line)
- **row_counter.py** -- row occupancy counter (YOLOv8s, snapshots every N seconds, counts people in a region)

## setup

### 1. export models (on your dev machine, not the pi)

```
cd pi/
pip install ultralytics ncnn pnnx
python export_model.py                      # yolov8n for door counter
python export_model.py --config row_config.yaml   # yolov8s for row counter
```

### 2. copy to pi

Copy this whole `pi/` folder to the Pi.

### 3. install deps on pi

```
cd pi/
python3 -m venv venv --system-site-packages
source venv/bin/activate
pip install -r requirements.txt
```

Takes ~15-20 min on Pi (torch is big). The `--system-site-packages` flag is
needed if you're using a CSI camera so picamera2 is accessible.

### 4. configure

Edit `config.yaml`. Main things to change:

- `camera.source` -- `"usb"`, `"csi"`, or a video file path
- `camera.device_index` -- which USB cam (`ls /dev/video*` to check)
- `detection.input_size` -- `640` for Pi 5, `320` for Pi 4. If you change this you need to re-run `export_model.py`
- `counter.line_position` -- where the counting line goes (0=top, 1=bottom, 0.5=middle)
- `logging.device_id` -- name for this camera, shows up in the CSV
- `display.show` -- only set `true` if you have a monitor plugged in, crashes otherwise

### 5. run

```
python main.py
```

Ctrl+C to stop.

## two cameras (wide door)

Run two instances with different configs:

```
python main.py --config config_left.yaml
python main.py --config config_right.yaml
```

Different `device_index` and `device_id` in each. Counts go to separate CSVs, sum them later.

Find camera indices with `ls /dev/video*` or `v4l2-ctl --list-devices`.

## pi 4 vs pi 5

Pi 5 gets ~15-20 fps at input_size 640. Pi 4 gets like 2-3 fps at 640 which
is unusable -- set `input_size: 320` and re-export, gets you ~8-10 fps.
Shorter detection range but fine when the camera is above a doorway.

## csv format

```
timestamp,weekday,event,device_id
2026-04-12 14:30:01,Sat,ENTER,door-left
2026-04-12 14:30:03,Sat,EXIT,door-left
```

One file per day in `logs/`.

## testing without a pi

Works on any machine with a USB webcam:

```
pip install ultralytics ncnn pnnx pyyaml opencv-python
python export_model.py
python main.py
```

Or set `camera.source` to a video file.

## row counter (poster rows)

Counts how many people are standing in a poster row. Takes a snapshot every N
seconds, runs detection, filters to an ROI, logs the count.

```
python row_counter.py
```

Edit `row_config.yaml`:

- `detection.model_name` -- uses yolov8s by default (better accuracy, speed doesn't matter)
- `roi` -- x1/y1/x2/y2 as fractions of frame size, defines where the row is. anything outside gets ignored (filters out passersby at the end of the row)
- `interval` -- seconds between snapshots (default 30)
- `logging.device_id` -- e.g. `"row-A"`, `"row-B"`

CSV format for row counter:

```
timestamp,weekday,count,device_id
2026-05-05 14:30:01,Mon,12,row-A
2026-05-05 14:31:01,Mon,8,row-A
```

Mount the camera high up on the poster board looking down the row at an angle
for best results. Reduces occlusion so it can see more people.

## troubleshooting

- too many false positives -- bump `confidence` up (0.6, 0.7)
- missing people -- lower `confidence` (0.3, 0.4)
- enter/exit swapped -- rotate camera 180 or mess with `line_position`
- double counting -- try `tracker: "botsort.yaml"` instead of bytetrack
- row counter counting passersby -- tighten the `roi` bounds in row_config.yaml
