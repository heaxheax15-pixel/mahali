#include "database.h"

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace app::data {

namespace {

QString uniqueConnectionName()
{
    return QStringLiteral("mahali_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

} // namespace

Database::Database(const QString& filePath)
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
        m_lastError = m_db.lastError().text();
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

void Database::applyPragmas()
{
    execStatements(QStringList{
                       QStringLiteral("PRAGMA journal_mode = WAL;"),
                       // FULL: fsync every committed transaction so a sudden power
                       // cut cannot lose committed money/stock even after days offline.
                       QStringLiteral("PRAGMA synchronous = FULL;"),
                       QStringLiteral("PRAGMA foreign_keys = ON;"),
                       QStringLiteral("PRAGMA busy_timeout = 5000;"),
                   },
                   QStringLiteral("pragma"));
}

void Database::createSchema()
{
    const QStringList schema = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS products ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "barcode TEXT UNIQUE NOT NULL,"
            "name TEXT NOT NULL,"
            "cost_price_cents INTEGER NOT NULL DEFAULT 0,"
            "sale_price_cents INTEGER NOT NULL DEFAULT 0,"
            "quantity INTEGER NOT NULL DEFAULT 0,"
            "unit TEXT NOT NULL DEFAULT '',"
            "package_size INTEGER NOT NULL DEFAULT 1,"
            "active INTEGER NOT NULL DEFAULT 1);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sales ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT NOT NULL,"
            "total_cents INTEGER NOT NULL,"
            "device_id TEXT NOT NULL,"
            "oversold INTEGER NOT NULL DEFAULT 0,"
            "reversed_sale_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sale_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sale_id INTEGER NOT NULL REFERENCES sales(id),"
            "product_id INTEGER NOT NULL REFERENCES products(id),"
            "quantity INTEGER NOT NULL,"
            "unit_price_cents INTEGER NOT NULL,"
            "unit_cost_cents INTEGER NOT NULL,"
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

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
            "reversed_id INTEGER NOT NULL DEFAULT 0);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS suppliers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS supplier_transactions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "supplier_id INTEGER NOT NULL REFERENCES suppliers(id),"
            "amount_cents INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "note TEXT NOT NULL DEFAULT '');"),

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
            "note TEXT NOT NULL DEFAULT '');"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "role TEXT NOT NULL);"),

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
            "CREATE TABLE IF NOT EXISTS zakat_settings ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "key TEXT UNIQUE NOT NULL,"
            "value TEXT NOT NULL);"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "key TEXT UNIQUE NOT NULL,"
            "value TEXT NOT NULL);"),

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
            "INSERT OR IGNORE INTO sync_sequence (id, value) VALUES (1, 0);"),

        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS trg_stock_after_insert "
            "AFTER INSERT ON stock_movements "
            "BEGIN "
            "  UPDATE products SET quantity = quantity + NEW.delta WHERE id = NEW.product_id; "
            "END;"),

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
    };

    if (!execStatements(schema, QStringLiteral("schema"))) {
        throw std::runtime_error(
            QStringLiteral("Failed to create schema: %1").arg(m_lastError).toStdString());
    }
}

bool Database::execStatements(const QStringList& statements, const QString& source)
{
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