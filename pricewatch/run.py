"""PriceWatch 入口。

用法：
    python run.py serve
    python run.py check competitor.com
    python run.py collect
    python run.py user add a@b.com 你的密码
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
