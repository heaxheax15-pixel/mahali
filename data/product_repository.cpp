#include "product_repository.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace app::data {

ProductRepository::ProductRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Product productFromQuery(const QSqlQuery& query)
{
    core::Product product;
    product.id = query.value(0).toInt();
    // A SQL NULL comes back as a null QString, which is how "no barcode" is kept
    // distinct from a blank one.
    product.barcode = query.value(1).toString();
    product.name = query.value(2).toString();
    product.costPriceCents = query.value(3).toLongLong();
    product.salePriceCents = query.value(4).toLongLong();
    product.quantity = query.value(5).toLongLong();
    product.unit = query.value(6).toString();
    product.packageSize = query.value(7).toInt();
    product.active = query.value(8).toInt() != 0;
    product.soldByWeight = query.value(9).toInt() != 0;
    return product;
}

const char* kProductColumns =
    "id, barcode, name, cost_price_cents, sale_price_cents, quantity, unit, package_size, active, "
    "sold_by_weight";

const char* kQuickItemsFilter = "active = 1 AND (barcode IS NULL OR TRIM(barcode) = '')";

// Binds a barcode, sending "no barcode" as SQL NULL. The column is UNIQUE, and
// UNIQUE treats every NULL as different from every other, so any number of
// products can lack a barcode. Binding a blank string instead would allow exactly
// one of them, and the second product saved without a barcode would be refused.
// Whitespace counts as blank, which is the same rule the products migration uses
// when it rewrites legacy rows.
QVariant barcodeVariant(const QString& barcode)
{
    if (barcode.trimmed().isEmpty()) {
        return QVariant{};
    }
    return QVariant{barcode};
}

// Escapes the LIKE wildcards so a literal % or _ in the query is not a pattern.
QString likePattern(const QString& query)
{
    QString escaped = query;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('%'), QLatin1String("\\%"));
    escaped.replace(QLatin1Char('_'), QLatin1String("\\_"));
    return QLatin1Char('%') + escaped + QLatin1Char('%');
}

// True when the name holds at least one Unicode letter, which is what separates
// a real product name from a row an import left nameless. \p{L} covers Latin,
// Arabic and every other script, so this is not a test for ASCII.
const QRegularExpression kNamedProductRe(QStringLiteral("[\\p{L}]"));

} // namespace

std::optional<core::Product> ProductRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM products WHERE id = ?").arg(QLatin1StringView(kProductColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return productFromQuery(query);
}

std::optional<core::Product> ProductRepository::findByBarcode(const QString& barcode) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM products WHERE barcode = ?").arg(QLatin1StringView(kProductColumns)));
    query.addBindValue(barcode);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return productFromQuery(query);
}

std::vector<core::Product> ProductRepository::findAll(Visibility visibility) const
{
    std::vector<core::Product> products;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM products ORDER BY name").arg(QLatin1StringView(kProductColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::findAll"));
        return products;
    }
    if (visibility == Visibility::All) {
        while (query.next()) {
            products.push_back(productFromQuery(query));
        }
        return products;
    }
    // The name filter is done here rather than in SQL because the rule is a
    // Unicode property, and SQLite has no way to ask for one: its GLOB and LIKE
    // both work on bytes, so "no letters" would mean "no ASCII a-z and no
    // A-Z", which would hide every Arabic product -- the majority of them.
    // QRegularExpression with \p{L} is the property Qt has always had.
    //
    // It is a match, not a test for emptiness: "---" and "123 456" hold no letter
    // and are dropped, while "Coca 123" holds one and is kept. An empty name is
    // dropped by the same test, since there is no letter in it to match.
    while (query.next()) {
        core::Product product = productFromQuery(query);
        if (!kNamedProductRe.match(product.name).hasMatch()) {
            continue;
        }
        products.push_back(product);
    }
    return products;
}

