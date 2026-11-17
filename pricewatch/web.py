"""零依赖 Web 服务：标准库 http.server + jinja2 模板。

不用 Flask 的理由：这个产品要能被不懂运维的人直接跑起来，
少一个第三方依赖就少一道部署门槛。
"""

import re
import traceback
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional

from jinja2 import Environment, FileSystemLoader, select_autoescape

import config
from adapters import AdapterError, detect_adapter, normalize_base, ADAPTERS
from collector import collect_store
from models import EVENT_LABELS, EVENT_TONES, format_money, format_time
from storage import Storage

BASE = Path(__file__).resolve().parent
_env = Environment(
    loader=FileSystemLoader(str(BASE / "templates")),
    autoescape=select_autoescape(["html", "xml"]),
    trim_blocks=True,
    lstrip_blocks=True,
)

STORE: Optional[Storage] = None
SCHEDULER = None


class Handler(BaseHTTPRequestHandler):
    server_version = "PriceWatch/1.0"
    protocol_version = "HTTP/1.1"

    # ---------------- 响应 ----------------
    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _html(self, text: str, code: int = 200) -> None:
        self._send(code, text.encode("utf-8"), "text/html; charset=utf-8")

    def _json(self, payload, code: int = 200) -> None:
        import json
        self._send(code, json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8"),
                   "application/json; charset=utf-8")

    def _redirect(self, location: str, cookie: str = "") -> None:
        self.send_response(302)
        self.send_header("Location", location)
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.send_header("Content-Length", "0")
        self.end_headers()

    # ---------------- 会话与表单 ----------------
    def _cookie(self, name: str) -> str:
        raw = self.headers.get("Cookie") or ""
        for part in raw.split(";"):
            if "=" in part:
                k, v = part.strip().split("=", 1)
                if k == name:
                    return urllib.parse.unquote(v)
        return ""

    def _user(self):
        token = self._cookie(config.COOKIE_NAME)
        return STORE.get_session_user(token) if token else None

    def _form(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        raw = self.rfile.read(length).decode("utf-8", errors="replace")
        return {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}

    def _render(self, template: str, code: int = 200, **ctx) -> None:
        ctx.setdefault("user", self._user())
        ctx.setdefault("plans", config.PLANS)
        # 数据库里存的是字符串，模板里也用字符串查，避免枚举与 str 不匹配
        ctx.setdefault("event_labels", {k.value: v for k, v in EVENT_LABELS.items()})
        ctx.setdefault("event_tones", {k.value: v for k, v in EVENT_TONES.items()})
        ctx.setdefault("fmt", format_money)
        ctx.setdefault("ft", format_time)
        text = _env.get_template(template).render(**ctx)
        self._html(text, code)

    # ---------------- 路由 ----------------
    def do_GET(self) -> None:
        try:
            self._route_get()
        except Exception:
            traceback.print_exc()
            self._html("<h1>服务器内部错误</h1>", 500)

    def do_POST(self) -> None:
        try:
            self._route_post()
        except Exception:
            traceback.print_exc()
            self._html("<h1>服务器内部错误</h1>", 500)

    def _route_get(self) -> None:
        path = urllib.parse.urlsplit(self.path).path
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)

        if path == "/health":
            self._send(200, b"ok", "text/plain; charset=utf-8")
            return

        if path.startswith("/static/"):
            self._static(path)
            return

        if path == "/":
            user = self._user()
            if not user:
                self._redirect("/login")
                return
            self._render(
                "dashboard.html.j2",
                stores=STORE.list_stores(user["id"]),
                stats=STORE.dashboard(user["id"]),
                events=STORE.list_events(user["id"], limit=12),
                host=config.HOST,
                port=config.PORT,
            )
            return

        if path == "/login":
            self._render("auth.html.j2", mode="login", error="", email="")
            return

        if path == "/register":
            self._render("auth.html.j2", mode="register", error="", email="")
            return

        if path == "/logout":
            token = self._cookie(config.COOKIE_NAME)
            if token:
                STORE.delete_session(token)
            self._redirect("/login", cookie=f"{config.COOKIE_NAME}=; Path=/; Max-Age=0")
            return

        if path == "/pricing":
            self._render("pricing.html.j2")
            return

        if path == "/stores/new":
            user = self._user()
            if not user:
                self._redirect("/login")
                return
            self._render("new_store.html.j2", error="", url="",
                         used=STORE.count_stores(user["id"]),
                         limit=config.PLANS.get(user["plan"], config.PLANS["free"])["store_limit"])
            return

        if path == "/events":
            user = self._user()
            if not user:
                self._redirect("/login")
                return
            store_id = int((query.get("store_id") or ["0"])[0] or 0)
            etype = (query.get("type") or [""])[0]
            self._render(
                "events.html.j2",
                events=STORE.list_events(user["id"], store_id=store_id or None, etype=etype, limit=300),
                stores=STORE.list_stores(user["id"]),
                cur_store=store_id,
                cur_type=etype,
            )
            return

        m = re.match(r"^/stores/(\d+)$", path)
        if m:
            user = self._user()
            if not user:
                self._redirect("/login")
                return
            store_id = int(m.group(1))
            store = STORE.get_store(store_id, user["id"])
            if not store:
                self._html("<h1>店铺不存在</h1>", 404)
                return

            raw_page = (query.get("page") or ["1"])[0]
            page_no = int(raw_page) if raw_page.isdigit() else 1
            per_page = 100
            total = STORE.count_products(store_id)
            pages = max(1, (total + per_page - 1) // per_page)
            page_no = min(max(1, page_no), pages)

            self._render(
                "store.html.j2",
                store=store,
                products=STORE.list_products(
                    store_id, limit=per_page, offset=(page_no - 1) * per_page
                ),
                total=total,
                page_no=page_no,
                pages=pages,
                events=STORE.list_events(user["id"], store_id=store_id, limit=30),
            )
            return

        if path == "/api/events":
            user = self._user()
            if not user:
                self._json({"error": "未登录"}, 401)
                return
            rows = STORE.list_events(user["id"], limit=100)
            self._json([dict(r) for r in rows])
            return

        self._html("<h1>页面不存在</h1><p><a href='/'>返回首页</a></p>", 404)

    def _route_post(self) -> None:
        path = urllib.parse.urlsplit(self.path).path
        form = self._form()

        if path == "/login":
            email = (form.get("email") or "").strip()
            password = form.get("password") or ""
            from storage import verify_password
            user = STORE.get_user_by_email(email)
            if not user or not verify_password(password, user["password_hash"]):
                self._render("auth.html.j2", code=400, mode="login",
                             error="邮箱或密码不正确", email=email)
                return
            token = STORE.create_session(user["id"])
            self._redirect("/", cookie=(
                f"{config.COOKIE_NAME}={urllib.parse.quote(token)}; Path=/; "
                f"Max-Age={config.SESSION_DAYS * 86400}; HttpOnly; SameSite=Lax"
            ))
            return

        if path == "/register":
            email = (form.get("email") or "").strip().lower()
            password = form.get("password") or ""
            if "@" not in email or "." not in email.split("@")[-1]:
                self._render("auth.html.j2", code=400, mode="register",
                             error="请输入有效邮箱", email=email)
                return
            if len(password) < 8:
                self._render("auth.html.j2", code=400, mode="register",
                             error="密码至少 8 位", email=email)
                return
            if STORE.get_user_by_email(email):
                self._render("auth.html.j2", code=400, mode="register",
                             error="该邮箱已注册", email=email)
                return
            user_id = STORE.create_user(email, password)
            token = STORE.create_session(user_id)
            self._redirect("/", cookie=(
                f"{config.COOKIE_NAME}={urllib.parse.quote(token)}; Path=/; "
                f"Max-Age={config.SESSION_DAYS * 86400}; HttpOnly; SameSite=Lax"
            ))
            return

        if path == "/stores":
            self._create_store(form)
            return

        m = re.match(r"^/stores/(\d+)/delete$", path)
        if m:
            user = self._user()
            if user:
                STORE.delete_store(int(m.group(1)), user["id"])
            self._redirect("/")
            return

        m = re.match(r"^/stores/(\d+)/collect$", path)
        if m:
            user = self._user()
            if not user:
                self._redirect("/login")
                return
            store = STORE.get_store(int(m.group(1)), user["id"])
            if store:
                stats, events = collect_store(STORE, store)
                if events:
                    from notify import send_digest
                    send_digest(STORE, user["email"], store["label"], events)
            self._redirect(f"/stores/{m.group(1)}")
            return

        self._html("<h1>页面不存在</h1>", 404)

    def _create_store(self, form: dict) -> None:
        user = self._user()
        if not user:
            self._redirect("/login")
            return

        plan = config.PLANS.get(user["plan"], config.PLANS["free"])
        used = STORE.count_stores(user["id"])
        if used >= plan["store_limit"]:
            self._render("new_store.html.j2", code=400,
                         error=f"当前套餐最多监控 {plan['store_limit']} 个店铺，请升级",
                         url="", used=used, limit=plan["store_limit"])
            return

        raw_url = (form.get("url") or "").strip()
        label = (form.get("label") or "").strip()
        try:
            base = normalize_base(raw_url)
        except AdapterError as exc:
            self._render("new_store.html.j2", code=400, error=str(exc),
                         url=raw_url, used=used, limit=plan["store_limit"])
            return

        adapter, reasons = detect_adapter(base, ADAPTERS)
        if adapter is None:
            detail = "；".join(reasons) if reasons else "未识别出支持的建站平台"
            self._render("new_store.html.j2", code=400,
                         error=f"无法识别该店铺的技术平台。{detail}",
                         url=raw_url, used=used, limit=plan["store_limit"])
            return

        platform = adapter.platform
        platform_label = adapter.platform_label
        adapter.close()

        if not label:
            label = base.split("://")[-1]

        try:
            store_id = STORE.add_store(user["id"], base, platform, label)
        except Exception:
            self._render("new_store.html.j2", code=400,
                         error="这个店铺已经添加过了", url=raw_url,
                         used=used, limit=plan["store_limit"])
            return

        store = STORE.get_store(store_id, user["id"])
        collect_store(STORE, store)
        self._redirect(f"/stores/{store_id}")

    def _static(self, path: str) -> None:
        rel = path[len("/static/"):]
        if ".." in rel or rel.startswith("/"):
            self._send(403, b"forbidden", "text/plain")
            return
        target = BASE / "static" / rel
        if not target.exists():
            self._send(404, b"not found", "text/plain")
            return
        ctype = "text/css" if target.suffix == ".css" else "application/octet-stream"
        self._send(200, target.read_bytes(), ctype)

    def log_message(self, fmt: str, *args) -> None:
        print("[web] " + (fmt % args))


def create_server(host: str, port: int, storage: Storage):
    global STORE
    STORE = storage
    return ThreadingHTTPServer((host, port), Handler)


def serve(host: str = None, port: int = None, with_scheduler: bool = True) -> None:
    host = host or config.HOST
    port = port or config.PORT
    storage = Storage()

    server = create_server(host, port, storage)
    sched = None
    if with_scheduler:
        from scheduler import Scheduler
        sched = Scheduler(storage)
        sched.start()

    print(f"PriceWatch 已启动： http://{host}:{port}")
    print("按 Ctrl+C 停止")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n正在停止…")
    finally:
        if sched:
            sched.stop()
        server.server_close()
