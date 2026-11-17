"""WooCommerce 适配器。

数据来源是官方公开的 Store API：
    GET /wp-json/wc/store/products?per_page=100&page=N
不需要登录，也不绕过任何防护，符合对方的公开约定。

金额字段说明：
    prices.price 是「最小货币单位」的字符串，配合 prices.currency_minor_unit 使用。
    例如 price="5900"、minor_unit=2，表示 59.00。
"""

from typing import Iterator

import config
from models import ProductData, to_minor

from .base import AdapterError, BaseAdapter


class WooCommerceAdapter(BaseAdapter):
    platform = "woocommerce"
    platform_label = "WooCommerce"

    API = "/wp-json/wc/store/products"

    def probe(self) -> bool:
        data = self._get_json(self.API, {"per_page": 1})
        return isinstance(data, list)

    def fetch_products(self) -> Iterator[ProductData]:
        page = 1
        emitted = 0
        while page <= config.MAX_PAGES:
            data = self._get_json(
                self.API,
                {"per_page": config.PAGE_SIZE, "page": page, "orderby": "id", "order": "asc"},
            )
            if not isinstance(data, list) or not data:
                break

            for item in data:
                product = self._parse(item)
                if product is None:
                    continue
                yield product
                emitted += 1
                if emitted >= config.MAX_PRODUCTS_PER_STORE:
                    return

            if len(data) < config.PAGE_SIZE:
                break
            page += 1

    def _parse(self, item: dict):
        if not isinstance(item, dict):
            return None
        external_id = str(item.get("id") or "").strip()
        name = (item.get("name") or "").strip()
        if not external_id or not name:
            return None

        prices = item.get("prices") or {}
        try:
            minor = int(prices.get("currency_minor_unit", 2))
        except (TypeError, ValueError):
            minor = 2

        price = to_minor(prices.get("price"), minor)
        if price == 0:
            # 可变商品可能只给价格区间，取最低档作为代表价格
            price_range = prices.get("price_range") or {}
            if price_range:
                price = to_minor(price_range.get("min_amount"), minor)

        in_stock = item.get("is_in_stock")
        if in_stock is None:
            stock_class = str((item.get("stock_availability") or {}).get("class", ""))
            in_stock = stock_class != "out-of-stock"

        return ProductData(
            external_id=external_id,
            name=name,
            url=item.get("permalink") or "",
            price=price,
            regular_price=to_minor(prices.get("regular_price"), minor),
            in_stock=bool(in_stock),
            currency=prices.get("currency_code") or "",
            minor_unit=minor,
            sku=(item.get("sku") or "").strip(),
        )
