#include "schema_migrations.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include <functional>

namespace app::data {

namespace {

QStringList tableColumns(const QSqlDatabase& db, const QString& table)
{
    QStringList columns;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return columns;
    }
    while (query.next()) {
        columns << query.value(1).toString();
    }
    return columns;
}

// Runs one statement, reporting rather than throwing. Every migration goes
// through here so that a refusal ends up in the log with its own text attached
// instead of as a bare false that the caller has to remember to check.
bool run(const QSqlDatabase& db, const QString& sql, const QString& what)
{
    QSqlQuery query(db);
    if (query.exec(sql)) {
        return true;
    }
    qWarning() << "schema migration:" << what << "failed:" << query.lastError().text() << "---" << sql;
    return false;
}

// ---------------------------------------------------------------------------
// 2 — supplier_payments.method
//
// A payment to a supplier used to be recorded as an amount and nothing else, so
// nothing downstream could tell money that came out of the drawer from money
// that went to a bank account or was settled on credit. Every report that adds
// up the till therefore ignored it, and a day with a large supplier payment on
// it reconciled short by exactly that amount.
//
// Rows written before the column existed are tagged 'cash': they were all
// recorded from the supplier payment form, which had no other choice. No cash
// movement is written for them, deliberately. Back-dating a movement would put
// a negative row on a session that was opened, counted and closed against a
// till figure that did not include it, so the past would stop reconciling in a
// different place. The till figures for those days stay as they were counted;
// only days from here on carry the movement.
void migrateSupplierPaymentMethod(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("supplier_payments")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("supplier_payments")).contains(QStringLiteral("method"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE supplier_payments "
                                "ADD COLUMN method TEXT NOT NULL DEFAULT 'cash'"),
                 QStringLiteral("add supplier_payments.method"))) {
            return;
        }
    }

    // A row that predates the column reads as NULL or blank if something wrote it
    // directly; normalise those to 'cash' too, so every row carries a method a
    // reader can switch on without a fallback.
    QSqlQuery fix(db);
    if (fix.exec(QStringLiteral("UPDATE supplier_payments SET method = 'cash' "
                                "WHERE method IS NULL OR method = ''"))) {
        return;
    }
    qWarning() << "schema migration: normalising supplier_payments.method failed:"
               << fix.lastError().text();
}

// ---------------------------------------------------------------------------
// 3 — supplier_payments.reversed_id
//
// The column was not in the original table. Migration 2 added method; this one
// adds the reversal link so the uniqueness index in the next migration can use it.
void migrateSupplierPaymentReversal(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("supplier_payments")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("supplier_payments")).contains(QStringLiteral("reversed_id"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE supplier_payments "
                                "ADD COLUMN reversed_id INTEGER NOT NULL DEFAULT 0"),
                 QStringLiteral("add supplier_payments.reversed_id"))) {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 4 — one reversal per original, enforced by the database
//
// Each of these tables holds an append-only ledger whose reversal is a mirrored
// row pointing back at the original through a reversed column. Nothing stopped
// the same original being mirrored twice, so a second refund wrote a second
// negative row and the till gained the money back twice. Every service that
// writes one now checks first, which is what the checks are for; these indexes
// are the second line, for the writer nobody remembered to change.
//
// Partial, because the sentinel that means "not a reversal" is 0 and it appears
// on every ordinary row: a plain unique index would refuse the second sale,
// the second expense, the first payment of every customer. WHERE reversed <> 0
// restricts the rule to the rows that actually claim to reverse something.
//
// Migration refuses to fail a startup. A ledger that already holds a duplicate
// is exactly the database that must keep opening, so the duplicate is reported
// and the index is skipped; the service-level checks keep working, and the
// operator is told how to put the ledger right. The fix is an offsetting entry,
// never a delete: the books are append-only, and a row removed to satisfy an
// index takes the record of a real event with it.
//
// Nothing in the sync protocol writes one of these rows. core::SyncOpType is
// Sale, CustomerDebt and CustomerPayment, and all three arrive as ordinary rows
// with the sentinel 0 — the server applies a sale, a debt or a repayment, never
// a reversal. So no inbound batch can trip these indexes, and the server does
// not need a branch to recognise one: it cannot receive it.
struct PartialUniqueIndex {
    const char* index;
    const char* table;
    const char* column;
};

constexpr PartialUniqueIndex kReversalIndexes[] = {
    {"uq_payments_one_reversal", "payments", "reversed_id"},
    {"uq_expenses_one_reversal", "expenses", "reversed_id"},
    {"uq_owner_drawings_one_reversal", "owner_drawings", "reversed_id"},
    {"uq_sales_one_reversal", "sales", "reversed_sale_id"},
    {"uq_customer_transactions_one_reversal", "customer_transactions", "reversed_transaction_id"},
};

// Reports the ids that more than one reversal row points at, or an empty list.
// Read before the index is attempted so the log can name the rows to look at
// rather than only saying the index did not fit.
QStringList duplicatedReversalTargets(const QSqlDatabase& db, const PartialUniqueIndex& spec)
{
    QStringList targets;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE %3 <> 0 GROUP BY %3 HAVING COUNT(*) > 1 "
                                 "ORDER BY %3")
                      .arg(QLatin1StringView(spec.column), QLatin1StringView(spec.table),
                           QLatin1StringView(spec.column)));
    if (!query.exec()) {
        qWarning() << "schema migration: could not check" << spec.table << "for duplicate reversals:"
                   << query.lastError().text();
        return targets;
    }
    while (query.next()) {
        targets << query.value(0).toString();
    }
    return targets;
}

