#include "database.h"

#include "schema_migrations.h"

#include <QPair>

#include <QCryptographicHash>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace app::data {

namespace {

// At most one cash session may be open at a time. Kept as a constant because the
// same name is both created and then looked up, and a mismatch between the two
// would silently disable the check.
const char* kSingleOpenSessionIndex = "idx_cash_sessions_one_open";

QString uniqueConnectionName()
{
    return QStringLiteral("mahali_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

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

// Idempotent, runs on every open: adds only the columns an older users table
// lacks. SQLite forbids expression defaults in ALTER, so pre-existing rows get
// an empty created_at.
void migrateUsersTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("users"));
    if (columns.isEmpty()) {
        return;
    }
    QStringList statements;
    if (!columns.contains(QStringLiteral("pin"))) {
        statements << QStringLiteral("ALTER TABLE users ADD COLUMN pin TEXT NOT NULL DEFAULT ''");
    }
    if (!columns.contains(QStringLiteral("active"))) {
        statements << QStringLiteral("ALTER TABLE users ADD COLUMN active INTEGER NOT NULL DEFAULT 1");
    }
    if (!columns.contains(QStringLiteral("created_at"))) {
        statements << QStringLiteral("ALTER TABLE users ADD COLUMN created_at TEXT NOT NULL DEFAULT ''");
    }
    for (const QString& statement : statements) {
        QSqlQuery alter(db);
        alter.exec(statement);
    }
}

// zakat_settings was a second key/value table holding exactly the same three
// columns as settings, and the only thing that ever told the two apart was which
// one a key had been written to first. Every zakat key lives in settings now, so
// this folds whatever is left into it and drops the table.
//
// settings wins a collision rather than zakat_settings: it is the table the rest
// of the app reads, and a key that is already there was written by a build that
// knew what it meant. Nothing is lost, because the two tables only ever held
// "enabled" and "nisab_cents" under the same names.
//
// Idempotent, and a no-op on a database that never had the table.
void migrateZakatSettingsIntoSettings(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("zakat_settings"));
    if (columns.isEmpty()) {
        return;
    }
    QSqlQuery copy(db);
    if (!copy.exec(QStringLiteral(
            "INSERT OR IGNORE INTO settings (key, value) SELECT key, value FROM zakat_settings"))) {
        return;
    }
    QSqlQuery drop(db);
    drop.exec(QStringLiteral("DROP TABLE zakat_settings"));
}

// Idempotent, runs on every open. Adds missing columns to suppliers:
// phone, address, notes, opening_balance_cents, active.
// SQLite does not enforce NOT NULL on columns added via ALTER TABLE with a
// DEFAULT, but the defaults ensure new rows get sensible values.
void migrateSuppliersTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("suppliers"));
    if (columns.isEmpty()) {
        return;
    }
    QStringList statements;
    if (!columns.contains(QStringLiteral("phone"))) {
        statements << QStringLiteral("ALTER TABLE suppliers ADD COLUMN phone TEXT NOT NULL DEFAULT ''");
    }
    if (!columns.contains(QStringLiteral("address"))) {
        statements << QStringLiteral("ALTER TABLE suppliers ADD COLUMN address TEXT NOT NULL DEFAULT ''");
    }
    if (!columns.contains(QStringLiteral("notes"))) {
        statements << QStringLiteral("ALTER TABLE suppliers ADD COLUMN notes TEXT NOT NULL DEFAULT ''");
    }
    if (!columns.contains(QStringLiteral("opening_balance_cents"))) {
        statements << QStringLiteral("ALTER TABLE suppliers ADD COLUMN opening_balance_cents INTEGER NOT NULL DEFAULT 0");
    }
    if (!columns.contains(QStringLiteral("active"))) {
        statements << QStringLiteral("ALTER TABLE suppliers ADD COLUMN active INTEGER NOT NULL DEFAULT 1");
    }
    for (const QString& statement : statements) {
        QSqlQuery alter(db);
        alter.exec(statement);
    }
}

