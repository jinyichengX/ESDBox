"""数据模型与金额处理。

金额统一用「最小货币单位」的整数保存（如美元用分），
避免浮点误差；显示时再按小数位格式化。
"""

from dataclasses import dataclass
from enum import Enum
from typing import Optional


class EventType(str, Enum):
    NEW_PRODUCT = "new_product"
    PRICE_DOWN = "price_down"
    PRICE_UP = "price_up"
    OUT_OF_STOCK = "out_of_stock"
    BACK_IN_STOCK = "back_in_stock"
    REMOVED = "removed"


EVENT_LABELS = {
    EventType.NEW_PRODUCT: "新品上架",
    EventType.PRICE_DOWN: "降价",
    EventType.PRICE_UP: "涨价",
    EventType.OUT_OF_STOCK: "缺货",
    EventType.BACK_IN_STOCK: "补货",
    EventType.REMOVED: "下架",
}

EVENT_TONES = {
    EventType.NEW_PRODUCT: "info",
    EventType.PRICE_DOWN: "good",
    EventType.PRICE_UP: "warn",
    EventType.OUT_OF_STOCK: "bad",
    EventType.BACK_IN_STOCK: "good",
    EventType.REMOVED: "muted",
}


def to_minor(raw: Optional[str], minor_unit: int) -> int:
    """把接口返回的金额字符串转成最小单位整数。

    WooCommerce 的 price 字段本身就是最小单位（"5900" + 2 位小数 = 59.00），
    但部分实现会给出带小数点的写法，两种都要能吃。
    """
    if raw is None or raw == "":
        return 0
    try:
        text = str(raw).strip()
        if "." in text:
            value = float(text)
            return int(round(value * (10 ** minor_unit)))
        return int(text)
    except (TypeError, ValueError):
        return 0


def format_money(amount: int, currency: str = "", minor_unit: int = 2) -> str:
    """把最小单位整数格式化成人能读的金额。"""
    if amount is None:
        return "-"
    sign = "-" if amount < 0 else ""
    amount = abs(int(amount))
    if minor_unit <= 0:
        body = f"{amount:,}"
    else:
        scale = 10 ** minor_unit
        whole, frac = divmod(amount, scale)
        body = f"{whole:,}.{frac:0{minor_unit}d}"
    return f"{sign}{body} {currency}".strip()


def format_time(iso: str) -> str:
    """把 ISO 时间串转成报告里易读的格式。"""
    if not iso:
        return "-"
    try:
        from datetime import datetime
        return datetime.fromisoformat(iso).strftime("%m-%d %H:%M")
    except (ValueError, TypeError):
        return str(iso)[:16].replace("T", " ")


@dataclass
class ProductData:
    """适配器统一输出的商品结构，与具体平台解耦。"""
    external_id: str
    name: str
    url: str = ""
    price: int = 0
    regular_price: int = 0
    in_stock: bool = True
    currency: str = ""
    minor_unit: int = 2
    sku: str = ""

    @property
    def price_display(self) -> str:
        return format_money(self.price, self.currency, self.minor_unit)


@dataclass
class ChangeEvent:
    """一次商品状态变化。"""
    product_id: int
    product_name: str
    store_id: int
    store_label: str
    type: EventType
    old_value: str = ""
    new_value: str = ""
    product_url: str = ""
    event_id: int = 0
    created_at: str = ""

    @property
    def label(self) -> str:
        return EVENT_LABELS.get(self.type, self.type.value)

    @property
    def tone(self) -> str:
        return EVENT_TONES.get(self.type, "info")


@dataclass
class Snapshot:
    """某个时刻的商品状态。"""
    product_id: int
    price: int
    regular_price: int
    in_stock: bool
    created_at: str = ""