void migrateReversalUniqueness(const QSqlDatabase& db)
{
    for (const PartialUniqueIndex& spec : kReversalIndexes) {
        // A table that is not there yet cannot be indexed. Nothing to do.
        if (!tableColumns(db, QString::fromLatin1(spec.table)).contains(QString::fromLatin1(spec.column))) {
            continue;
        }

        const QStringList duplicates = duplicatedReversalTargets(db, spec);
        if (!duplicates.isEmpty()) {
            qWarning() << "schema migration: index" << spec.index << "was NOT created because"
                       << spec.table << "already holds more than one reversal of"
                       << duplicates.join(QStringLiteral(", "))
                       << "(-- " << spec.column
                       << "). Only the service checks are protecting this ledger from a second reversal;"
                       << "the database is not. Correct it by writing an offsetting entry against the"
                       << "duplicate, never by deleting a row: the books are append-only.";
            continue;
        }

        run(db,
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS %1 ON %2(%3) WHERE %3 <> 0")
                .arg(QLatin1StringView(spec.index), QLatin1StringView(spec.table),
                     QLatin1StringView(spec.column)),
            QStringLiteral("create %1").arg(QLatin1StringView(spec.index)));
    }
}

// ---------------------------------------------------------------------------
// 4 — indexes for the reports that scan by time
//
// Every repository here answers findBetween() with a range predicate and no
// index behind it, so a day's report reads the whole table and sorts it. These
// are the same columns the ranges are written against.
//
// Only columns a real query reads are indexed. customers(name) is deliberately
// absent: the table holds one row per account, orders of magnitude fewer than
// any ledger above, and a scan of it costs less than maintaining a second copy
// of every name. sync_outbox(status) earns its place differently — the scheduler
// asks for the pending rows on every tick, and that count grows with the backlog
// of a shop that has been offline.
struct PlainIndex {
    const char* index;
    const char* table;
    const char* columns;
};

constexpr PlainIndex kReportIndexes[] = {
    {"idx_payments_created_at", "payments", "created_at"},
    {"idx_expenses_created_at", "expenses", "created_at"},
    {"idx_owner_drawings_created_at", "owner_drawings", "created_at"},
    {"idx_sales_created_at", "sales", "created_at"},
    {"idx_customer_transactions_created_at", "customer_transactions", "created_at"},
    {"idx_stock_movements_created_at", "stock_movements", "created_at"},
    {"idx_cash_movements_created_at", "cash_movements", "created_at"},
    {"idx_audit_log_created_at", "audit_log", "created_at"},
    {"idx_cash_sessions_opened_at", "cash_sessions", "opened_at"},
    {"idx_purchases_purchased_at", "purchases", "purchased_at"},
    {"idx_supplier_payments_paid_at", "supplier_payments", "paid_at"},
    // Composite, not single-column, and the EXPLAIN QUERY PLAN says why: a
    // customer's card is `WHERE customer_id = ? ORDER BY created_at` and the
    // till's movement list is `WHERE session_id = ? ORDER BY created_at`. Indexed
    // on the first column alone, both read the rows and then sort them into a temp
    // B-tree; with the ordering column second, the index is already in the order
    // asked for and the sort disappears.
    {"idx_payments_customer_created", "payments", "customer_id, created_at"},
    {"idx_cash_movements_session_created", "cash_movements", "session_id, created_at"},
    {"idx_customer_transaction_items_transaction", "customer_transaction_items",
     "customer_transaction_id"},
    {"idx_sync_outbox_status", "sync_outbox", "status"},
};

