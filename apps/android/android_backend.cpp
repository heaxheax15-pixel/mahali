#include "android_backend.h"

#include "data/database.h"
#include "data/product_repository.h"

namespace {

// How many rows refresh() will hand to QML, and why it is a number and not
// "all of them". A shop can hold a thousand products; building a ListView
// delegate for every one of them happens before the first frame is on screen,
// and on a phone that reads as a hang on cold start. 200 rows cover several
// screens of scrolling — more than a first paint ever shows — and the search
// field reaches the rest once the UI is up. Paging can replace the cap later;
// this is the shell.
constexpr int kMaxProducts = 200;

} // namespace

AndroidBackend::AndroidBackend(app::data::Database& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
}

QVariantList AndroidBackend::products() const
{
    return m_cache;
}

void AndroidBackend::refresh()
{
    app::data::ProductRepository repo(m_db);
    const std::vector<app::core::Product> products = repo.findAll();

    QVariantList rows;
    for (const app::core::Product& product : products) {
        if (rows.size() >= kMaxProducts)
            break;

        QVariantMap row;
        row.insert(QStringLiteral("id"), product.id);
        row.insert(QStringLiteral("barcode"), product.barcode);
        row.insert(QStringLiteral("name"), product.name);
        row.insert(QStringLiteral("salePriceCents"),
                   QVariant::fromValue(static_cast<qlonglong>(product.salePriceCents)));
        row.insert(QStringLiteral("quantity"),
                   QVariant::fromValue(static_cast<qlonglong>(product.quantity)));
        row.insert(QStringLiteral("soldByWeight"), product.soldByWeight);
        rows.append(row);
    }

    m_cache = rows;
    emit productsChanged();
}
