"""命令行入口。

用法：
    python run.py serve                       启动网页版与后台采集
    python run.py check competitor.com        检测站点是否支持
    python run.py collect                     手动跑一轮采集
    python run.py user add a@b.com 密码        创建账号
"""

import argparse
import sys

import config
from adapters import ADAPTERS, detect_adapter, normalize_base
from collector import collect_all
from storage import Storage


def cmd_serve(args) -> int:
    from web import serve
    serve(host=args.host, port=args.port, with_scheduler=not args.no_scheduler)
    return 0


def cmd_check(args) -> int:
    try:
        base = normalize_base(args.url)
    except Exception as exc:
        print(f"地址无效：{exc}", file=sys.stderr)
        return 2

    adapter, reasons = detect_adapter(base, ADAPTERS)
    if adapter is None:
        print(f"不支持：{base}")
        for r in reasons:
            print(f"  - {r}")
        return 1

    print(f"支持：{base}  →  {adapter.platform_label}（{adapter.platform}）")
    products = list(adapter.fetch_products())
    adapter.close()
    print(f"读取到 {len(products)} 个商品，前 5 个：")
    for p in products[:5]:
        stock = "有货" if p.in_stock else "缺货"
        print(f"  - {p.name}  {p.price_display}  [{stock}]")
    return 0


def cmd_collect(args) -> int:
    storage = Storage()
    results = collect_all(storage)
    if not results:
        print("没有需要采集的店铺")
        return 0
    total_events = 0
    for s in results:
        if s["error"]:
            print(f"[失败] {s['store_label']}：{s['error']}")
        else:
            changed = (s["new"] + s["price_down"] + s["price_up"]
                       + s["out_of_stock"] + s["back_in_stock"] + s["removed"])
            total_events += changed
            print(f"[完成] {s['store_label']}：{s['total']} 件商品，{changed} 条变化")
    print(f"共 {len(results)} 个店铺，{total_events} 条变化")
    return 0


def cmd_user(args) -> int:
    storage = Storage()
    if args.action == "add":
        if storage.get_user_by_email(args.email):
            print("该邮箱已存在", file=sys.stderr)
            return 1
        uid = storage.create_user(args.email, args.password, plan=args.plan)
        print(f"已创建用户 id={uid} 邮箱={args.email} 套餐={args.plan}")
        return 0
    if args.action == "list":
        from sqlite3 import Row
        print("请用网页版查看用户列表")
        return 0
    return 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="pricewatch", description="竞品价格与库存监控")
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("serve", help="启动网页版")
    s.add_argument("--host", default=config.HOST)
    s.add_argument("--port", type=int, default=config.PORT)
    s.add_argument("--no-scheduler", action="store_true", help="不启动后台采集")
    s.set_defaults(func=cmd_serve)

    c = sub.add_parser("check", help="检测一个站点是否支持")
    c.add_argument("url")
    c.set_defaults(func=cmd_check)

    a = sub.add_parser("collect", help="手动采集一轮")
    a.set_defaults(func=cmd_collect)

    u = sub.add_parser("user", help="用户管理")
    u.add_argument("action", choices=["add", "list"])
    u.add_argument("email", nargs="?", default="")
    u.add_argument("password", nargs="?", default="")
    u.add_argument("--plan", default="free", choices=list(config.PLANS.keys()))
    u.set_defaults(func=cmd_user)

    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
