"""SQLite 仓储层。

每次操作独立开关连接：Web 是多线程的，而本产品写入频率很低，
维护连接池带来的复杂度和线程安全问题不值得。
"""

import hashlib
import secrets
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timedelta
from pathlib import Path
from typing import List, Optional, Tuple

import config

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    email         TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    plan          TEXT NOT NULL DEFAULT 'free',
    created_at    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS stores (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    url             TEXT NOT NULL,
    platform        TEXT NOT NULL,
    label           TEXT NOT NULL,
    currency        TEXT DEFAULT '',
    minor_unit      INTEGER DEFAULT 2,
    active          INTEGER NOT NULL DEFAULT 1,
    interval_min    INTEGER NOT NULL DEFAULT 60,
    last_checked_at TEXT,
    last_error      TEXT DEFAULT '',
    created_at      TEXT NOT NULL,
    UNIQUE(user_id, url)
);

CREATE TABLE IF NOT EXISTS products (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    store_id    INTEGER NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
    external_id TEXT NOT NULL,
    name        TEXT NOT NULL,
    url         TEXT DEFAULT '',
    currency    TEXT DEFAULT '',
    minor_unit  INTEGER DEFAULT 2,
    active      INTEGER NOT NULL DEFAULT 1,
    first_seen  TEXT NOT NULL,
    last_seen   TEXT NOT NULL,
    UNIQUE(store_id, external_id)
);

