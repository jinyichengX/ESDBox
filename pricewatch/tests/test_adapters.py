"""适配器解析测试：用固定样本，不联网。"""

import json
import unittest
from pathlib import Path

from adapters.woocommerce import WooCommerceAdapter

FIXTURES = json.loads(
    (Path(__file__).parent / "fixtures" / "woo_products.json").read_text(encoding="utf-8")
)


class TestWooCommerceParse(unittest.TestCase):
    def setUp(self):
        self.adapter = WooCommerceAdapter("https://shop.example.com")

    def test_normal_product(self):
        p = self.adapter._parse(FIXTURES[0])
        self.assertEqual(p.external_id, "101")
        self.assertEqual(p.name, "Classic T-Shirt")
        self.assertEqual(p.price, 2900)
        self.assertEqual(p.regular_price, 3900)
        self.assertTrue(p.in_stock)
        self.assertEqual(p.currency, "USD")
        self.assertEqual(p.price_display, "29.00 USD")

    def test_variable_product_uses_price_range(self):
        p = self.adapter._parse(FIXTURES[1])
        self.assertEqual(p.price, 5500)
        self.assertFalse(p.in_stock)

    def test_zero_price_is_kept(self):
        p = self.adapter._parse(FIXTURES[2])
        self.assertEqual(p.price, 0)

    def test_decimal_price_string(self):
        p = self.adapter._parse(FIXTURES[3])
        self.assertEqual(p.price, 1999)
        self.assertEqual(p.currency, "EUR")

    def test_invalid_items_are_skipped(self):
        self.assertIsNone(self.adapter._parse({"id": "", "name": ""}))
        self.assertIsNone(self.adapter._parse({"id": 5}))
        self.assertIsNone(self.adapter._parse("not a dict"))


if __name__ == "__main__":
    unittest.main()