// Idempotent, runs on every open. Adds the two columns the customer card and the
// page's own filters need: the opening figure the balance starts from, and the
// flag the "Tous" filter reads to hide a customer who is no longer served.
// Same shape as migrateSuppliersTable, and for the same reason: the table is
// created above with only id/name/phone, and ALTER fills old rows with the
// defaults below, so no existing customer changes balance or visibility.
void migrateCustomersTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("customers"));
    if (columns.isEmpty()) {
        return;
    }
    QStringList statements;
    if (!columns.contains(QStringLiteral("opening_balance_cents"))) {
        statements << QStringLiteral("ALTER TABLE customers ADD COLUMN opening_balance_cents INTEGER NOT NULL DEFAULT 0");
    }
    if (!columns.contains(QStringLiteral("active"))) {
        statements << QStringLiteral("ALTER TABLE customers ADD COLUMN active INTEGER NOT NULL DEFAULT 1");
    }
    for (const QString& statement : statements) {
        QSqlQuery alter(db);
        alter.exec(statement);
    }
}

// Idempotent, runs on every open. Adds the reference column that links a
// movement to whatever produced it, e.g. "Purchase #42". Rows written before the
// column existed get an empty reference rather than NULL, so a reader never has
// to tell "no source" apart from "the column is missing".
void migrateStockMovementsTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("stock_movements"));
    if (columns.isEmpty()) {
        return;
    }
    if (columns.contains(QStringLiteral("reference"))) {
        return;
    }
    QSqlQuery alter(db);
    alter.exec(QStringLiteral("ALTER TABLE stock_movements ADD COLUMN reference TEXT NOT NULL DEFAULT ''"));
}

// A sale is stamped with the occasion running when it happened, so the reports
// can split a day's takings by event. sales predates occasions, so existing
// databases are given the column here; a fresh one already has it.
void migrateSalesTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("sales"));
    if (columns.isEmpty()) {
        return;
    }
    if (columns.contains(QStringLiteral("occasion_id"))) {
        return;
    }
    QSqlQuery alter(db);
    alter.exec(QStringLiteral("ALTER TABLE sales ADD COLUMN occasion_id INTEGER"));
}

// Reports whether `column` of `table` is declared NOT NULL. PRAGMA table_info
// rows are (cid, name, type, notnull, dflt_value, pk).
bool columnIsNotNull(const QSqlDatabase& db, const QString& table, const QString& column)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return query.value(3).toInt() != 0;
        }
    }
    return false;
}

