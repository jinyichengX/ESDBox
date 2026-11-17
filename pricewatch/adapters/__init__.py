"""适配器注册表。

探测顺序即优先级：WooCommerce 在前，因为它有官方公开接口，最稳定。
"""

from .base import (  # noqa: F401
    AdapterError,
    BaseAdapter,
    detect_adapter,
    normalize_base,
)
from .shopify import ShopifyAdapter
from .woocommerce import WooCommerceAdapter

ADAPTERS = [WooCommerceAdapter, ShopifyAdapter]

__all__ = [
    "AdapterError",
    "BaseAdapter",
    "detect_adapter",
    "normalize_base",
    "ADAPTERS",
    "WooCommerceAdapter",
    "ShopifyAdapter",
]
