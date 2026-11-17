"""全局配置。所有可调参数集中在这里，不散落到业务代码。"""

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
DB_PATH = os.environ.get("PRICEWATCH_DB", str(DATA_DIR / "pricewatch.db"))

# ---------------- 采集 ----------------
USER_AGENT = "PriceWatchBot/1.0 (+https://pricewatch.example.com/bot)"
TIMEOUT = 20
RETRIES = 2
PAGE_SIZE = 100
MAX_PAGES = 20
MAX_PRODUCTS_PER_STORE = 2000
REQUEST_DELAY = 0.5          # 同一域名两次请求的最小间隔（秒）

DEFAULT_INTERVAL_MIN = 60    # 每个店铺的默认采集间隔（分钟）
MIN_INTERVAL_MIN = 15        # 防止把别人的站打崩

# ---------------- 会话 ----------------
SESSION_DAYS = 14
COOKIE_NAME = "pw_session"

# ---------------- 套餐 ----------------
PLANS = {
    "free": {
        "name": "免费",
        "store_limit": 1,
        "price_usd": 0,
        "history_days": 7,
    },
    "starter": {
        "name": "标准",
        "store_limit": 5,
        "price_usd": 19,
        "history_days": 90,
    },
    "pro": {
        "name": "专业",
        "store_limit": 20,
        "price_usd": 49,
        "history_days": 365,
    },
}

# ---------------- 告警邮件 ----------------
SMTP_HOST = os.environ.get("PRICEWATCH_SMTP_HOST", "")
SMTP_PORT = int(os.environ.get("PRICEWATCH_SMTP_PORT", "587") or 587)
SMTP_USER = os.environ.get("PRICEWATCH_SMTP_USER", "")
SMTP_PASSWORD = os.environ.get("PRICEWATCH_SMTP_PASSWORD", "")
SMTP_FROM = os.environ.get("PRICEWATCH_SMTP_FROM", "alerts@pricewatch.local")
SMTP_TLS = os.environ.get("PRICEWATCH_SMTP_TLS", "1") != "0"

# 未配置 SMTP 时，告警只落库并在控制台打印，便于本地验证
ALERT_EMAIL_ENABLED = bool(SMTP_HOST)

# ---------------- 服务 ----------------
HOST = os.environ.get("PRICEWATCH_HOST", "127.0.0.1")
PORT = int(os.environ.get("PRICEWATCH_PORT", "8000") or 8000)