// Idempotent, runs on every open. Two upgrades for products:
//  1. sold_by_weight, a plain ADD COLUMN (SQLite forbids expression defaults in
//     ALTER, but a literal 0 is fine).
//  2. barcode becomes nullable so several quick items can share "no barcode".
//     UNIQUE still permits only one '' and any number of NULLs, so quick items
//     are stored as NULL. SQLite cannot drop NOT NULL in place, so the table is
//     rebuilt following the documented 12-step ALTER procedure.
//
// The rebuild is not a plain rename dance: trg_stock_after_insert is declared on
// stock_movements yet its body writes to products, so DROP TABLE products leaves
// it dangling and the following RENAME fails with "no such table: main.products".
// Every trigger is therefore snapshotted and recreated around the swap.
//
// Returns an empty string on success, otherwise the failing statement.
QString migrateProductsTable(const QSqlDatabase& db)
{
    const QStringList columns = tableColumns(db, QStringLiteral("products"));
    if (columns.isEmpty()) {
        return QString();
    }

    const bool barcodeNullable = !columnIsNotNull(db, QStringLiteral("products"), QStringLiteral("barcode"));
    const bool hasWeightColumn = columns.contains(QStringLiteral("sold_by_weight"));
    if (barcodeNullable && hasWeightColumn) {
        return QString();
    }

    // ADD COLUMN covers the common case: barcode is already nullable and only
    // sold_by_weight is missing.
    if (barcodeNullable) {
        QSqlQuery alter(db);
        if (alter.exec(QStringLiteral(
                "ALTER TABLE products ADD COLUMN sold_by_weight INTEGER NOT NULL DEFAULT 0"))) {
            return QString();
        }
        return alter.lastError().text();
    }

    // Triggers that touch the table, wherever they are declared. Saved, dropped
    // before the swap and replayed after it: a trigger left in place would be
    // re-parsed by RENAME and fail against the half-swapped schema.
    QList<QPair<QString, QString>> triggers;
    {
        QSqlQuery query(db);
        if (query.exec(QStringLiteral(
                "SELECT name, sql FROM sqlite_master WHERE type = 'trigger' AND sql IS NOT NULL "
                "AND (tbl_name IN ('products', 'stock_movements', 'sale_items', "
                "'customer_transaction_items') OR sql LIKE '%products%')"))) {
            while (query.next()) {
                triggers.append(qMakePair(query.value(0).toString(), query.value(1).toString()));
            }
        }
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = OFF"));

    QStringList statements = {
        QStringLiteral("BEGIN"),
    };
    for (const auto& trigger : triggers) {
        statements << QStringLiteral("DROP TRIGGER IF EXISTS %1").arg(trigger.first);
    }
    statements
        << QStringLiteral(
               "CREATE TABLE products_migrated ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "barcode TEXT UNIQUE,"
               "name TEXT NOT NULL,"
               "cost_price_cents INTEGER NOT NULL DEFAULT 0,"
               "sale_price_cents INTEGER NOT NULL DEFAULT 0,"
               "quantity INTEGER NOT NULL DEFAULT 0,"
               "unit TEXT NOT NULL DEFAULT '',"
               "package_size INTEGER NOT NULL DEFAULT 1,"
               "active INTEGER NOT NULL DEFAULT 1,"
               "sold_by_weight INTEGER NOT NULL DEFAULT 0)")
        // A blank barcode becomes NULL so that quick items stop colliding on
        // the UNIQUE index; real barcodes are copied through untouched.
        << QStringLiteral(
               "INSERT INTO products_migrated "
               "(id, barcode, name, cost_price_cents, sale_price_cents, quantity, unit, "
               " package_size, active, sold_by_weight) "
               "SELECT id, CASE WHEN TRIM(barcode) = '' THEN NULL ELSE barcode END, "
               " name, cost_price_cents, sale_price_cents, quantity, unit, package_size, active, 0 "
               "FROM products")
        << QStringLiteral("DROP TABLE products")
        << QStringLiteral("ALTER TABLE products_migrated RENAME TO products")
        << QStringLiteral("COMMIT");

    QString error;
    for (const QString& statement : statements) {
        QSqlQuery step(db);
        if (!step.exec(statement)) {
            error = QStringLiteral("%1: %2").arg(statement.left(48), step.lastError().text());
            QSqlQuery rollback(db);
            rollback.exec(QStringLiteral("ROLLBACK"));
            break;
        }
    }

    if (error.isEmpty()) {
        for (const auto& trigger : triggers) {
            QSqlQuery recreate(db);
            if (!recreate.exec(trigger.second)) {
                error = QStringLiteral("recreate trigger %1: %2")
                            .arg(trigger.first, recreate.lastError().text());
                break;
            }
        }
    }

    QSqlQuery restore(db);
    restore.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return error;
}

} // namespace

Database::Database(const QString& filePath, DatabaseMode mode)
    : m_mode(mode)
{
    const QString connectionName = uniqueConnectionName();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    m_db.setDatabaseName(filePath);
    if (!m_db.open()) {
        throw std::runtime_error(
            QStringLiteral("Failed to open database: %1").arg(m_db.lastError().text()).toStdString());
    }
    applyPragmas();
    createSchema();
    // A fresh install gets the whole current shape from createSchema() above and
    // has nothing to replay. A database carried over from an older build gets the
    // shape it was created with and is walked forward from here.
    runSchemaMigrations(m_db);
}

