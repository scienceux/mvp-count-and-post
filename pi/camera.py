import time
import cv2


def create_camera(cfg):
    src = cfg["camera"]["source"]
    if src == "usb":
        return USBCamera(cfg)
    elif src == "csi":
        return CSICamera(cfg)
    return VideoFileCamera(cfg)


class USBCamera:
    def __init__(self, cfg):
        self._idx = cfg["camera"].get("device_index", 0)
        self._w = cfg["camera"].get("width", 640)
        self._h = cfg["camera"].get("height", 480)
        self._cap = None

    def open(self):
        self._cap = cv2.VideoCapture(self._idx)
        if not self._cap.isOpened():
            raise RuntimeError(f"can't open USB camera {self._idx}")
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, self._w)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self._h)

    def read(self):
        if self._cap is None:
            self.open()
        ok, frame = self._cap.read()
        if not ok:
            print("USB read failed, reconnecting...")
            self.close()
            time.sleep(2)
            try:
                self.open()
                ok, frame = self._cap.read()
            except RuntimeError:
                return False, None
        return ok, frame

    def close(self):
        if self._cap is not None:
            self._cap.release()
            self._cap = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()


class CSICamera:
    def __init__(self, cfg):
        self._w = cfg["camera"].get("width", 640)
        self._h = cfg["camera"].get("height", 480)
        self._fps = cfg["camera"].get("fps", 5)
        self._cam = None

    def open(self):
        try:
            from picamera2 import Picamera2
        except ImportError:
            raise RuntimeError("picamera2 not found -- use --system-site-packages in venv")
        self._cam = Picamera2()
        conf = self._cam.create_preview_configuration(
            main={"format": "RGB888", "size": (self._w, self._h)},
            controls={"FrameRate": self._fps},
        )
        self._cam.configure(conf)
        self._cam.start()

    def read(self):
        if self._cam is None:
            self.open()
        try:
            return True, self._cam.capture_array()
        except Exception as e:
            print(f"CSI read failed: {e}, reconnecting...")
            self.close()
            time.sleep(2)
            try:
                self.open()
                return True, self._cam.capture_array()
            except Exception:
                return False, None

    def close(self):
        if self._cam is not None:
            self._cam.stop()
            self._cam.close()
            self._cam = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()


class VideoFileCamera:
    def __init__(self, cfg):
        self._path = cfg["camera"]["source"]
        self._cap = None

    def open(self):
        self._cap = cv2.VideoCapture(self._path)
        if not self._cap.isOpened():
            raise RuntimeError(f"can't open video: {self._path}")

    def read(self):
        if self._cap is None:
            self.open()
        return self._cap.read()

    def close(self):
        if self._cap is not None:
            self._cap.release()
            self._cap = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()
