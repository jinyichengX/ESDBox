# 部署说明

## 服务器要求

| 项目 | 最低 | 建议 |
|---|---|---|
| CPU | 1 核 | 2 核 |
| 内存 | 1 GB | 2 GB |
| 磁盘 | 10 GB | 20 GB |
| 系统 | 任意 Linux / Windows | Ubuntu 22.04 |
| Python | 3.9+ | 3.11 |

按免费 1 店 + 付费 5 店估算，单店铺每次采集约 1~2 秒，1 核足够撑到几百个店铺。
真正的瓶颈不是 CPU，而是**目标站点的响应速度**和**出口 IP 是否被限流**。

## 依赖

```bash
pip install requests jinja2
```

只需要这两个第三方库，其余全部用 Python 标准库（`sqlite3`、`http.server`、`smtplib`、`hashlib`）。

## Linux 部署

```bash
# 1. 拉代码
mkdir -p /opt && cd /opt
# 把 pricewatch 目录放进来

# 2. 建虚拟环境并装依赖
cd /opt/pricewatch
python3 -m venv venv
./venv/bin/pip install requests jinja2

# 3. 先手动跑一次，确认能采集
./venv/bin/python run.py check woocommerce.com

# 4. 创建系统服务
sudo tee /etc/systemd/system/pricewatch.service > /dev/null <<'EOF'
[Unit]
Description=PriceWatch competitor price monitor
After=network-online.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/pricewatch
Environment=PRICEWATCH_HOST=127.0.0.1
Environment=PRICEWATCH_PORT=8000
Environment=PRICEWATCH_DB=/var/lib/pricewatch/pricewatch.db
Environment=PRICEWATCH_SMTP_HOST=smtp.example.com
Environment=PRICEWATCH_SMTP_USER=alerts@yourdomain.com
Environment=PRICEWATCH_SMTP_PASSWORD=你的授权码
Environment=PRICEWATCH_SMTP_FROM=alerts@yourdomain.com
ExecStart=/opt/pricewatch/venv/bin/python run.py serve
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# 5. 数据目录与权限
sudo mkdir -p /var/lib/pricewatch
sudo chown -R www-data:www-data /var/lib/pricewatch /opt/pricewatch

# 6. 启动
sudo systemctl daemon-reload
sudo systemctl enable --now pricewatch
sudo systemctl status pricewatch
```

插件（systemd）就是这个系统的“开机自启 + 崩溃重启”机制，`Restart=always` 保证进程挂了
五秒内自动拉起。

## 反向代理与 HTTPS

服务本身只监听 `127.0.0.1`，对外由 nginx 转发并终止 TLS（传输层加密）：

```nginx
server {
    listen 80;
    server_name pricewatch.yourdomain.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl http2;
    server_name pricewatch.yourdomain.com;

    ssl_certificate     /etc/letsencrypt/live/pricewatch.yourdomain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/pricewatch.yourdomain.com/privkey.pem;

    location / {
        proxy_pass         http://127.0.0.1:8000;
        proxy_set_header   Host $host;
        proxy_set_header   X-Real-IP $remote_addr;
        proxy_set_header   X-Forwarded-Proto $scheme;
        proxy_read_timeout 120s;   # 首次采集商品多时较慢
    }
}
```

证书用 Let's Encrypt 的 certbot 免费签发：

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d pricewatch.yourdomain.com
```

## Windows 部署

生产环境不建议用 Windows，但如果只是内部试用：

```powershell
$env:PRICEWATCH_PORT = "8000"
$env:PRICEWATCH_DB = "D:\pricewatch\data\pricewatch.db"
python run.py serve
```

要做成开机自启，用「任务计划程序」创建一条「启动时运行」的任务，操作填：

```
程序： python
参数： run.py serve
起始于： D:\pricewatch
```

## 备份

数据全在一个 SQLite 文件里，备份就是复制文件。**一定要用 SQLite 自带的备份命令**，
直接复制运行中的数据库可能拿到不一致的状态：

```bash
sqlite3 /var/lib/pricewatch/pricewatch.db ".backup '/backup/pricewatch-$(date +%F).db'"
```

放进 crontab（定时任务）每天凌晨跑一次：

```
0 3 * * * sqlite3 /var/lib/pricewatch/pricewatch.db ".backup '/backup/pricewatch-$(date +\%F).db'"
```

保留最近 14 天：

```
5 3 * * * find /backup -name 'pricewatch-*.db' -mtime +14 -delete
```

## 监控

最简单的健康检查：

```bash
curl -f http://127.0.0.1:8000/health
```

返回 `ok` 即正常。接到任意监控服务（UptimeRobot、Better Stack）上，一分钟一次就够。

采集失败不会让服务挂掉，错误会写在店铺记录的 `last_error` 字段里，界面首页能看到。
**建议每周看一眼首页有没有店铺长时间失败**，那通常意味着对方改接口了。

## 升级

```bash
cd /opt/pricewatch
sudo systemctl stop pricewatch
# 覆盖代码，保留 data/ 目录
sudo systemctl start pricewatch
```

数据库结构变更需要手动迁移，当前版本没有自动迁移机制。升级前先备份。

## 常见问题

**Q：添加店铺提示“无法识别平台”**
对方不是 WooCommerce，或 Store API 被插件禁用。用 `python run.py check 域名` 能看到具体原因。

**Q：采集一直是 429**
对方限流。调大 `config.py` 里的 `REQUEST_DELAY`，或把 `interval_min` 拉长。

**Q：邮件收不到**
`PRICEWATCH_SMTP_HOST` 为空时告警只打到控制台。配置后先用 `python run.py collect` 手动触发一次看日志。

**Q：多个实例能同时跑吗**
不能。调度器没有分布式锁，两个进程会重复采集。要多实例必须先加任务队列。
