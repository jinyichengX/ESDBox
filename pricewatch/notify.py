"""告警发送。

未配置邮件服务器时降级为控制台输出，仍然写告警记录，
这样本地就能完整验证"采集 → 变化 → 通知"这条链路。
"""

import smtplib
from email.message import EmailMessage
from typing import List

import config
from models import EVENT_LABELS


def build_digest(store_label: str, events: List[dict]) -> str:
    """把一次采集的多条变化合并成一封信，避免刷屏。"""
    lines = [f"「{store_label}」本次有 {len(events)} 条变化：", ""]
    for e in events:
        label = EVENT_LABELS.get(e["type"], getattr(e["type"], "value", str(e["type"])))
        if e["old"] and e["new"]:
            lines.append(f"- [{label}] {e['product_name']}：{e['old']} → {e['new']}")
        elif e["new"]:
            lines.append(f"- [{label}] {e['product_name']}：{e['new']}")
        else:
            lines.append(f"- [{label}] {e['product_name']}")
        if e.get("product_url"):
            lines.append(f"  {e['product_url']}")
    return "\n".join(lines)


def send_digest(storage, to_email: str, store_label: str, events: List[dict]) -> str:
    """发送汇总告警，返回 sent / logged / failed。"""
    if not events:
        return "skipped"

    subject = f"[PriceWatch] {store_label} 有 {len(events)} 条变化"
    body = build_digest(store_label, events)

    if not config.ALERT_EMAIL_ENABLED:
        print(f"\n[告警-控制台] {subject}\n{body}\n")
        for e in events:
            storage.add_alert(e["event_id"], "console", to_email or "local", "logged")
        return "logged"

    msg = EmailMessage()
    msg["Subject"] = subject
    msg["From"] = config.SMTP_FROM
    msg["To"] = to_email
    msg.set_content(body)

    try:
        with smtplib.SMTP(config.SMTP_HOST, config.SMTP_PORT, timeout=20) as server:
            if config.SMTP_TLS:
                server.starttls()
            if config.SMTP_USER:
                server.login(config.SMTP_USER, config.SMTP_PASSWORD)
            server.send_message(msg)
        for e in events:
            storage.add_alert(e["event_id"], "email", to_email, "sent")
        return "sent"
    except Exception as exc:
        for e in events:
            storage.add_alert(e["event_id"], "email", to_email, "failed", str(exc))
        print(f"[告警] 邮件发送失败：{exc}")
        return "failed"
