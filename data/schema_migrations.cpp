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