void migrateReportIndexes(const QSqlDatabase& db)
{
    for (const PlainIndex& spec : kReportIndexes) {
        // Every column named has to be there, or the index is not the index that
        // was planned for.
        bool allPresent = true;
        for (const QString& column : QString::fromLatin1(spec.columns).split(QLatin1Char(','))) {
            allPresent = allPresent
                && tableColumns(db, QString::fromLatin1(spec.table)).contains(column.trimmed());
        }
        if (!allPresent) {
            continue;
        }
        run(db,
            QStringLiteral("CREATE INDEX IF NOT EXISTS %1 ON %2(%3)")
                .arg(QLatin1StringView(spec.index), QLatin1StringView(spec.table),
                     QLatin1StringView(spec.columns)),
            QStringLiteral("create %1").arg(QLatin1StringView(spec.index)));
    }
}

// ---------------------------------------------------------------------------
// The migration table. Append only: a released number is never edited or reused,
// so a database that has already run it never runs it twice. Every entry is
// idempotent anyway (each one looks before it creates), because a database
// restored from a backup taken between two versions can arrive with some of the
// work already done.

// The paid part of a purchase used to have no way to say how it was settled,
// so a cash payment left the drawer and nothing else, and the session
// reconciled short by exactly that amount. Every report that adds up the till
// therefore ignored it. The column is added with 'cash' as the default because
// all pre-existing paid invoices were recorded from the purchase form, which
// had no other choice. No cash movement is back-dated for them — that would put
// a negative row on a session that was already closed against a figure that did
// not include it.
void migratePurchaseMethod(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("purchases")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("purchases")).contains(QStringLiteral("method"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE purchases "
                                "ADD COLUMN method TEXT NOT NULL DEFAULT 'cash'"),
                 QStringLiteral("add purchases.method"))) {
            return;
        }
    }
}

// The void purchase feature needs to link the void header to the original. The
// column is added with 0 as the default ("not a void"). The partial unique
// index in the next migration uses it.
void migratePurchaseReversedId(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("purchases")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("purchases")).contains(QStringLiteral("reversed_id"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE purchases "
                                "ADD COLUMN reversed_id INTEGER NOT NULL DEFAULT 0"),
                 QStringLiteral("add purchases.reversed_id"))) {
            return;
        }
    }
}

// The void purchase feature needs to link void items to their originals. The
// column is added with 0 as the default ("not a void item").
void migratePurchaseItemReversedId(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("purchase_items")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("purchase_items")).contains(QStringLiteral("reversed_id"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE purchase_items "
                                "ADD COLUMN reversed_id INTEGER NOT NULL DEFAULT 0"),
                 QStringLiteral("add purchase_items.reversed_id"))) {
            return;
        }
    }
}

// Cash movements reference the originating document (sale, purchase, expense, etc.)
// so the till can be reconciled line-by-line. Old rows have NULL in both fields
// and are counted in the "unreferenced" bucket at close.
void migrateCashMovementReference(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("cash_movements")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("cash_movements")).contains(QStringLiteral("ref_type"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE cash_movements "
                                "ADD COLUMN ref_type TEXT"),
                 QStringLiteral("add cash_movements.ref_type"))) {
            return;
        }
    }
    if (!tableColumns(db, QStringLiteral("cash_movements")).contains(QStringLiteral("ref_id"))) {
        if (!run(db,
                 QStringLiteral("ALTER TABLE cash_movements "
                                "ADD COLUMN ref_id INTEGER"),
                 QStringLiteral("add cash_movements.ref_id"))) {
            return;
        }
    }
    // No backfill: old rows keep NULL. They will be reported as "unreferenced"
    // at close so the operator knows which historical movements lack a trace.
}

