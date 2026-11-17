"""适配器基类：统一请求、限速、重试与平台识别。

新增平台只需实现 fetch_products() 和 probe()，上层采集与告警逻辑不用改。
"""

import time
from typing import Iterator, List, Optional, Type
from urllib.parse import urlsplit, urlunsplit

import requests

import config
from models import ProductData


class AdapterError(Exception):
    """适配器无法完成请求，且原因需要告知用户。"""


def normalize_base(raw: str) -> str:
    """把用户输入的店铺地址整理成 scheme + host 形式。

    只保留协议和域名，去掉路径，因为接口都挂在根路径下。
    """
    raw = (raw or "").strip()
    if not raw:
        raise AdapterError("店铺地址为空")
    if "://" not in raw:
        raw = "https://" + raw
    parts = urlsplit(raw)
    if parts.scheme not in ("http", "https"):
        raise AdapterError("只支持 http / https 地址")
    if not parts.netloc:
        raise AdapterError("地址缺少域名")
    return urlunsplit((parts.scheme, parts.netloc, "", "", ""))


class BaseAdapter:
    platform = "unknown"
    platform_label = "未知"

    def __init__(self, base_url: str) -> None:
        self.base_url = normalize_base(base_url)
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": config.USER_AGENT,
            "Accept": "application/json, text/plain, */*",
            "Accept-Language": "en-US,en;q=0.9,zh-CN;q=0.8",
        })
        self._last_request = 0.0

    # ---------- 请求工具 ----------
    def _respect_delay(self) -> None:
        gap = config.REQUEST_DELAY - (time.monotonic() - self._last_request)
        if gap > 0:
            time.sleep(gap)
        self._last_request = time.monotonic()

    def _get_json(self, path: str, params: Optional[dict] = None):
        url = path if path.startswith("http") else self.base_url + path
        last_error = ""
        for attempt in range(config.RETRIES + 1):
            try:
                self._respect_delay()
                resp = self.session.get(url, params=params, timeout=config.TIMEOUT)
                if resp.status_code == 429:
                    last_error = "对方站点限流（429）"
                    time.sleep(2 * (attempt + 1))
                    continue
                if resp.status_code >= 400:
                    last_error = f"HTTP {resp.status_code}"
                    if attempt == config.RETRIES:
                        raise AdapterError(last_error)
                    time.sleep(1.2 * (attempt + 1))
                    continue
                return resp.json()
            except ValueError:
                last_error = "返回内容不是合法 JSON"
            except requests.RequestException as exc:
                last_error = exc.__class__.__name__
            if attempt == config.RETRIES:
                break
            time.sleep(1.2 * (attempt + 1))
        raise AdapterError(last_error or "请求失败")

    # ---------- 子类实现 ----------
    def probe(self) -> bool:
        """判断这个店铺是否属于本适配器的平台。"""
        return False

    def fetch_products(self) -> Iterator[ProductData]:   # pragma: no cover
        raise NotImplementedError

    def close(self) -> None:
        self.session.close()


def detect_adapter(base_url: str, candidates: List[Type[BaseAdapter]]):
    """依次探测候选适配器，返回第一个能用的实例。

    全部失败时返回 (None, 错误信息列表)，方便前端把原因展示给用户。
    """
    reasons = []
    for cls in candidates:
        adapter = None
        try:
            adapter = cls(base_url)
            if adapter.probe():
                return adapter, reasons
            reasons.append(f"{cls.platform_label}：接口不可用")
        except AdapterError as exc:
            reasons.append(f"{cls.platform_label}：{exc}")
        except Exception as exc:            # 网络异常等
            reasons.append(f"{cls.platform_label}：{exc.__class__.__name__}")
        finally:
            if adapter is not None:
                adapter.close()
    return None, reasons
