import sys
import urllib.request
import urllib.parse
from datetime import datetime
from pathlib import Path

WEEKDAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


class EventLogger:
    def __init__(self, csv_dir, device_id, upload_url=None, event_id=None, time_source="ntp"):
        self._dir = Path(csv_dir)
        self._dir.mkdir(parents=True, exist_ok=True)
        self._device = device_id
        self._upload_url = upload_url
        self._event_id = event_id or device_id
        self._time_source = time_source  # "ntp" or "estimated" — checked once at startup
        # staging file for events not yet uploaded
        self._queue_path = self._dir / "queue.csv"
        self._date = None
        self._f = None

    def _open_permanent(self, dt):
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
            self._f.write("timestamp,weekday,device_id,event,count,time_source\n")
            self._f.flush()

    def log_event(self, event, count=1):
        now = datetime.now()
        ts = now.strftime("%Y-%m-%d %H:%M:%S")
        wd = WEEKDAYS[now.weekday()]
        row = f"{ts},{wd},{self._device},{event},{count},{self._time_source}"
        # write to queue only — permanent CSV is written after successful upload
        try:
            with open(self._queue_path, "a", encoding="utf-8") as qf:
                qf.write(row + "\n")
        except OSError as e:
            print(f"WARNING: queue write failed: {e}", file=sys.stderr)

    def flush_queue(self):
        if not self._upload_url or not self._queue_path.exists():
            return

        # open the queue and read all pending rows
        try:
            rows = [
                ln for ln in self._queue_path.read_text(encoding="utf-8").splitlines()
                if ln.strip()
            ]
        except OSError as e:
            print(f"WARNING: queue read failed: {e}", file=sys.stderr)
            return

        if not rows:
            self._queue_path.unlink(missing_ok=True)
            return

        # try to upload each row; keep track of what succeeded and what didn't
        failed = []
        uploaded = []
        for row in rows:
            try:
                params = urllib.parse.urlencode({
                    "eventId": self._event_id,
                    "rows": row,
                })
                url = f"{self._upload_url}?{params}"
                with urllib.request.urlopen(url, timeout=10) as resp:
                    if resp.status == 200:
                        uploaded.append(row)
                    else:
                        failed.append(row)
            except Exception as e:
                print(f"WARNING: upload failed: {e}", file=sys.stderr)
                failed.append(row)

        # move successfully uploaded rows to the permanent daily CSV
        if uploaded:
            now = datetime.now()
            try:
                self._open_permanent(now)
                for row in uploaded:
                    self._f.write(row + "\n")
                self._f.flush()
            except OSError as e:
                print(f"WARNING: permanent CSV write failed: {e}", file=sys.stderr)

        # rewrite the queue with only the failed rows, or delete it if everything uploaded
        try:
            if failed:
                self._queue_path.write_text("\n".join(failed) + "\n", encoding="utf-8")
            else:
                self._queue_path.unlink(missing_ok=True)
        except OSError as e:
            print(f"WARNING: queue rewrite failed: {e}", file=sys.stderr)

    def close(self):
        if self._f is not None:
            self._f.close()
            self._f = None