CREATE TABLE IF NOT EXISTS snapshots (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    product_id    INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,
    price         INTEGER NOT NULL,
    regular_price INTEGER NOT NULL DEFAULT 0,
    in_stock      INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_snap_product ON snapshots(product_id, id DESC);

CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,
    store_id   INTEGER NOT NULL REFERENCES stores(id) ON DELETE CASCADE,
    type       TEXT NOT NULL,
    old_value  TEXT DEFAULT '',
    new_value  TEXT DEFAULT '',
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_events_store ON events(store_id, id DESC);

CREATE TABLE IF NOT EXISTS alerts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id   INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    channel    TEXT NOT NULL,
    target     TEXT NOT NULL,
    status     TEXT NOT NULL,
    error      TEXT DEFAULT '',
    created_at TEXT NOT NULL
);
"""


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 200_000)
    return f"pbkdf2$200000${salt.hex()}${dk.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        algo, rounds, salt_hex, hash_hex = stored.split("$")
        if algo != "pbkdf2":
            return False
        dk = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), bytes.fromhex(salt_hex), int(rounds)
        )
        return secrets.compare_digest(dk.hex(), hash_hex)
    except Exception:
        return False


class Storage:
    def __init__(self, path: Optional[str] = None) -> None:
        self.path = str(path or config.DB_PATH)
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        with self._conn() as c:
            c.executescript(SCHEMA)

    @contextmanager
    def _conn(self):
        conn = sqlite3.connect(self.path, timeout=15)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON")
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()

    # ---------------- 用户 ----------------
    def create_user(self, email: str, password: str, plan: str = "free") -> int:
        with self._conn() as c:
            cur = c.execute(
                "INSERT INTO users(email, password_hash, plan, created_at) VALUES(?,?,?,?)",
                (email.strip().lower(), hash_password(password), plan, now_iso()),
            )
            return int(cur.lastrowid)

    def get_user_by_email(self, email: str):
        with self._conn() as c:
            return c.execute(
                "SELECT * FROM users WHERE email = ?", (email.strip().lower(),)
            ).fetchone()

    def get_user(self, user_id: int):
        with self._conn() as c:
            return c.execute("SELECT * FROM users WHERE id = ?", (user_id,)).fetchone()

    def set_user_plan(self, user_id: int, plan: str) -> None:
        if plan not in config.PLANS:
            raise ValueError("未知套餐")
        with self._conn() as c:
            c.execute("UPDATE users SET plan = ? WHERE id = ?", (plan, user_id))

    # ---------------- 会话 ----------------
    def create_session(self, user_id: int) -> str:
        token = secrets.token_urlsafe(32)
        expires = (datetime.now() + timedelta(days=config.SESSION_DAYS)).isoformat(timespec="seconds")
        with self._conn() as c:
            c.execute(
                "INSERT INTO sessions(token, user_id, expires_at) VALUES(?,?,?)",
                (token, user_id, expires),
            )
        return token

    def get_session_user(self, token: str):
        if not token:
            return None
        with self._conn() as c:
            return c.execute(
                """SELECT u.* FROM sessions s JOIN users u ON u.id = s.user_id
                   WHERE s.token = ? AND s.expires_at > ?""",
                (token, now_iso()),
            ).fetchone()

    def delete_session(self, token: str) -> None:
        with self._conn() as c:
            c.execute("DELETE FROM sessions WHERE token = ?", (token,))

    # ---------------- 店铺 ----------------
    def add_store(
        self,
        user_id: int,
        url: str,
        platform: str,
        label: str,
        currency: str = "",
        minor_unit: int = 2,
        interval_min: Optional[int] = None,
    ) -> int:
        interval = max(config.MIN_INTERVAL_MIN, interval_min or config.DEFAULT_INTERVAL_MIN)
        with self._conn() as c:
            cur = c.execute(
                """INSERT INTO stores(user_id, url, platform, label, currency,
                                      minor_unit, interval_min, created_at)
                   VALUES(?,?,?,?,?,?,?,?)""",
                (user_id, url, platform, label, currency, minor_unit, interval, now_iso()),
            )
            return int(cur.lastrowid)

    def list_stores(self, user_id: int) -> List[sqlite3.Row]:
        with self._conn() as c:
            return c.execute(
                """SELECT s.*,
                          (SELECT COUNT(*) FROM products p
                            WHERE p.store_id = s.id AND p.active = 1) AS product_count,
                          (SELECT COUNT(*) FROM events e
                            WHERE e.store_id = s.id) AS event_count,
                          (SELECT MAX(created_at) FROM events e
                            WHERE e.store_id = s.id) AS last_event_at
                   FROM stores s WHERE s.user_id = ? ORDER BY s.id DESC""",
                (user_id,),
            ).fetchall()

    def get_store(self, store_id: int, user_id: int):
        with self._conn() as c:
            return c.execute(
                "SELECT * FROM stores WHERE id = ? AND user_id = ?", (store_id, user_id)
            ).fetchone()

    def count_stores(self, user_id: int) -> int:
        with self._conn() as c:
            row = c.execute(
                "SELECT COUNT(*) AS n FROM stores WHERE user_id = ?", (user_id,)
            ).fetchone()
            return int(row["n"])

    def delete_store(self, store_id: int, user_id: int) -> None:
        with self._conn() as c:
            c.execute("DELETE FROM stores WHERE id = ? AND user_id = ?", (store_id, user_id))

    def all_active_stores(self) -> List[sqlite3.Row]:
        with self._conn() as c:
            return c.execute("SELECT * FROM stores WHERE active = 1").fetchall()

    def mark_store_checked(self, store_id: int, error: str = "") -> None:
        with self._conn() as c:
            c.execute(
                "UPDATE stores SET last_checked_at = ?, last_error = ? WHERE id = ?",
                (now_iso(), error, store_id),
            )

    # ---------------- 商品 ----------------
    def upsert_product(self, store_id: int, product) -> Tuple[int, bool]:
        """写入商品，返回 (product_id, 是否新商品)。"""
        ts = now_iso()
        with self._conn() as c:
            row = c.execute(
                "SELECT id FROM products WHERE store_id = ? AND external_id = ?",
                (store_id, product.external_id),
            ).fetchone()
            if row:
                c.execute(
                    """UPDATE products SET name = ?, url = ?, currency = ?, minor_unit = ?,
                                           active = 1, last_seen = ? WHERE id = ?""",
                    (product.name, product.url, product.currency, product.minor_unit, ts, row["id"]),
                )
                return int(row["id"]), False
            cur = c.execute(
                """INSERT INTO products(store_id, external_id, name, url, currency,
                                        minor_unit, first_seen, last_seen)
                   VALUES(?,?,?,?,?,?,?,?)""",
                (store_id, product.external_id, product.name, product.url,
                 product.currency, product.minor_unit, ts, ts),
            )
            return int(cur.lastrowid), True

    def has_products(self, store_id: int) -> bool:
        """判断该店铺是否已有基线商品（用于区分首次采集）。"""
        with self._conn() as c:
            row = c.execute(
                "SELECT 1 FROM products WHERE store_id = ? LIMIT 1", (store_id,)
            ).fetchone()
            return row is not None

    def count_products(self, store_id: int) -> int:
        with self._conn() as c:
            row = c.execute(
                "SELECT COUNT(*) AS n FROM products WHERE store_id = ? AND active = 1",
                (store_id,),
            ).fetchone()
            return int(row["n"])

    def list_products(self, store_id: int, limit: int = 100, offset: int = 0) -> List[sqlite3.Row]:
        with self._conn() as c:
            return c.execute(
                """SELECT p.*,
                          (SELECT price FROM snapshots s WHERE s.product_id = p.id
                            ORDER BY s.id DESC LIMIT 1) AS last_price,
                          (SELECT in_stock FROM snapshots s WHERE s.product_id = p.id
                            ORDER BY s.id DESC LIMIT 1) AS last_in_stock,
                          (SELECT COUNT(*) FROM events e WHERE e.product_id = p.id) AS event_count
                   FROM products p WHERE p.store_id = ? AND p.active = 1
                   ORDER BY p.name LIMIT ? OFFSET ?""",
                (store_id, limit, offset),
            ).fetchall()

    def deactivate_missing(self, store_id: int, keep_external_ids: List[str]) -> List[int]:
        """把本次没出现的商品标记为下架，返回被标记的 product_id 列表。"""
        with self._conn() as c:
            rows = c.execute(
                "SELECT id, external_id FROM products WHERE store_id = ? AND active = 1",
                (store_id,),
            ).fetchall()
            keep = set(keep_external_ids)
            gone = [int(r["id"]) for r in rows if r["external_id"] not in keep]
            for pid in gone:
                c.execute("UPDATE products SET active = 0 WHERE id = ?", (pid,))
            return gone

    # ---------------- 快照 ----------------
    def last_snapshot(self, product_id: int):
        with self._conn() as c:
            return c.execute(
                "SELECT * FROM snapshots WHERE product_id = ? ORDER BY id DESC LIMIT 1",
                (product_id,),
            ).fetchone()

    def add_snapshot(self, product_id: int, price: int, regular_price: int, in_stock: bool) -> int:
        with self._conn() as c:
            cur = c.execute(
                """INSERT INTO snapshots(product_id, price, regular_price, in_stock, created_at)
                   VALUES(?,?,?,?,?)""",
                (product_id, price, regular_price, 1 if in_stock else 0, now_iso()),
            )
            return int(cur.lastrowid)

    def price_history(self, product_id: int, limit: int = 200) -> List[sqlite3.Row]:
        with self._conn() as c:
            rows = c.execute(
                "SELECT * FROM snapshots WHERE product_id = ? ORDER BY id DESC LIMIT ?",
                (product_id, limit),
            ).fetchall()
            return list(reversed(rows))

    # ---------------- 事件 ----------------
    def add_event(self, product_id: int, store_id: int, etype: str,
                  old_value: str = "", new_value: str = "") -> int:
        with self._conn() as c:
            cur = c.execute(
                """INSERT INTO events(product_id, store_id, type, old_value, new_value, created_at)
                   VALUES(?,?,?,?,?,?)""",
                (product_id, store_id, etype, old_value, new_value, now_iso()),
            )
            return int(cur.lastrowid)

    def list_events(self, user_id: int, store_id: Optional[int] = None,
                    etype: str = "", limit: int = 200) -> List[sqlite3.Row]:
        sql = [
            """SELECT e.*, p.name AS product_name, p.url AS product_url,
                      p.minor_unit AS minor_unit, p.currency AS currency,
                      s.label AS store_label
               FROM events e
               JOIN products p ON p.id = e.product_id
               JOIN stores s ON s.id = e.store_id
               WHERE s.user_id = ?"""
        ]
        params: list = [user_id]
        if store_id:
            sql.append("AND e.store_id = ?")
            params.append(store_id)
        if etype:
            sql.append("AND e.type = ?")
            params.append(etype)
        sql.append("ORDER BY e.id DESC LIMIT ?")
        params.append(limit)
        with self._conn() as c:
            return c.execute(" ".join(sql), params).fetchall()

    # ---------------- 告警 ----------------
    def add_alert(self, event_id: int, channel: str, target: str,
                  status: str, error: str = "") -> int:
        with self._conn() as c:
            cur = c.execute(
                """INSERT INTO alerts(event_id, channel, target, status, error, created_at)
                   VALUES(?,?,?,?,?,?)""",
                (event_id, channel, target, status, error, now_iso()),
            )
            return int(cur.lastrowid)

    # ---------------- 统计 ----------------
    def dashboard(self, user_id: int) -> dict:
        today = datetime.now().date().isoformat()
        with self._conn() as c:
            stores = c.execute(
                "SELECT COUNT(*) AS n FROM stores WHERE user_id = ?", (user_id,)
            ).fetchone()["n"]
            products = c.execute(
                """SELECT COUNT(*) AS n FROM products p
                   JOIN stores s ON s.id = p.store_id
                   WHERE s.user_id = ? AND p.active = 1""",
                (user_id,),
            ).fetchone()["n"]
            today_events = c.execute(
                """SELECT COUNT(*) AS n FROM events e
                   JOIN stores s ON s.id = e.store_id
                   WHERE s.user_id = ? AND e.created_at LIKE ?""",
                (user_id, today + "%"),
            ).fetchone()["n"]
            price_drops = c.execute(
                """SELECT COUNT(*) AS n FROM events e
                   JOIN stores s ON s.id = e.store_id
                   WHERE s.user_id = ? AND e.type = 'price_down' AND e.created_at LIKE ?""",
                (user_id, today + "%"),
            ).fetchone()["n"]
        return {
            "stores": int(stores),
            "products": int(products),
            "today_events": int(today_events),
            "price_drops": int(price_drops),
        }