Database::~Database()
{
    if (m_db.isValid() && m_db.isOpen()) {
        const QString connectionName = m_db.connectionName();
        m_db.close();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

QSqlDatabase Database::handle() const
{
    // Handing out the handle is the start of an operation, so the reason from
    // whatever ran before it is no longer about to be read.
    clearError();
    return m_db;
}

bool Database::beginTransaction()
{
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

bool Database::commit()
{
    if (!m_db.commit()) {
        const QString commitError = m_db.lastError().text();
        // A failed COMMIT (e.g. SQLITE_BUSY) leaves the SQLite transaction
        // open, which would make every following beginTransaction() fail.
        // Close it explicitly so the connection stays usable.
        if (!m_db.rollback()) {
            m_lastError = QStringLiteral("commit failed: %1; rollback also failed: %2")
                              .arg(commitError, m_db.lastError().text());
        } else {
            m_lastError = commitError;
        }
        return false;
    }
    return true;
}

bool Database::rollback()
{
    if (!m_db.rollback()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

QString Database::lastError() const
{
    return m_lastError;
}

void Database::recordError(const QSqlError& err, const QString& context)
{
    m_lastSqlError = err;
    m_lastErrorContext = context;
    // The context is folded into the text the services already propagate, so the
    // message an operator finally sees says which write failed, not just that
    // SQLite said no.
    m_lastError = context.isEmpty() ? err.text() : QStringLiteral("%1: %2").arg(context, err.text());
}

QString Database::lastErrorContext() const
{
    return m_lastErrorContext;
}

void Database::clearError() const
{
    m_lastError.clear();
    m_lastErrorContext.clear();
    m_lastSqlError = QSqlError();
}

void Database::applyPragmas()
{
    QStringList pragmas = {
        // The server DB switched to WAL: concurrent phone syncs read without
        // blocking the writer. The device keeps the plain rollback journal so a
        // power cut never leaves a partially committed page on disk.
        m_mode == DatabaseMode::Server ? QStringLiteral("PRAGMA journal_mode = WAL;")
                                       : QStringLiteral("PRAGMA journal_mode = DELETE;"),
        // FULL: fsync every committed transaction so a sudden power cut cannot
        // lose committed money/stock even after days offline.
        QStringLiteral("PRAGMA synchronous = FULL;"),
        QStringLiteral("PRAGMA foreign_keys = ON;"),
        QStringLiteral("PRAGMA busy_timeout = 5000;"),
    };
    execStatements(pragmas, QStringLiteral("pragma"));
}

void Database::createSchema()
{
    const QStringList schema = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS products ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "barcode TEXT UNIQUE,"
            "name TEXT NOT NULL,"
            "cost_price_cents INTEGER NOT NULL DEFAULT 0,"
            "sale_price_cents INTEGER NOT NULL DEFAULT 0,"
            "quantity INTEGER NOT NULL DEFAULT 0,"
            "unit TEXT NOT NULL DEFAULT '',"
            "package_size INTEGER NOT NULL DEFAULT 1,"
            "active INTEGER NOT NULL DEFAULT 1,"
            "sold_by_weight INTEGER NOT NULL DEFAULT 0,"
            // What the shop calls a carton. Stored, not a constant, so a shop
            // whose word for it is not this one can change it without a build.
            "package_name TEXT NOT NULL DEFAULT 'كرتونة',"
            // Kept beside pieces_per_package so the existing form and mapper do
            // not have to change at once; the dialog writes both from the same
            // widget. Nothing reads package_size in production — pieces_per_package
            // is the column the stock arithmetic uses. This one exists only for a
            // transition, not because two readers need two numbers.
            "pieces_per_package INTEGER NOT NULL DEFAULT 1,"
            // The carton's own barcode, NULL when there is none. Deliberately not
            // UNIQUE, for the same reason products.barcode is allowed to be blank:
            // one typo would otherwise make a product unsaveable.
            "package_barcode TEXT,"
            // What a carton costs, in cents, as invoiced. Not derived by dividing,
            // because the division rounds and the remainder is exactly what a
            // shelf price has to be checked against.
            "package_cost_cents INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sales ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT NOT NULL,"
            "total_cents INTEGER NOT NULL,"
            // What was changed by hand on the invoice, already signed. Zero on
            // every ordinary sale: it is not a sentinel here, it is the absence
            // of an adjustment.
            "adjustment_cents INTEGER NOT NULL DEFAULT 0,"
            "device_id TEXT NOT NULL,"
            "oversold INTEGER NOT NULL DEFAULT 0,"
            "reversed_sale_id INTEGER NOT NULL DEFAULT 0,"
            // Nullable on purpose, unlike the 0 sentinels above: no occasion
            // running is a real state, and occasion_id is never 0.
            "occasion_id INTEGER);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS occasions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "starts_at TEXT NOT NULL,"
            "ends_at TEXT NOT NULL,"
            "icon TEXT NOT NULL DEFAULT '',"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sale_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sale_id INTEGER NOT NULL REFERENCES sales(id),"
            "product_id INTEGER NOT NULL REFERENCES products(id),"
            "quantity INTEGER NOT NULL,"
            "unit_price_cents INTEGER NOT NULL,"
            "unit_cost_cents INTEGER NOT NULL,"
            "reversed_id INTEGER NOT NULL DEFAULT 0,"
            // Whether quantity counts pieces or whole cartons. A line of 3 pieces
            // and a line of 3 cartons are the same number and move the shelf by
            // different amounts, so the unit has to be stored: once it is not, the
            // two cannot be told apart by anything left on the row.
            "unit_kind TEXT NOT NULL DEFAULT 'piece',"
            // Pieces actually off the shelf, counted rather than re-derived from
            // quantity, which no longer says what it is counting.
            "pieces_consumed INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS customers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "phone TEXT NOT NULL DEFAULT '');"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS customer_transactions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "customer_id INTEGER NOT NULL REFERENCES customers(id),"
            "amount_cents INTEGER NOT NULL,"
            // Same meaning as sales.adjustment_cents, and 0 for the same reason.
            "adjustment_cents INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT NOT NULL,"
            "reversed_transaction_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS customer_transaction_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "customer_transaction_id INTEGER NOT NULL REFERENCES customer_transactions(id),"
            "product_id INTEGER NOT NULL REFERENCES products(id),"
            "quantity INTEGER NOT NULL,"
            "unit_price_cents INTEGER NOT NULL,"
            "unit_cost_cents INTEGER NOT NULL,"
            "reversed_id INTEGER NOT NULL DEFAULT 0,"
            // Pieces or cartons, as on a cash sale line. A credit sale moves stock
            // exactly as a cash one does, so a carton line here that could not say
            // how many pieces it took would reconcile the shelf against the till
            // wrongly — the goods leave the shelf either way.
            "unit_kind TEXT NOT NULL DEFAULT 'piece',"
            "pieces_consumed INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS suppliers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "phone TEXT NOT NULL DEFAULT '',"
            "address TEXT NOT NULL DEFAULT '',"
            "notes TEXT NOT NULL DEFAULT '',"
            "opening_balance_cents INTEGER NOT NULL DEFAULT 0,"
            "active INTEGER NOT NULL DEFAULT 1);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS payments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "customer_id INTEGER NOT NULL REFERENCES customers(id),"
            "amount_cents INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "note TEXT NOT NULL DEFAULT '',"
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS expenses ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT NOT NULL,"
            "label TEXT NOT NULL,"
            "amount_cents INTEGER NOT NULL,"
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS owner_drawings ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT NOT NULL,"
            "amount_cents INTEGER NOT NULL,"
            "note TEXT NOT NULL DEFAULT '',"
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS stock_movements ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_id INTEGER NOT NULL REFERENCES products(id),"
            "delta INTEGER NOT NULL,"
            "reason TEXT NOT NULL,"
            "reference TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL,"
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS cash_sessions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "opened_at TEXT NOT NULL,"
            "opening_float_cents INTEGER NOT NULL,"
            "closed_at TEXT NULL,"
            "closing_counted_cents INTEGER NULL,"
            "expected_cents INTEGER NULL,"
            "variance_cents INTEGER NULL,"
            "status TEXT NOT NULL DEFAULT 'open');"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS cash_movements ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "session_id INTEGER NOT NULL REFERENCES cash_sessions(id),"
            "type TEXT NOT NULL,"
            "amount_cents INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "note TEXT NOT NULL DEFAULT '',"
            "ref_type TEXT,"
            "ref_id INTEGER);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "role TEXT NOT NULL,"
            "pin TEXT NOT NULL DEFAULT '',"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL DEFAULT (datetime('now')));"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS admin_secrets ("
            "user_id INTEGER PRIMARY KEY REFERENCES users(id),"
            "master_hash TEXT NOT NULL,"
            "master_salt TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS devices ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT UNIQUE NOT NULL,"
            "auth_token TEXT NOT NULL,"
            "paired_at TEXT NOT NULL,"
            "last_seen_at TEXT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS audit_log ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "actor TEXT NOT NULL,"
            "action TEXT NOT NULL,"
            "target TEXT NOT NULL,"
            "created_at TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "key TEXT UNIQUE NOT NULL,"
            "value TEXT NOT NULL);"),

        // One row per zakat year. year is UNIQUE, so the year itself is what
        // makes a second write for the same year a no-op rather than a duplicate:
        // the history is a ledger, and re-recording a year that is already there
        // must never silently overwrite what it was recorded as.
        //
        // paid is kept beside paid_cents rather than inferred from it being
        // non-null, so "this year owes nothing yet" and "this year was settled
        // at zero" stay two different facts.
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS zakat_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "year INTEGER NOT NULL UNIQUE,"
            "nisab_cents INTEGER NOT NULL,"
            "base_cents INTEGER NOT NULL,"
            "due_cents INTEGER NOT NULL,"
            "gold_price_cents INTEGER NOT NULL,"
            "paid INTEGER NOT NULL DEFAULT 0,"
            "paid_cents INTEGER,"
            "paid_at TEXT);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sync_outbox ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "op_json TEXT NOT NULL,"
            "status TEXT NOT NULL DEFAULT 'pending',"
            "attempts INTEGER NOT NULL DEFAULT 0,"
            "last_error TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS applied_ops ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "device_id TEXT NOT NULL,"
            "op_id INTEGER NOT NULL,"
            "op_type INTEGER NOT NULL,"
            "entity_id INTEGER NOT NULL,"
            "total_cents INTEGER NOT NULL,"
            "cogs_cents INTEGER NOT NULL DEFAULT 0,"
            "applied_at TEXT NOT NULL,"
            "UNIQUE(device_id, op_id));"),

        // Single-row durable per-device sequence used to mint opIds whose values
        // are never reused, even after days of power cuts or outbox clearing.
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sync_sequence ("
            "id INTEGER PRIMARY KEY CHECK (id = 1),"
            "value INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS purchases ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "supplier_id INTEGER NOT NULL,"
            "invoice_number TEXT NOT NULL DEFAULT '',"
            "purchased_at TEXT NOT NULL,"
            "subtotal_cents INTEGER NOT NULL,"
            "vat_cents INTEGER NOT NULL DEFAULT 0,"
            "total_cents INTEGER NOT NULL,"
            "paid_cents INTEGER NOT NULL DEFAULT 0,"
            // How the paid part was settled. Cash means a cash_movements row was
            // written; Credit/Bank settled the balance without touching the drawer.
            "method TEXT NOT NULL DEFAULT 'cash',"
            "add_to_stock INTEGER NOT NULL DEFAULT 1,"
            "note TEXT NOT NULL DEFAULT '',"
            "occasion_id INTEGER,"
            "created_at TEXT NOT NULL,"
            // Links a void to the original purchase. 0 means "not a void".
            "reversed_id INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY (supplier_id) REFERENCES suppliers(id));"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS purchase_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "purchase_id INTEGER NOT NULL,"
            "product_id INTEGER,"
            "description TEXT NOT NULL DEFAULT '',"
            "quantity INTEGER NOT NULL,"
            "unit TEXT NOT NULL DEFAULT 'piece',"
            "unit_price_cents INTEGER NOT NULL,"
            "total_cents INTEGER NOT NULL,"
            // Cartons come in as well as go out. Named apart from pieces_consumed
            // on purpose: goods arrive and goods leave, and one column would have
            // to mean whichever the row happened to be. This is only ever read off
            // a purchase.
            "unit_kind TEXT NOT NULL DEFAULT 'piece',"
            "pieces_received INTEGER NOT NULL DEFAULT 0,"
            // Links a void item to the original. 0 means "not a void item".
            "reversed_id INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY (purchase_id) REFERENCES purchases(id),"
            "FOREIGN KEY (product_id) REFERENCES products(id));"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS supplier_payments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "supplier_id INTEGER NOT NULL,"
            "purchase_id INTEGER,"
            "amount_cents INTEGER NOT NULL,"
            "paid_at TEXT NOT NULL,"
            // How the money left the shop: 'cash' came out of the drawer and has
            // a cash_movements row behind it, 'credit' was settled against the
            // account and 'bank' went through the bank. Both of the last two
            // settle the supplier balance without touching the till, which is
            // what kept them from being told apart before.
            "method TEXT NOT NULL DEFAULT 'cash',"
            // Links a reversal to the original payment. 0 means "not a reversal".
            // The partial unique index (migration 3) guarantees at most one
            // reversal per original.
            "reversed_id INTEGER NOT NULL DEFAULT 0,"
            "is_purchase_initial_payment INTEGER NOT NULL DEFAULT 0,"
            "note TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY (supplier_id) REFERENCES suppliers(id),"
            "FOREIGN KEY (purchase_id) REFERENCES purchases(id));"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS supplier_returns ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "supplier_id INTEGER NOT NULL,"
            "purchase_id INTEGER,"
            "amount_cents INTEGER NOT NULL,"
            "returned_at TEXT NOT NULL,"
            "remove_from_stock INTEGER NOT NULL DEFAULT 1,"
            "note TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY (supplier_id) REFERENCES suppliers(id),"
            "FOREIGN KEY (purchase_id) REFERENCES purchases(id));"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS supplier_return_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "return_id INTEGER NOT NULL,"
            "product_id INTEGER,"
            "quantity INTEGER NOT NULL,"
            "unit_price_cents INTEGER NOT NULL,"
            "total_cents INTEGER NOT NULL,"
            // Counted the same terms as a sale line, because a return is stock
            // going back the other way and reuses pieces_consumed rather than
            // introducing a third name for the same count.
            "unit_kind TEXT NOT NULL DEFAULT 'piece',"
            "pieces_consumed INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY (return_id) REFERENCES supplier_returns(id),"
            "FOREIGN KEY (product_id) REFERENCES products(id));"),

        QStringLiteral(
            "INSERT OR IGNORE INTO sync_sequence (id, value) VALUES (1, 0);"),

        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS trg_stock_after_insert "
            "AFTER INSERT ON stock_movements "
            "BEGIN "
            "  UPDATE products SET quantity = quantity + NEW.delta WHERE id = NEW.product_id; "
            "END;"),

        QStringLiteral("DROP TRIGGER IF EXISTS trg_stock_before_insert_guard;"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sale_items_sale_id ON sale_items(sale_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_customer_transactions_customer_id ON customer_transactions(customer_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_stock_movements_product_id ON stock_movements(product_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_cash_movements_session_id ON cash_movements(session_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_applied_ops_device_op ON applied_ops(device_id, op_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_purchases_supplier "
            "ON purchases(supplier_id, purchased_at DESC);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_purchase_items_purchase "
            "ON purchase_items(purchase_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_supplier_payments_supplier "
            "ON supplier_payments(supplier_id, paid_at DESC);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_supplier_payments_purchase "
            "ON supplier_payments(purchase_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_supplier_returns_supplier "
            "ON supplier_returns(supplier_id, returned_at DESC);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_supplier_return_items_return "
            "ON supplier_return_items(return_id);"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_occasions_active "
            "ON occasions(active, starts_at);"),
    };

    if (!execStatements(schema, QStringLiteral("schema"))) {
        throw std::runtime_error(
            QStringLiteral("Failed to create schema: %1").arg(m_lastError).toStdString());
    }

    createSingleOpenSessionIndex();

    // purchases + purchase_items indexes are created via schema above.

    migrateZakatSettingsIntoSettings(m_db);
    migrateUsersTable(m_db);
    migrateCustomersTable(m_db);
    migrateSuppliersTable(m_db);
    migrateStockMovementsTable(m_db);
    migrateSalesTable(m_db);
    const QString productsError = migrateProductsTable(m_db);
    if (!productsError.isEmpty()) {
        m_lastError = QStringLiteral("products migration failed: %1").arg(productsError);
    }
}

// A partial unique index, so the "one open session at a time" rule is enforced
// by the database itself and not only by CashSessionRepository::open().
//
// This is deliberately kept out of the schema statement list above: that list
// throws on the first failing statement, and a database copied from an older
// build can already hold two open rows, in which case the index cannot be
// created at all. Such a database has to keep working, so a failure here is
// reported and the app runs on without the index.
void Database::createSingleOpenSessionIndex()
{
    const QString createSql =
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS %1 ON cash_sessions(status) WHERE status = 'open'")
            .arg(QLatin1StringView(kSingleOpenSessionIndex));

    QSqlQuery create(m_db);
    const bool created = create.exec(createSql);

    // Read the index list back rather than trusting the statement: a failure
    // above has to end up as a warning, not as a silently unprotected database.
    QSqlQuery indexes(m_db);
    if (!indexes.exec(QStringLiteral("PRAGMA index_list(cash_sessions)"))) {
        qWarning() << "could not read the index list of cash_sessions:" << indexes.lastError().text();
        return;
    }
    bool present = false;
    while (indexes.next()) {
        if (indexes.value(1).toString() == QLatin1StringView(kSingleOpenSessionIndex)) {
            present = true;
            break;
        }
    }
    if (present) {
        return;
    }
    qWarning() << "index" << kSingleOpenSessionIndex << "is missing"
               << (created ? QStringLiteral("even though it was just created")
                           : QStringLiteral("because it could not be created"))
               << "-this database holds more than one open cash session, so opening"
               << "a new session is only prevented by the repository, not by the database."
               << "Close the extra open sessions to restore the index.";
}

bool Database::execStatements(const QStringList& statements, const QString& source)
{
    // Runs from applyPragmas(), createSchema() and the index creation, all of
    // which are starts of operations rather than continuations, so the reason
    // from before them is dropped first. A schema step that fails sets its own
    // message below, which is what the caller is meant to read.
    clearError();
    for (const QString& statement : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(statement)) {
            m_lastError = QStringLiteral("%1 statement failed: %2 (%3)")
                              .arg(source, statement, query.lastError().text());
            return false;
        }
    }
    return true;
}

bool Database::verifyStockConsistency() const
{
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT COUNT(*) FROM ("
                       "  SELECT p.id, p.quantity, COALESCE(SUM(m.delta), 0) AS total "
                       "  FROM products p LEFT JOIN stock_movements m ON m.product_id = p.id "
                       "  GROUP BY p.id HAVING p.quantity != total)"));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        return false;
    }
    return query.value(0).toLongLong() == 0;
}

void Database::recomputeStockQuantities()
{
    QSqlQuery update(m_db);
    update.prepare(
        QStringLiteral("UPDATE products SET quantity = "
                       "(SELECT COALESCE(SUM(delta), 0) FROM stock_movements WHERE product_id = products.id)"));
    update.exec();

    QSqlQuery reset(m_db);
    reset.prepare(
        QStringLiteral("UPDATE products SET quantity = 0 WHERE "
                       "NOT EXISTS (SELECT 1 FROM stock_movements WHERE product_id = products.id)"));
    reset.exec();
}

} // namespace app::data