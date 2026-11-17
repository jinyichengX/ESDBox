"""变化检测测试：用假适配器，完全不联网。"""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import collector
from models import EventType, ProductData
from storage import Storage


class FakeAdapter:
    platform = "woocommerce"
    platform_label = "WooCommerce"

    def __init__(self, products):
        self._products = products

    def fetch_products(self):
        return iter(self._products)

    def close(self):
        pass


class CollectorTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.storage = Storage(str(Path(self.tmp.name) / "collect.db"))
        uid = self.storage.create_user("owner@shop.com", "password123")
        sid = self.storage.add_store(uid, "https://shop.example.com", "woocommerce", "测试店")
        self.store = self.storage.get_store(sid, uid)

    def tearDown(self):
        self.tmp.cleanup()

    def _collect(self, products):
        with patch.object(collector, "detect_adapter", return_value=(FakeAdapter(products), [])):
            return collector.collect_store(self.storage, self.store)

    def test_baseline_produces_no_events(self):
        stats, events = self._collect([ProductData("1", "A", price=100)])
        self.assertTrue(stats["baseline"])
        self.assertEqual(stats["total"], 1)
        self.assertEqual(len(events), 0)

    def test_price_down(self):
        self._collect([ProductData("1", "A", price=200)])
        stats, events = self._collect([ProductData("1", "A", price=150)])
        self.assertEqual(stats["price_down"], 1)
        self.assertEqual(events[0]["type"], EventType.PRICE_DOWN)
        self.assertIn("1.50", events[0]["new"])

    def test_price_up(self):
        self._collect([ProductData("1", "A", price=100)])
        stats, _ = self._collect([ProductData("1", "A", price=300)])
        self.assertEqual(stats["price_up"], 1)

    def test_out_of_stock_then_back(self):
        self._collect([ProductData("1", "A", price=100, in_stock=True)])
        stats, _ = self._collect([ProductData("1", "A", price=100, in_stock=False)])
        self.assertEqual(stats["out_of_stock"], 1)
        stats, _ = self._collect([ProductData("1", "A", price=100, in_stock=True)])
        self.assertEqual(stats["back_in_stock"], 1)

    def test_unchanged_produces_nothing(self):
        self._collect([ProductData("1", "A", price=100)])
        stats, events = self._collect([ProductData("1", "A", price=100)])
        self.assertEqual(len(events), 0)
        self.assertEqual(stats["total"], 1)

    def test_new_product_after_baseline(self):
        self._collect([ProductData("1", "A", price=100)])
        stats, _ = self._collect([
            ProductData("1", "A", price=100),
            ProductData("2", "B", price=500),
        ])
        self.assertEqual(stats["new"], 1)

    def test_removed_product(self):
        self._collect([
            ProductData("1", "A", price=100),
            ProductData("2", "B", price=200),
        ])
        stats, _ = self._collect([ProductData("1", "A", price=100)])
        self.assertEqual(stats["removed"], 1)

    def test_adapter_failure_is_recorded(self):
        with patch.object(collector, "detect_adapter",
                          return_value=(None, ["WooCommerce：HTTP 403"])):
            stats, events = collector.collect_store(self.storage, self.store)
        self.assertIn("403", stats["error"])
        self.assertEqual(len(events), 0)

    def test_events_are_queryable(self):
        uid = self.storage.get_user_by_email("owner@shop.com")["id"]
        self._collect([ProductData("1", "A", price=200)])
        self._collect([ProductData("1", "A", price=100)])
        rows = self.storage.list_events(uid)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["type"], "price_down")
        self.assertEqual(rows[0]["product_name"], "A")


if __name__ == "__main__":
    unittest.main()
