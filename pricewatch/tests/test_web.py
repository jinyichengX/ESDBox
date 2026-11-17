"""Web 端到端测试：启动真实 HTTP 服务，走完整用户路径。"""

import http.client
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

import web
from storage import Storage


class WebTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.storage = Storage(str(Path(self.tmp.name) / "web.db"))
        self.server = web.create_server("127.0.0.1", 0, self.storage)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.tmp.cleanup()

    def _req(self, method, path, body=None, cookie=""):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        headers = {}
        if body:
            headers["Content-Type"] = "application/x-www-form-urlencoded"
            # http.client 默认按 latin-1 编码，中文表单必须显式转 utf-8
            body = body.encode("utf-8") if isinstance(body, str) else body
        if cookie:
            headers["Cookie"] = cookie
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        data = resp.read().decode("utf-8", errors="replace")
        set_cookie = resp.getheader("Set-Cookie") or ""
        conn.close()
        return resp.status, data, set_cookie

    def _register(self, email="a@b.com"):
        status, _, cookie = self._req(
            "POST", "/register", body=f"email={email}&password=password123"
        )
        token = cookie.split("pw_session=")[1].split(";")[0] if "pw_session=" in cookie else ""
        return status, token

    def test_health(self):
        status, body, _ = self._req("GET", "/health")
        self.assertEqual(status, 200)
        self.assertEqual(body, "ok")

    def test_root_redirects_when_anonymous(self):
        status, _, _ = self._req("GET", "/")
        self.assertEqual(status, 302)

    def test_pricing_is_public(self):
        status, body, _ = self._req("GET", "/pricing")
        self.assertEqual(status, 200)
        self.assertIn("套餐", body)
        self.assertIn("19", body)

    def test_register_then_dashboard(self):
        status, token = self._register()
        self.assertEqual(status, 302)
        self.assertTrue(token)
        status, body, _ = self._req("GET", "/", cookie=f"pw_session={token}")
        self.assertEqual(status, 200)
        self.assertIn("概览", body)

    def test_login_wrong_password(self):
        self._register()
        status, body, _ = self._req(
            "POST", "/login", body="email=a@b.com&password=wrongpass"
        )
        self.assertEqual(status, 400)
        self.assertIn("不正确", body)

    def test_duplicate_email_rejected(self):
        self._register()
        status, body, _ = self._req(
            "POST", "/register", body="email=a@b.com&password=password123"
        )
        self.assertEqual(status, 400)
        self.assertIn("已注册", body)

    def test_short_password_rejected(self):
        status, body, _ = self._req("POST", "/register", body="email=x@y.com&password=short")
        self.assertEqual(status, 400)
        self.assertIn("8 位", body)

    def test_add_store_with_fake_adapter(self):
        _, token = self._register("c@d.com")

        class FakeAdapter:
            platform = "woocommerce"
            platform_label = "WooCommerce"

            def fetch_products(self):
                return iter([])

            def close(self):
                pass

        with patch.object(web, "detect_adapter", return_value=(FakeAdapter(), [])):
            with patch.object(web, "collect_store", return_value=({"error": ""}, [])):
                status, _, _ = self._req(
                    "POST", "/stores",
                    body="url=shop.example.com&label=测试店",
                    cookie=f"pw_session={token}",
                )

        self.assertEqual(status, 302)
        stores = self.storage.list_stores(1)
        self.assertEqual(len(stores), 1)
        self.assertEqual(stores[0]["platform"], "woocommerce")
        self.assertEqual(stores[0]["label"], "测试店")

    def test_add_store_rejects_unknown_platform(self):
        _, token = self._register("e@f.com")
        with patch.object(web, "detect_adapter", return_value=(None, ["WooCommerce：HTTP 403"])):
            status, body, _ = self._req(
                "POST", "/stores", body="url=unknown.example.com",
                cookie=f"pw_session={token}",
            )
        self.assertEqual(status, 400)
        self.assertIn("无法识别", body)

    def test_store_detail_page(self):
        _, token = self._register("g@h.com")
        uid = self.storage.get_user_by_email("g@h.com")["id"]
        sid = self.storage.add_store(uid, "https://s.example.com", "woocommerce", "店铺A")
        status, body, _ = self._req("GET", f"/stores/{sid}", cookie=f"pw_session={token}")
        self.assertEqual(status, 200)
        self.assertIn("店铺A", body)

    def test_other_users_store_is_hidden(self):
        self._register("owner@x.com")
        owner_id = self.storage.get_user_by_email("owner@x.com")["id"]
        sid = self.storage.add_store(owner_id, "https://s.example.com", "woocommerce", "私密店")

        _, token = self._register("other@x.com")
        status, _, _ = self._req("GET", f"/stores/{sid}", cookie=f"pw_session={token}")
        self.assertEqual(status, 404)

    def test_events_api_requires_login(self):
        status, _, _ = self._req("GET", "/api/events")
        self.assertEqual(status, 401)


if __name__ == "__main__":
    unittest.main()
