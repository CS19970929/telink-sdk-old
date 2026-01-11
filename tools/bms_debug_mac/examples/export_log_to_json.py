"""示例：将 output/notify_log.csv 导出为 JSON。"""
import csv
import json
import os
from pathlib import Path


def main() -> None:
    root = Path(os.path.dirname(os.path.dirname(__file__)))
    csv_path = root / "output" / "notify_log.csv"
    if not csv_path.exists():
        print("未找到 notify_log.csv，请先运行主程序并开启 log on")
        return

    rows = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    json_path = csv_path.with_suffix(".json")
    with json_path.open("w") as f:
        json.dump(rows, f, ensure_ascii=False, indent=2)
    print(f"已导出 {len(rows)} 条记录 -> {json_path}")


if __name__ == "__main__":
    main()
