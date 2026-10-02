Financial Model — Design Specification
Version 1.0 — September 28, 2026

1. General Principles
All monetary amounts are integer cents. No floats.

All entities have id INTEGER PRIMARY KEY AUTOINCREMENT.

All records have created_at TEXT in ISO UTC format.

Deletion is rare: prefer active = 0 over row deletion.

Every write operation runs inside a transaction — no exceptions.

2. Suppliers (suppliers)
Field	Type	Description
id	INTEGER	PK
name	TEXT NOT NULL	Name
phone	TEXT	Phone
address	TEXT	Address
notes	TEXT	Notes
opening_balance_cents	INTEGER DEFAULT 0	Opening balance (legacy debt)
active	INTEGER DEFAULT 1	Enabled
created_at	TEXT	—
Current balance = opening_balance_cents + sum(purchases) − sum(payments) − sum(returns).

Note: The balance is derived entirely from purchases and supplier_payments, so a
supplier row never needs editing to record an invoice or a payment. Returns are not
deducted yet.

3. Purchase Invoices (purchases)
Field	Type	Description
id	INTEGER	PK
supplier_id	INTEGER NOT NULL	FK → suppliers
invoice_number	TEXT	Supplier's invoice number (optional)
purchased_at	TEXT NOT NULL	Purchase date (defaults to now, editable)
subtotal_cents	INTEGER NOT NULL	Sum of items before VAT
vat_cents	INTEGER DEFAULT 0	VAT (optional, 0 by default)
total_cents	INTEGER NOT NULL	subtotal + vat
paid_cents	INTEGER DEFAULT 0	Amount paid at invoice time
add_to_stock	INTEGER DEFAULT 1	Add to stock?
note	TEXT	Notes
created_at	TEXT	—
Invoice remaining balance = total_cents − paid_cents − sum(later payments) − sum(returns).

4. Purchase Items (purchase_items)
Field	Type	Description
id	INTEGER	PK
purchase_id	INTEGER NOT NULL	FK → purchases
product_id	INTEGER	FK → products (may be NULL for a new product)
description	TEXT NOT NULL	Free description (for a new product)
quantity	INTEGER NOT NULL	Quantity (in base unit)
unit	TEXT DEFAULT 'piece'	piece, kg, liter
unit_price_cents	INTEGER NOT NULL	Unit price
total_cents	INTEGER NOT NULL	quantity × unit_price
On save:

If product_id exists → update average cost (PMP).

If add_to_stock = 1 → add quantity to product stock.

If product_id = NULL → auto-create a new product (no barcode).

5. Supplier Payments (supplier_payments)
Field	Type	Description
id	INTEGER	PK
supplier_id	INTEGER NOT NULL	FK
purchase_id	INTEGER	FK → purchases (NULL = general payment)
amount_cents	INTEGER NOT NULL	Amount
paid_at	TEXT NOT NULL	Payment date (defaults to now)
note	TEXT	Notes
created_at	TEXT	—
Rule: If purchase_id is set, deduct from that invoice's balance. If NULL, deduct from oldest first (FIFO).

6. Supplier Returns (supplier_returns)
Field	Type	Description
id	INTEGER	PK
supplier_id	INTEGER NOT NULL	FK
purchase_id	INTEGER	FK (optional)
amount_cents	INTEGER NOT NULL	Return value
returned_at	TEXT NOT NULL	Return date
remove_from_stock	INTEGER DEFAULT 1	Remove from stock?
note	TEXT	—
created_at	TEXT	—
Return items (supplier_return_items) — if returns are itemized, added in a later phase. Total amount suffices now.

7. Products (products) — Modifications
Two fields added:

Field	Type	Description
cost_cents	INTEGER DEFAULT 0	Current average cost (PMP)
unit	TEXT DEFAULT 'piece'	piece, kg, liter
PMP is updated on each purchase invoice:

text
newCost = ((oldQty × oldCost) + (newQty × newCost)) / (oldQty + newQty)
Note: sold_by_weight already exists. It remains.

8. Customers (customers) — Modifications
Two fields added:

Field	Type	Description
opening_balance_cents	INTEGER DEFAULT 0	Opening balance
active	INTEGER DEFAULT 1	—
The existing structure (customer_transactions, customer_transaction_items, payments) remains as is.

9. Occasions (occasions)
Field	Type	Description
id	INTEGER	PK
name	TEXT NOT NULL	"Ramadan 2026"
starts_at	TEXT NOT NULL	—
ends_at	TEXT NOT NULL	—
icon	TEXT	Small display icon
active	INTEGER DEFAULT 1	—
In invoices: occasion_id INTEGER column (optional) in sales and purchases.

In settings: active_occasion_id key — current occasion. When active, new invoices are tagged.

10. Business Rules
Purchase invoice:

Adds to stock by default (can be overridden).

Increases supplier balance by total_cents.

If paid in cash, auto-records a payment equal to paid_cents.

Updates cost_cents for each product (PMP).

Supplier payment:

Carries a `method`: `cash`, `credit` or `bank`.

`cash` names the open cash session it is paid out of and writes a
`cash_movements` row of type `supplier_payment` carrying a **negative** amount.
Without a session there is no way to know which day the money left on, so a cash
payment that names no open session is refused rather than recorded.

`credit` and `bank` need no session and write no cash movement.

Deducts from linked invoice, or oldest-first (FIFO).

Reduces supplier balance.

Supplier return:

Reduces supplier balance.

Reduces stock (by default).

Does not recompute PMP (point for later discussion).

Sale:

Freezes cost_at_sale_cents in the item (cost at sale time).

Profit is not recomputed afterward.

Credit sale invoice:

Increases customer balance.

On payment, reduces balance.

A repayment is written as a negative row on the same ledger, which is why the
balance sums every row with no filter on the sign: filtering to positive would
drop both repayments and cancellations and leave a settled or cancelled sale
still showing as debt.

Cancelling a credit sale:

`reverseCustomerDebt` writes a negative `customer_transactions` row carrying
`reversed_transaction_id` against the original, and negative
`customer_transaction_items` rows carrying `reversed_id` against each original
item, at the same prices and costs. Nothing is updated or deleted.

The goods go back on the shelf through a `stock_movements` row of reason
`customer_debt_reversal`.

No cash movement: a credit sale never put money in the drawer, so cancelling it
takes none out.

A sale can be cancelled once — a lookup on `reversed_transaction_id` refuses the
second attempt — and a negative row (a repayment, or an earlier cancellation) is
not a sale and cannot be cancelled.

11. Reports
Daily (automatic on session close):

Total sales today.

Total profit.

Invoice count.

Expected vs actual cash difference.

Monthly (automatic at start of each month):

Monthly sales, expenses, net profit.

Monthly purchases per supplier.

Accumulated debts.

On demand:

Each supplier's balance, each customer's balance.

Current occasion (sales, profits).

Best-selling products