"""Shopify 适配器（备用）。

数据来源：/products.json，公开接口，不需要登录。
已知限制：该接口不返回币种，只能用店铺默认货币兜底；
部分店铺会返回 403 或 429，此时本适配器不可用。
"""

from typing import Iterator

import config
from models import ProductData, to_minor

from .base import BaseAdapter


class ShopifyAdapter(BaseAdapter):
    platform = "shopify"
    platform_label = "Shopify"

    API = "/products.json"

    def probe(self) -> bool:
        data = self._get_json(self.API, {"limit": 1})
        return isinstance(data, dict) and "products" in data

    def fetch_products(self) -> Iterator[ProductData]:
        page = 1
        emitted = 0
        while page <= config.MAX_PAGES:
            data = self._get_json(self.API, {"limit": 250, "page": page})
            products = (data or {}).get("products") or []
            if not products:
                break

            for product in products:
                for variant in product.get("variants") or []:
                    if not variant.get("id"):
                        continue
                    title = (product.get("title") or "").strip()
                    variant_title = (variant.get("title") or "").strip()
                    if variant_title and variant_title != "Default Title":
                        name = f"{title} - {variant_title}"
                    else:
                        name = title

                    handle = product.get("handle") or ""
                    yield ProductData(
                        external_id=f"{product.get('id')}:{variant.get('id')}",
                        name=name,
                        url=f"{self.base_url}/products/{handle}" if handle else "",
                        price=to_minor(variant.get("price"), 2),
                        regular_price=to_minor(variant.get("compare_at_price"), 2),
                        in_stock=bool(variant.get("available", True)),
                        currency="",
                        minor_unit=2,
                        sku=(variant.get("sku") or "").strip(),
                    )
                    emitted += 1
                    if emitted >= config.MAX_PRODUCTS_PER_STORE:
                        return

            if len(products) < 250:
                break
            page += 1
