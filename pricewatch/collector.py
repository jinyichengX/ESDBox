"""采集编排与变化检测。

核心规则：
1. 首次采集只建立基线，不产生告警，避免一上来就发几百条"新品上架"
2. 只在状态真正变化时写快照与事件，不变化就不落库
3. 本次没出现的商品标记为下架
"""

from typing import List, Optional, Tuple

from adapters import ADAPTERS, AdapterError, detect_adapter
from models import EventType, format_money


def _stat_key(etype: EventType) -> str:
    return {
        EventType.NEW_PRODUCT: "new",
        EventType.PRICE_DOWN: "price_down",
        EventType.PRICE_UP: "price_up",
        EventType.OUT_OF_STOCK: "out_of_stock",
        EventType.BACK_IN_STOCK: "back_in_stock",
        EventType.REMOVED: "removed",
    }[etype]


def collect_store(storage, store_row) -> Tuple[dict, List[dict]]:
    """采集单个店铺。返回 (统计, 事件列表)。

    事件列表用于发告警，只包含本次产生的事件。
    """
    store_id = int(store_row["id"])
    url = store_row["url"]

    stats = {
        "store_id": store_id,
        "store_label": store_row["label"],
        "total": 0,
        "new": 0,
        "price_down": 0,
        "price_up": 0,
        "out_of_stock": 0,
        "back_in_stock": 0,
        "removed": 0,
        "error": "",
        "baseline": False,
    }
    events: List[dict] = []

    adapter, reasons = detect_adapter(url, ADAPTERS)
    if adapter is None:
        error = "；".join(reasons) or "无法识别平台"
        storage.mark_store_checked(store_id, error)
        stats["error"] = error
        return stats, events

    try:
        products = list(adapter.fetch_products())
    except AdapterError as exc:
        storage.mark_store_checked(store_id, str(exc))
        stats["error"] = str(exc)
        return stats, events
    except Exception as exc:
        storage.mark_store_checked(store_id, exc.__class__.__name__)
        stats["error"] = exc.__class__.__name__
        return stats, events
    finally:
        adapter.close()

    is_baseline = not storage.has_products(store_id)
    stats["baseline"] = is_baseline

    seen = []
    for product in products:
        product_id, is_new = storage.upsert_product(store_id, product)
        seen.append(product.external_id)
        stats["total"] += 1

        last = storage.last_snapshot(product_id)

        # 首次采集：只建基线，不产生告警
        if is_baseline:
            storage.add_snapshot(product_id, product.price, product.regular_price, product.in_stock)
            continue

        changed = False

        def emit(etype: EventType, old_value: str, new_value: str) -> None:
            nonlocal changed
            event_id = storage.add_event(
                product_id, store_id, etype.value, old_value, new_value
            )
            events.append({
                "event_id": event_id,
                "product_name": product.name,
                "product_url": product.url,
                "type": etype,
                "old": old_value,
                "new": new_value,
            })
            stats[_stat_key(etype)] += 1
            changed = True

        # 基线之后第一次出现的商品 = 新品上架
        if last is None:
            storage.add_snapshot(product_id, product.price, product.regular_price, product.in_stock)
            emit(EventType.NEW_PRODUCT, "", product.price_display)
            continue

        old_price = int(last["price"])
        old_stock = bool(last["in_stock"])

        if old_price != product.price:
            etype = EventType.PRICE_DOWN if product.price < old_price else EventType.PRICE_UP
            emit(
                etype,
                format_money(old_price, product.currency, product.minor_unit),
                format_money(product.price, product.currency, product.minor_unit),
            )

        if old_stock != product.in_stock:
            emit(
                EventType.BACK_IN_STOCK if product.in_stock else EventType.OUT_OF_STOCK,
                "有货" if old_stock else "缺货",
                "有货" if product.in_stock else "缺货",
            )

        if changed:
            storage.add_snapshot(product_id, product.price, product.regular_price, product.in_stock)

    # 本次没出现的商品 = 下架
    gone = storage.deactivate_missing(store_id, seen)
    if not is_baseline:
        for product_id in gone:
            event_id = storage.add_event(product_id, store_id, EventType.REMOVED.value, "", "")
            events.append({
                "event_id": event_id,
                "product_name": f"商品 #{product_id}",
                "product_url": "",
                "type": EventType.REMOVED,
                "old": "",
                "new": "",
            })
            stats["removed"] += 1

    storage.mark_store_checked(store_id, "")
    return stats, events


def collect_all(storage) -> List[dict]:
    """采集所有启用中的店铺，返回每个店铺的统计。"""
    results = []
    for store in storage.all_active_stores():
        stats, _events = collect_store(storage, store)
        results.append(stats)
    return results
