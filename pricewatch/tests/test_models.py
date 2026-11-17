"""金额转换测试。

这个模块错了会让所有价格显示错 100 倍，所以边界要全覆盖。
"""

import unittest

from models import format_money, format_time, to_minor


class TestToMinor(unittest.TestCase):
    def test_integer_minor_unit(self):
        self.assertEqual(to_minor("5900", 2), 5900)
        self.assertEqual(to_minor("0", 2), 0)

    def test_decimal_string(self):
        self.assertEqual(to_minor("19.99", 2), 1999)
        self.assertEqual(to_minor("19.9", 2), 1990)

    def test_zero_minor_unit(self):
        self.assertEqual(to_minor("1200", 0), 1200)

    def test_empty_and_invalid(self):
        self.assertEqual(to_minor("", 2), 0)
        self.assertEqual(to_minor(None, 2), 0)
        self.assertEqual(to_minor("abc", 2), 0)


class TestFormatMoney(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(format_money(2900, "USD", 2), "29.00 USD")
        self.assertEqual(format_money(0, "USD", 2), "0.00 USD")

    def test_large_number_has_separator(self):
        self.assertEqual(format_money(123456789, "USD", 2), "1,234,567.89 USD")

    def test_no_minor_unit(self):
        self.assertEqual(format_money(1200, "JPY", 0), "1,200 JPY")

    def test_none(self):
        self.assertEqual(format_money(None, "USD", 2), "-")

    def test_negative(self):
        self.assertEqual(format_money(-500, "USD", 2), "-5.00 USD")


class TestFormatTime(unittest.TestCase):
    def test_iso_to_short(self):
        self.assertEqual(format_time("2026-11-15T03:52:01"), "11-15 03:52")

    def test_empty(self):
        self.assertEqual(format_time(""), "-")

    def test_garbage_passthrough(self):
        self.assertEqual(format_time("bad"), "bad")


if __name__ == "__main__":
    unittest.main()
