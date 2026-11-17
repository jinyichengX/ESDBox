# PriceWatch · 竞品价格与库存监控

给 WooCommerce 独立站店主的竞品监控工具。输入竞品域名，自动抓取对方的商品、价格和库存，
一旦有变化就告警。核心用途只有一个：**别等客户告诉你，你已经知道对手降价了。**

## 为什么只做 WooCommerce

竞品监控类工具（Prisync、Price2Spy）大多靠爬页面实现，代价是脆弱：对方改一次页面结构就失效，
还得配代理池绕防护，月费因此推到 $99 以上。

WooCommerce 站点开放了官方公开接口，不需要登录、不需要绕过任何防护：

```
GET https://店铺域名/wp-json/wc/store/products?per_page=100&page=1
```

返回商品名、价格、库存状态、链接，字段稳定、有官方文档。只做一个平台换来的是
**稳定性**和**成本**，这两样正好是能转换成价格优势的东西。

## 功能

- 添加竞品店铺，自动识别平台并做首次基线采集
- 定时采集，只记录真实变化（降价、涨价、缺货、补货、新品、下架）
- 变化按严重度汇总成一封邮件，不刷屏
- 事件流可按店铺、类型筛选
- 分页浏览商品，显示当前价与库存状态
- 多用户，每人只能看到自己的店铺
- 套餐控制店铺数量（免费 1 个 / 标准 5 个 / 专业 20 个）

## 快速开始

只需要 Python 3.9 以上，没有第三方依赖要装（`requests`、`jinja2` 是仅有的两个前置库，
它们通常随环境提供；如果你的环境没有，见下方“依赖说明”）。

```bash
cd pricewatch

# 1. 检测一个站点是否支持
python run.py check woocommerce.com

# 2. 启动网页版（含后台自动采集）
python run.py serve

# 3. 打开 http://127.0.0.1:8000 ，注册账号，添加竞品店铺
```

手动跑一轮采集（不启动网页）：

```bash
python run.py collect
```

创建账号（不经过网页）：

```bash
python run.py user add you@example.com 你的密码 --plan starter
```

## 架构

```
adapters/            平台适配器（唯一与外部数据源耦合的地方）
  base.py            统一请求、限速、重试、平台探测
  woocommerce.py     官方 Store API
  shopify.py         products.json（备用，部分站点会限制）
collector.py         采集编排 + 变化检测
storage.py           SQLite 仓储层
notify.py            邮件告警（未配置时降级为控制台输出）
scheduler.py         后台按店铺间隔轮询
web.py               标准库 HTTP 服务 + 路由
templates/           页面模板
tests/               53+ 个测试，全部离线可跑
```

数据流：

```
定时触发 → 适配器拉商品 → 与上次快照比对 → 有变化才写事件
        → 汇总成邮件 → 写告警记录 → 页面展示
```

设计上的几个取舍：

- **首次采集只建基线，不告警**。否则刚添加店铺就会收到几百条“新品上架”。
- **只在变化时写快照**。不变就不落库，数据库增长与真实变化频率成正比。
- **每次请求独立开关 SQLite 连接**。写入频率很低，换线程安全更划算。
- **告警按批次汇总**。一个店铺一次采集的内部变化合成一封邮件。

## 金额处理

WooCommerce 返回的价格是「最小货币单位」字符串，例如 `"5900"` 配 `currency_minor_unit=2`
表示 59.00。全部金额在内部用整数（分）保存，只在展示时格式化，避免浮点误差和 100 倍错误。

## 配置

全部通过环境变量，不需要改代码：

| 变量 | 默认 | 说明 |
|---|---|---|
| `PRICEWATCH_DB` | `data/pricewatch.db` | 数据库路径 |
| `PRICEWATCH_HOST` | `127.0.0.1` | 监听地址 |
| `PRICEWATCH_PORT` | `8000` | 监听端口 |
| `PRICEWATCH_SMTP_HOST` | 空 | 邮件服务器，留空则告警只打印到控制台 |
| `PRICEWATCH_SMTP_PORT` | `587` | 邮件端口 |
| `PRICEWATCH_SMTP_USER` | 空 | 邮箱账号 |
| `PRICEWATCH_SMTP_PASSWORD` | 空 | 邮箱密码或授权码 |
| `PRICEWATCH_SMTP_FROM` | `alerts@pricewatch.local` | 发件人 |

采集间隔、分页上限、请求延时等在 `config.py` 里，都有注释。

## 测试

```bash
python -m unittest discover -s tests -v
```

测试全部离线：适配器用固定 JSON 样本，采集用假适配器，Web 起真实 HTTP 服务。
覆盖金额转换、解析边界、变化检测、幂等性、多用户隔离、分页、调度到期判断。

## 已知限制

1. **只支持 WooCommerce 与部分 Shopify 站点**。其他建站平台需要新增适配器。
2. **Shopify 不返回币种**，只能按店铺默认货币显示，多币种店铺可能不准。
3. **价格区间商品**（可变商品）取最低档作为代表价格，不做逐规格监控。
4. **没有支付集成**。套餐字段已就绪，接 Stripe 需要填入密钥后自行接通。
5. **单进程调度**。店铺数量到几百个后需要拆成独立 worker 加任务队列。

## 合规

只调用店铺主动公开的商品接口，不登录、不提交表单、不抓取后台数据。
请求带明确标识的用户代理，同域名请求间隔不低于 0.5 秒，单店铺商品数有上限。

## 目录

```
pricewatch/
├── run.py            入口
├── config.py         配置
├── models.py         数据模型与金额处理
├── adapters/         平台适配器
├── collector.py      采集与变化检测
├── storage.py        SQLite 仓储
├── notify.py         告警
├── scheduler.py      后台调度
├── web.py            Web 服务
├── cli.py            命令行
├── templates/        页面模板
├── static/           样式
├── tests/            测试与样本
└── data/             运行时数据库
```
