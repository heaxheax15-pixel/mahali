#!/usr/bin/env python3
"""Seed a demo mahali database so UI screenshots show real content."""

import sqlite3
import sys
from datetime import datetime

DB = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mahali-demo/db.sqlite"
THEME = sys.argv[2] if len(sys.argv) > 2 else None

def ts(h: int, m: int = 0) -> str:
    now = datetime.now()
    return (now.replace(hour=h, minute=m, second=0, microsecond=0)
               .strftime("%Y-%m-%dT%H:%M:%S") + ".000")

con = sqlite3.connect(DB)
cur = con.cursor()

cur.executemany(
    "INSERT INTO products (barcode, name, cost_price_cents, sale_price_cents, quantity, unit, package_size, active) VALUES (?,?,?,?,?,?,?,1)",
    [
        ("6130001", "خبز", 700, 1000, 120, "قطعة", 1),
        ("6130002", "حليب", 9500, 11000, 40, "لتر", 1),
        ("6130003", "سكر", 11800, 13000, 60, "كلغ", 1),
        ("6130004", "زيت طعام", 72000, 80000, 25, "قارورة 5ل", 1),
        ("6130005", "شاي", 32000, 35000, 30, "عبوة", 1),
        ("6130006", "جبن", 40000, 45000, 12, "عبوة", 1),
    ],
)

cur.executemany(
    "INSERT INTO customers (name, phone) VALUES (?,?)",
    [
        ("أحمد بن عيسى", "0551122334"),
        ("فاطمة مرابط", "0664987654"),
        ("يوسف حمداني", "0777112233"),
    ],
)

cur.executemany(
    "INSERT INTO suppliers (name) VALUES (?)",
    [("مؤسسة الإمداد الغذائي",), ("تعاونية الخضار",)],
)

cur.execute(
    "INSERT INTO supplier_transactions (supplier_id, amount_cents, created_at, note) VALUES (?,?,?,?)",
    (1, 150000, ts(7, 30), "توريد دفعة بضاعة"),
)
cur.execute(
    "INSERT INTO supplier_transactions (supplier_id, amount_cents, created_at, note) VALUES (?,?,?,?)",
    (2, 60000, ts(8, 15), "توريد خضار"),)

cur.execute(
    "INSERT INTO cash_sessions (opened_at, opening_float_cents, status) VALUES (?,?,?)",
    (ts(8, 0), 100000, "open"),
)

# Two sales today + their line items + cash movements.
cur.execute(
    "INSERT INTO sales (created_at, total_cents, device_id) VALUES (?,?,?)",
    (ts(9, 5), 27000, "desktop"),
)
sale1 = cur.lastrowid
cur.executemany(
    "INSERT INTO sale_items (sale_id, product_id, quantity, unit_price_cents, unit_cost_cents) VALUES (?,?,?,?,?)",
    [
        (sale1, 1, 5, 1000, 700),
        (sale1, 2, 2, 11000, 9500),
    ],
)

cur.execute(
    "INSERT INTO sales (created_at, total_cents, device_id) VALUES (?,?,?)",
    (ts(10, 20), 35000, "desktop"),
)
sale2 = cur.lastrowid
cur.execute(
    "INSERT INTO sale_items (sale_id, product_id, quantity, unit_price_cents, unit_cost_cents) VALUES (?,?,?,?,?)",
    (sale2, 5, 1, 35000, 32000),
)

cur.executemany(
    "INSERT INTO cash_movements (session_id, type, amount_cents, created_at, note) VALUES (1,?,?,?,?)",
    [
        ("sale", 27000, ts(9, 5), "البيع #1"),
        ("sale", 35000, ts(10, 20), "البيع #2"),
    ],
)

cur.execute(
    "INSERT INTO payments (customer_id, amount_cents, created_at, note) VALUES (?,?,?,?)",
    (1, 15000, ts(10, 45), "دفعة على الحساب"),
)

cur.execute(
    "INSERT INTO expenses (created_at, label, amount_cents) VALUES (?,?,?)",
    (ts(12, 0), "إيجار محل", 5000),
)

cur.execute(
    "INSERT INTO owner_drawings (created_at, amount_cents, note) VALUES (?,?,?)",
    (ts(12, 30), 8000, "سحب نقدي للمستخدم"),
)

cur.executemany(
    "INSERT INTO audit_log (actor, action, target, created_at) VALUES (?,?,?,?)",
    [
        ("desktop", "price_override", "شاي", ts(10, 21)),
        ("desktop", "customer_payment", "أحمد بن عيسى", ts(10, 45)),
    ],
)

cur.executemany(
    "INSERT INTO zakat_settings (key, value) VALUES (?,?)",
    [("enabled", "1")],
)

settings_rows = [
    ("shop_name", "محل النور"),
    ("currency_symbol", "دج"),
]
if THEME:
    settings_rows.append(("theme", THEME))
cur.executemany(
    "INSERT INTO settings (key, value) VALUES (?,?)",
    settings_rows,
)

con.commit()
con.close()
print("seeded", DB)