void migrateCashMovementReferenceIndex(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("cash_movements")).isEmpty()) {
        return;
    }
    if (!tableColumns(db, QStringLiteral("cash_movements")).contains(QStringLiteral("ref_type"))
        || !tableColumns(db, QStringLiteral("cash_movements")).contains(QStringLiteral("ref_id"))) {
        return;
    }
    run(db,
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_cash_movements_reference "
                       "ON cash_movements(ref_type, ref_id)"),
        QStringLiteral("create idx_cash_movements_reference"));
}

void migratePurchaseInitialPaymentMarker(const QSqlDatabase& db)
{
    if (tableColumns(db, QStringLiteral("supplier_payments")).isEmpty()
        || tableColumns(db, QStringLiteral("supplier_payments")).contains(
            QStringLiteral("is_purchase_initial_payment"))) {
        return;
    }
    if (!run(db,
        QStringLiteral("ALTER TABLE supplier_payments "
                       "ADD COLUMN is_purchase_initial_payment INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("add supplier_payments.is_purchase_initial_payment"))) {
        return;
    }
    run(db,
        QStringLiteral(
            "UPDATE supplier_payments SET is_purchase_initial_payment = 1 "
            "WHERE reversed_id = 0 AND purchase_id IS NOT NULL "
            "AND note = 'Purchase #' || purchase_id "
            "AND EXISTS (SELECT 1 FROM purchases p WHERE p.id = supplier_payments.purchase_id "
            "            AND p.paid_cents = supplier_payments.amount_cents "
            "            AND p.purchased_at = supplier_payments.paid_at) "
            "AND id = (SELECT MIN(sp.id) FROM supplier_payments sp "
            "          WHERE sp.purchase_id = supplier_payments.purchase_id AND sp.reversed_id = 0 "
            "          AND sp.note = 'Purchase #' || sp.purchase_id)"),
        QStringLiteral("mark historical purchase-time payments"));
}

// ---------------------------------------------------------------------------
// 12 — sales.adjustment_cents + customer_transactions.adjustment_cents
//
// A cashier could only ever sell at exactly the sum of the lines: a discount was
// made by rewriting a unit price, which changed the cost the goods went out at
// and could not be undone from the invoice afterwards. A whole-invoice adjustment
// — a surcharge, a round-figure discount, a correction — has nowhere to live at
// all, so it is added as a signed column on both ledgers. Both are 0 on every
// row that predates it, which is the truth: those sales were never adjusted.
//
// The two tables get it together because both are invoices and both are read back
// the same way. A credit sale with a discount on it is exactly as common as a cash
// one.
//
// No backfill and no rewrite of the totals: total_cents already holds the sum of
// the lines and nothing else, so leaving it and leaving the new column at 0 keeps
// every historical row stating what it always stated.
void migrateSaleAdjustment(const QSqlDatabase& db)
{
    struct AdjustmentColumn {
        const char* table;
    };

    constexpr AdjustmentColumn kAdjustmentColumns[] = {
        {"sales"},
        {"customer_transactions"},
    };

    for (const AdjustmentColumn& spec : kAdjustmentColumns) {
        const QString table = QString::fromLatin1(spec.table);
        // A table that is not there yet has nothing to alter. createSchema() is
        // run before this and creates it with the column already in place, so the
        // common case is the empty check below.
        if (tableColumns(db, table).isEmpty()) {
            continue;
        }
        // SQLite has no ADD COLUMN IF NOT EXISTS, so the presence check is the
        // only way to write this idempotently. Without it a second run fails with
        // "duplicate column name" on every open.
        if (tableColumns(db, table).contains(QStringLiteral("adjustment_cents"))) {
            continue;
        }
        if (!run(db,
                 QStringLiteral("ALTER TABLE %1 ADD COLUMN adjustment_cents INTEGER NOT NULL DEFAULT 0")
                     .arg(QLatin1StringView(spec.table)),
                 QStringLiteral("add %1.adjustment_cents").arg(QLatin1StringView(spec.table)))) {
            continue;
        }
    }
}

// ---------------------------------------------------------------------------
// 13 — the carton (كرتونة) on products, and what a ledger line was counted in
//
// A product knew how many pieces made up its package but nothing could *be* sold
// or bought by the package: every line carried a bare quantity and a unit price,
// so a cashier wanting to charge for a carton had to divide the price by hand,
// round, and be quietly wrong by the remainder every time. The remainder is the
// part that cannot be recovered later — once the unit price is rounded, nobody can
// tell whether the shortfall was a rounding artefact or a wrong price. So the
// exact supplier cost of a carton is kept alongside it (package_cost_cents) rather
// than recomputed as a division, and products.package_size is deliberately left
// alone: it is still written by the product form, and nothing in production reads
// it — pieces_per_package is what the stock arithmetic reads.
//
// Every ledger line gains unit_kind ("piece" or "package") so a line says what it
// was counted in, and pieces_consumed / pieces_received so the pieces are counted
// explicitly instead of being inferred later from a quantity whose unit is no
// longer recorded anywhere. Inferring them after the fact is what would go wrong:
// a carton line of 3 and a piece line of 3 are the same number and need different
// stock arithmetic, and once the unit is not stored the two cannot be told apart.
//
// The backfill sets pieces_consumed/pieces_received from the existing quantity
// with unit_kind left at its 'piece' default, which is the truth about every row
// that predates this: nothing was ever sold by the carton, so every historical
// quantity was a count of pieces. Without the backfill those rows would read 0
// pieces and a stock count derived from them would empty the shelves.
void migrateProductPackages(const QSqlDatabase& db)
{
    // One row per column to add. type is the SQLite column definition, which is
    // spelled per table because the defaults differ; the presence check below is
    // what keeps a second run from failing with "duplicate column name".
    struct PackageColumn {
        const char* table;
        const char* column;
        const char* type;
        const char* what;
    };

    constexpr PackageColumn kPackageColumns[] = {
        // What the shop calls a carton, in Arabic because that is what is printed
        // on it and what the cashier will be typing. Stored rather than left as a
        // constant so a shop that calls them something else can say so without a
        // new build.
        {"products", "package_name", "TEXT NOT NULL DEFAULT 'كرتونة'", "add products.package_name"},
        // How many pieces are in one carton. Kept beside the older package_size
        // rather than replacing it, so the form and the mapper do not have to
        // change in the same commit: both are written from one widget and both
        // default to 1. Nothing in production reads package_size — this column is
        // the one the stock arithmetic reads — so the second number is a
        // transition leftover, not a second reader's requirement.
        {"products", "pieces_per_package", "INTEGER NOT NULL DEFAULT 1",
         "add products.pieces_per_package"},
        // The carton's own barcode, NULL when the product has none. Not UNIQUE at
        // the database level, deliberately: products.barcode is UNIQUE so that a
        // blank means "no barcode" and does not collide with other blanks, and a
        // carton barcode has exactly the same problem. Making this UNIQUE would
        // let one row refuse another over a typo, which is a barcode mistake
        // turning into an unsaveable product.
        {"products", "package_barcode", "TEXT", "add products.package_barcode"},
        // What one carton costs the shop, in cents, exactly as invoiced. Not
        // package_cost / pieces_per_package, because that division rounds and the
        // remainder is the figure that decides whether the shelf price is right.
        {"products", "package_cost_cents", "INTEGER NOT NULL DEFAULT 0",
         "add products.package_cost_cents"},

        // What this line was counted in. 'piece' is the default because that is
        // what every line written before this column existed was.
        {"sale_items", "unit_kind", "TEXT NOT NULL DEFAULT 'piece'", "add sale_items.unit_kind"},
        // Pieces actually taken off the shelf, so the stock movement is a count of
        // pieces rather than a re-derivation from a quantity whose unit is not
        // recorded. Zero on rows that predate this, hence the backfill below.
        {"sale_items", "pieces_consumed", "INTEGER NOT NULL DEFAULT 0",
         "add sale_items.pieces_consumed"},

        {"customer_transaction_items", "unit_kind", "TEXT NOT NULL DEFAULT 'piece'",
         "add customer_transaction_items.unit_kind"},
        // Same as sale_items, and for the same reason: a credit sale moves stock
        // exactly as a cash one does, so a carton line here that cannot say how
        // many pieces it took would reconcile the shelf against the till wrongly.
        {"customer_transaction_items", "pieces_consumed", "INTEGER NOT NULL DEFAULT 0",
         "add customer_transaction_items.pieces_consumed"},

        {"purchase_items", "unit_kind", "TEXT NOT NULL DEFAULT 'piece'",
         "add purchase_items.unit_kind"},
        // Pieces put *on* the shelf, and named differently on purpose: goods come
        // in and go out, and a single column would have to mean whichever the row
        // happened to be. This one is only ever read off a purchase.
        {"purchase_items", "pieces_received", "INTEGER NOT NULL DEFAULT 0",
         "add purchase_items.pieces_received"},

        // A return takes goods back, so it is counted in the same terms as a sale
        // and borrows pieces_consumed rather than inventing a third name.
        {"supplier_return_items", "unit_kind", "TEXT NOT NULL DEFAULT 'piece'",
         "add supplier_return_items.unit_kind"},
        {"supplier_return_items", "pieces_consumed", "INTEGER NOT NULL DEFAULT 0",
         "add supplier_return_items.pieces_consumed"},
    };

    for (const PackageColumn& spec : kPackageColumns) {
        const QString table = QString::fromLatin1(spec.table);
        const QString column = QString::fromLatin1(spec.column);
        // A table that is not there yet has nothing to alter. createSchema() runs
        // before this and creates it with the columns already in place, so on a
        // fresh install this is the branch that runs for every row above.
        if (tableColumns(db, table).isEmpty()) {
            continue;
        }
        // SQLite has no ADD COLUMN IF NOT EXISTS, so the presence check is the only
        // way to write this idempotently. Without it a second run fails with
        // "duplicate column name" on every open.
        if (tableColumns(db, table).contains(column)) {
            continue;
        }
        run(db,
            QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                .arg(QString::fromUtf8(spec.table), QString::fromUtf8(spec.column),
                     QString::fromUtf8(spec.type)),
            QString::fromLatin1(spec.what));
    }

    // The backfills. A row that predates the columns reads 0 pieces, which is a
    // figure the stock arithmetic would believe: a sale of 5 would empty 5 pieces
    // on the next count and then some, and the shelves would go negative. Every
    // such quantity was a count of pieces, because nothing could be sold or bought
    // by the carton before this migration — so copying quantity across states what
    // was true, and unit_kind stays at its 'piece' default to say so.
    struct PackageBackfill {
        const char* table;
        const char* column;
        const char* what;
    };

    constexpr PackageBackfill kPackageBackfills[] = {
        {"sale_items", "pieces_consumed", "backfill sale_items.pieces_consumed"},
        {"customer_transaction_items", "pieces_consumed",
         "backfill customer_transaction_items.pieces_consumed"},
        {"purchase_items", "pieces_received", "backfill purchase_items.pieces_received"},
        {"supplier_return_items", "pieces_consumed",
         "backfill supplier_return_items.pieces_consumed"},
    };

    for (const PackageBackfill& spec : kPackageBackfills) {
        const QString table = QString::fromLatin1(spec.table);
        const QString column = QString::fromLatin1(spec.column);
        // Same guard as above: no table, or no column, means there is nothing to
        // correct. A database where the ALTER failed lands here and is skipped
        // rather than warned about twice for one problem.
        if (!tableColumns(db, table).contains(column)) {
            continue;
        }
        QSqlQuery fix(db);
        if (fix.exec(QStringLiteral("UPDATE %1 SET %2 = quantity WHERE %2 = 0")
                         .arg(QString::fromUtf8(spec.table), QString::fromUtf8(spec.column)))) {
            continue;
        }
        qWarning() << "schema migration:" << spec.what << "failed:" << fix.lastError().text();
    }
}

// ---------------------------------------------------------------------------
// 14 — copy package_size into pieces_per_package
//
// Migration 13 added pieces_per_package with a default of 1 and did not copy
// the figure across, so every product a shop had already described as a tray
// of twelve read back as one and the first carton sale would have taken a
// single piece off the shelf. Copy it across where the two disagree and only
// the old column has a figure in it; rows where the shop has already set the
// new column to something other than 1 are left alone.
void migrateBackfillPiecesPerPackage(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("products"));
    // Either column missing means there is nothing to copy between. A fresh
    // install has both, but a database that reached this entry without the
    // ALTER above being applied is skipped rather than warned about twice for
    // one problem.
    if (!columns.contains(QStringLiteral("package_size"))
        || !columns.contains(QStringLiteral("pieces_per_package"))) {
        return;
    }

    run(db,
        QStringLiteral("UPDATE products SET pieces_per_package = package_size "
                       "WHERE package_size > 1 AND pieces_per_package = 1"),
        QStringLiteral("backfill products.pieces_per_package from products.package_size"));
}

// ---------------------------------------------------------------------------
// The table below is keyed by schema version, and runSchemaMigrations() walks it
// so a database that has already run it never runs it twice. Every entry is
// idempotent anyway (each one looks before it creates), because a database
// restored from a backup taken between two versions can arrive with some of the
// work already done.
const std::pair<int, std::function<void(const QSqlDatabase&)>> kMigrations[] = {
    // 1 is the version this table started at. Everything it would have held was
    // applied unconditionally by createSchema() before versioning existed, and
    // still is, so there is nothing to replay for it.
    {2, migrateSupplierPaymentMethod},
    // The reversal column was not in the original schema; it arrived with the
    // supplier payment reversal feature. Existing databases need it added so the
    // index migration can create the partial unique index on it.
    {3, migrateSupplierPaymentReversal},
    {4, migrateReversalUniqueness},
    // The paid part of a purchase used to have no way to say how it was settled,
    // so a cash payment left the drawer and nothing else, and the session
    // reconciled short by exactly that amount. Every report that adds up the till
    // therefore ignored it. The column is added with 'cash' as the default because
    // all pre-existing paid invoices were recorded from the purchase form, which
    // had no other choice. No cash movement is back-dated for them — that would put
    // a negative row on a session that was already closed against a figure that did
    // not include it.
    {5, migratePurchaseMethod},
    // The void purchase feature needs to link the void header to the original. The
    // column is added with 0 as the default ("not a void"). The partial unique
    // index in the next migration uses it.
    {6, migratePurchaseReversedId},
    // The void purchase feature needs to link void items to their originals. The
    // column is added with 0 as the default ("not a void item").
    {7, migratePurchaseItemReversedId},
    {8, migrateReportIndexes},
    // cash_movements.ref_type + ref_id for till reconciliation.
    {9, migrateCashMovementReference},
    {10, migrateCashMovementReferenceIndex},
    {11, migratePurchaseInitialPaymentMarker},
    // A cashier had no way to change the whole invoice: a discount had to be made
    // by rewriting a unit price, which changed the cost the goods left at and left
    // nothing on the invoice to say so. The column is added to both ledgers at 0,
    // which is what every existing row says about itself.
    {12, migrateSaleAdjustment},
    // Cartons. products gains what a package is called, how many pieces are in it,
    // its own barcode and its exact cost; every ledger line that moves stock gains
    // the unit it was counted in and the piece count, because a quantity alone
    // cannot say whether it meant 3 pieces or 3 cartons. Existing rows are
    // backfilled from their quantity, which is a count of pieces by definition —
    // nothing could be sold or bought by the carton before now.
    {13, migrateProductPackages},
    // The carton column arrived with its default of 1 and the old tray figure was
    // left where it was, so the two disagreed on every product a shop had already
    // described. The old one is copied across only where the new one still holds
    // its default, so a row a shop has since set by hand is not overwritten.
    {14, migrateBackfillPiecesPerPackage},
};

} // namespace

int readSchemaVersion(const QSqlDatabase& db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

void runSchemaMigrations(const QSqlDatabase& db)
{
    const int from = readSchemaVersion(db);
    if (from >= kSchemaVersion) {
        return;
    }

    for (const auto& [version, migration] : kMigrations) {
        if (version <= from || version > kSchemaVersion) {
            continue;
        }
        migration(db);
    }

    // Stamped unconditionally at the end, including when a migration above only
    // warned: the version records that this build has visited the database, and
    // the entry that warned about a skipped index has already named itself in
    // the log. Re-running it on every open would repeat the same warning on
    // every startup forever.
    QSqlQuery stamp(db);
    if (!stamp.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        qWarning() << "schema migration: could not stamp the schema version:"
                   << stamp.lastError().text();
    }
}

} // namespace app::data