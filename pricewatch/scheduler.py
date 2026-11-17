"""后台调度：按每个店铺自己的间隔轮询采集。

单进程线程模型足够：采集是 I/O 密集型，而店铺数量在初期只有几十个。
"""

import threading
from datetime import datetime, timedelta
from typing import Optional

import config
import notify
from collector import collect_store


class Scheduler:
    def __init__(self, storage, tick_seconds: int = 60) -> None:
        self.storage = storage
        self.tick_seconds = tick_seconds
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, name="collector", daemon=True)
        self._thread.start()
        print(f"[调度] 已启动，每 {self.tick_seconds} 秒检查一次到期店铺")

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=5)

    def _loop(self) -> None:
        while not self._stop.wait(self.tick_seconds):
            try:
                self.tick()
            except Exception as exc:
                print(f"[调度] 本轮异常：{exc.__class__.__name__}: {exc}")

    def _due(self, store) -> bool:
        last = store["last_checked_at"]
        if not last:
            return True
        try:
            last_dt = datetime.fromisoformat(last)
        except ValueError:
            return True
        interval = int(store["interval_min"] or config.DEFAULT_INTERVAL_MIN)
        return (datetime.now() - last_dt) >= timedelta(minutes=interval)

    def tick(self) -> int:
        """检查所有到期店铺，返回本轮实际采集的数量。"""
        count = 0
        for store in self.storage.all_active_stores():
            if self._stop.is_set():
                break
            if not self._due(store):
                continue
            self.collect_one(store)
            count += 1
        return count

    def collect_one(self, store):
        stats, events = collect_store(self.storage, store)

        if stats["error"]:
            print(f"[采集] {store['label']} 失败：{stats['error']}")
            return stats

        tag = "（首次建基线）" if stats["baseline"] else ""
        print(f"[采集] {store['label']} 共 {stats['total']} 件，变化 {len(events)} 条{tag}")

        if events:
            user = self.storage.get_user(store["user_id"])
            email = user["email"] if user else ""
            notify.send_digest(self.storage, email, store["label"], events)

        return stats
