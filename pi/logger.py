import sys
from datetime import datetime
from pathlib import Path

WEEKDAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


class EventLogger:
    def __init__(self, csv_dir, device_id):
        self._dir = Path(csv_dir)
        self._dir.mkdir(parents=True, exist_ok=True)
        self._device = device_id
        self._date = None
        self._f = None

    def _rotate(self, dt):
        today = dt.date()
        if self._date == today and self._f is not None:
            return
        if self._f is not None:
            self._f.close()
        self._date = today
        path = self._dir / f"{today}_{self._device}.csv"
        is_new = not path.exists()
        self._f = open(path, "a", encoding="utf-8")
        if is_new:
            self._f.write("timestamp,weekday,event,device_id\n")
            self._f.flush()

    def log_event(self, event):
        now = datetime.now()
        try:
            self._rotate(now)
            ts = now.strftime("%Y-%m-%d %H:%M:%S")
            wd = WEEKDAYS[now.weekday()]
            self._f.write(f"{ts},{wd},{event},{self._device}\n")
            self._f.flush()
        except OSError as e:
            print(f"WARNING: log write failed: {e}", file=sys.stderr)

    def close(self):
        if self._f is not None:
            self._f.close()
            self._f = None