int ProductRepository::save(const core::Product& product)
{
    QSqlQuery query(m_db.handle());
    if (product.id == 0) {
        query.prepare(
            QStringLiteral("INSERT INTO products "
                           "(barcode, name, cost_price_cents, sale_price_cents, quantity, unit, package_size, active, sold_by_weight) "
                           "VALUES (?, ?, ?, ?, 0, ?, ?, ?, ?)"));
        query.addBindValue(barcodeVariant(product.barcode));
        query.addBindValue(product.name);
        query.addBindValue(product.costPriceCents);
        query.addBindValue(product.salePriceCents);
        query.addBindValue(product.unit);
        query.addBindValue(product.packageSize);
        query.addBindValue(product.active ? 1 : 0);
        query.addBindValue(product.soldByWeight ? 1 : 0);
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::save"));
            return 0;
        }
        return query.lastInsertId().toInt();
    }

    query.prepare(
        QStringLiteral("UPDATE products SET "
                       "barcode = ?, name = ?, cost_price_cents = ?, sale_price_cents = ?, "
                       "unit = ?, package_size = ?, active = ?, sold_by_weight = ? "
                       "WHERE id = ?"));
    query.addBindValue(barcodeVariant(product.barcode));
    query.addBindValue(product.name);
    query.addBindValue(product.costPriceCents);
    query.addBindValue(product.salePriceCents);
    query.addBindValue(product.unit);
    query.addBindValue(product.packageSize);
    query.addBindValue(product.active ? 1 : 0);
    query.addBindValue(product.soldByWeight ? 1 : 0);
    query.addBindValue(product.id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::save"));
        return 0;
    }
    return product.id;
}

std::vector<core::Product> ProductRepository::findQuickItems() const
{
    std::vector<core::Product> products;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM products WHERE %2 ORDER BY name")
                      .arg(QLatin1StringView(kProductColumns), QLatin1StringView(kQuickItemsFilter)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::findQuickItems"));
        return products;
    }
    while (query.next()) {
        products.push_back(productFromQuery(query));
    }
    return products;
}

std::vector<core::Product> ProductRepository::findQuickItemsByName(const QString& nameQuery) const
{
    std::vector<core::Product> products;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM products WHERE %2 AND name LIKE ? ESCAPE '\\' ORDER BY name")
                      .arg(QLatin1StringView(kProductColumns), QLatin1StringView(kQuickItemsFilter)));
    query.addBindValue(likePattern(nameQuery));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::findQuickItemsByName"));
        return products;
    }
    while (query.next()) {
        products.push_back(productFromQuery(query));
    }
    return products;
}

void ProductRepository::adjustStock(int productId, long long delta, const QString& reason)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO stock_movements (product_id, delta, reason, created_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(productId);
    query.addBindValue(delta);
    query.addBindValue(reason);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::adjustStock"));
        return;
    }
}

bool ProductRepository::updateAverageCost(int productId, long long oldQty, long long oldCostCents,
                                          long long addedQty, long long addedCostCents)
{
    const long long newQty = oldQty + addedQty;
    if (newQty <= 0) {
        return true;
    }
    const long long newCost = (oldQty * oldCostCents + addedQty * addedCostCents) / newQty;

    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE products SET cost_price_cents = ? WHERE id = ?"));
    query.addBindValue(newCost);
    query.addBindValue(productId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::updateAverageCost"));
        return false;
    }
    return true;
}

void ProductRepository::setActive(int productId, bool active)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE products SET active = ? WHERE id = ?"));
    query.addBindValue(active ? 1 : 0);
    query.addBindValue(productId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::setActive"));
        return;
    }
}

void ProductRepository::setSoldByWeight(int productId, bool value)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE products SET sold_by_weight = ? WHERE id = ?"));
    query.addBindValue(value ? 1 : 0);
    query.addBindValue(productId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ProductRepository::setSoldByWeight"));
        return;
    }
}

} // namespace app::data