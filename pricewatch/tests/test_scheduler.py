"""调度器测试：验证到期判断与采集触发。"""

import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import patch

import scheduler as sched_mod
from storage import Storage


class TestScheduler(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.storage = Storage(str(Path(self.tmp.name) / "sched.db"))
        self.uid = self.storage.create_user("a@b.com", "password123")
        self.sid = self.storage.add_store(
            self.uid, "https://shop.example.com", "woocommerce", "测试店"
        )
        self.sched = sched_mod.Scheduler(self.storage, tick_seconds=3600)

    def tearDown(self):
        self.tmp.cleanup()

    def _set_last(self, iso):
        with self.storage._conn() as conn:
            conn.execute(
                "UPDATE stores SET last_checked_at = ? WHERE id = ?", (iso, self.sid)
            )

    def test_due_when_never_checked(self):
        store = self.storage.get_store(self.sid, self.uid)
        self.assertTrue(self.sched._due(store))

    def test_not_due_when_just_checked(self):
        self._set_last(datetime.now().isoformat(timespec="seconds"))
        store = self.storage.get_store(self.sid, self.uid)
        self.assertFalse(self.sched._due(store))

    def test_due_when_interval_passed(self):
        old = (datetime.now() - timedelta(minutes=120)).isoformat(timespec="seconds")
        self._set_last(old)
        store = self.storage.get_store(self.sid, self.uid)
        self.assertTrue(self.sched._due(store))

    def test_broken_timestamp_is_treated_as_due(self):
        self._set_last("not-a-date")
        store = self.storage.get_store(self.sid, self.uid)
        self.assertTrue(self.sched._due(store))

    def test_tick_collects_due_store(self):
        fake_stats = {"error": "", "total": 0, "baseline": True}
        with patch.object(sched_mod, "collect_store", return_value=(fake_stats, [])) as mocked:
            count = self.sched.tick()
        self.assertEqual(count, 1)
        self.assertEqual(mocked.call_count, 1)

    def test_tick_skips_fresh_store(self):
        self._set_last(datetime.now().isoformat(timespec="seconds"))
        fake_stats = {"error": "", "total": 0, "baseline": False}
        with patch.object(sched_mod, "collect_store", return_value=(fake_stats, [])) as mocked:
            count = self.sched.tick()
        self.assertEqual(count, 0)
        self.assertEqual(mocked.call_count, 0)


if __name__ == "__main__":
    unittest.main()
