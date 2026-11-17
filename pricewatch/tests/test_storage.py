"""仓储层测试：用临时数据库，覆盖增删改查与级联删除。"""

import tempfile
import unittest
from pathlib import Path

from models import ProductData
from storage import Storage, verify_password


class StorageTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = str(Path(self.tmp.name) / "test.db")
        self.storage = Storage(self.db)

    def tearDown(self):
        self.tmp.cleanup()

    def _make_user(self, email="owner@shop.com"):
        uid = self.storage.create_user(email, "password123")
        return uid

    def _make_store(self, uid, url="https://shop.example.com"):
        return self.storage.add_store(uid, url, "woocommerce", "测试店")


class TestUsers(StorageTestCase):
    def test_create_and_lookup(self):
        uid = self._make_user()
        user = self.storage.get_user_by_email("owner@shop.com")
        self.assertEqual(user["id"], uid)
        self.assertEqual(user["plan"], "free")

    def test_password_verify(self):
        self._make_user()
        user = self.storage.get_user_by_email("owner@shop.com")
        self.assertTrue(verify_password("password123", user["password_hash"]))
        self.assertFalse(verify_password("wrong", user["password_hash"]))

    def test_email_is_normalized(self):
        self._make_user("Owner@Shop.COM")
        self.assertIsNotNone(self.storage.get_user_by_email("owner@shop.com"))


class TestSessions(StorageTestCase):
    def test_session_roundtrip(self):
        uid = self._make_user()
        token = self.storage.create_session(uid)
        user = self.storage.get_session_user(token)
        self.assertEqual(user["id"], uid)

    def test_logout_invalidates(self):
        uid = self._make_user()
        token = self.storage.create_session(uid)
        self.storage.delete_session(token)
        self.assertIsNone(self.storage.get_session_user(token))

    def test_unknown_token(self):
        self.assertIsNone(self.storage.get_session_user("nope"))


class TestStores(StorageTestCase):
    def test_add_and_count(self):
        uid = self._make_user()
        self._make_store(uid)
        self.assertEqual(self.storage.count_stores(uid), 1)

    def test_duplicate_url_rejected(self):
        uid = self._make_user()
        self._make_store(uid)
        with self.assertRaises(Exception):
            self._make_store(uid)

    def test_delete_cascades(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        product = ProductData(external_id="1", name="A", price=100)
        pid, _ = self.storage.upsert_product(sid, product)
        self.storage.add_snapshot(pid, 100, 100, True)
        self.storage.add_event(pid, sid, "price_down", "2.00 USD", "1.00 USD")

        self.storage.delete_store(sid, uid)
        self.assertEqual(self.storage.count_stores(uid), 0)
        self.assertIsNone(self.storage.last_snapshot(pid))


class TestProducts(StorageTestCase):
    def test_upsert_creates_then_updates(self):
        uid = self._make_user()
        sid = self._make_store(uid)

        pid, is_new = self.storage.upsert_product(sid, ProductData("A1", "Alpha", price=100))
        self.assertTrue(is_new)
        self.assertEqual(pid, pid)

        pid2, is_new2 = self.storage.upsert_product(sid, ProductData("A1", "Alpha V2", price=200))
        self.assertFalse(is_new2)
        self.assertEqual(pid, pid2)

        rows = self.storage.list_products(sid)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["name"], "Alpha V2")

    def test_has_products(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        self.assertFalse(self.storage.has_products(sid))
        self.storage.upsert_product(sid, ProductData("A1", "Alpha"))
        self.assertTrue(self.storage.has_products(sid))

    def test_deactivate_missing(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        self.storage.upsert_product(sid, ProductData("A1", "Alpha"))
        self.storage.upsert_product(sid, ProductData("A2", "Beta"))

        gone = self.storage.deactivate_missing(sid, ["A1"])
        self.assertEqual(len(gone), 1)
        self.assertEqual(len(self.storage.list_products(sid)), 1)

    def test_pagination(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        for i in range(5):
            self.storage.upsert_product(sid, ProductData(f"P{i}", f"Name{i}"))
        self.assertEqual(self.storage.count_products(sid), 5)
        page1 = self.storage.list_products(sid, limit=2, offset=0)
        page2 = self.storage.list_products(sid, limit=2, offset=2)
        self.assertEqual(len(page1), 2)
        self.assertEqual(len(page2), 2)
        self.assertNotEqual(page1[0]["id"], page2[0]["id"])

    def test_price_history_order(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        pid, _ = self.storage.upsert_product(sid, ProductData("A1", "Alpha"))
        for price in (300, 200, 100):
            self.storage.add_snapshot(pid, price, price, True)
        history = self.storage.price_history(pid)
        self.assertEqual([r["price"] for r in history], [300, 200, 100])


class TestDashboard(StorageTestCase):
    def test_counts(self):
        uid = self._make_user()
        sid = self._make_store(uid)
        pid, _ = self.storage.upsert_product(sid, ProductData("A1", "Alpha"))
        self.storage.add_event(pid, sid, "price_down", "2.00", "1.00")

        stats = self.storage.dashboard(uid)
        self.assertEqual(stats["stores"], 1)
        self.assertEqual(stats["products"], 1)
        self.assertEqual(stats["today_events"], 1)
        self.assertEqual(stats["price_drops"], 1)


if __name__ == "__main__":
    unittest.main()
