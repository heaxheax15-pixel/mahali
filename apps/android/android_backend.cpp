#include "android_backend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlQuery>
#include <QUrl>

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

void AndroidBackend::syncProducts(const QString& serverUrl, const QString& key)
{
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QUrl url(serverUrl + QStringLiteral("/api/products"));
    QNetworkRequest request(url);
    request.setRawHeader("X-Mahali-Key", key.toUtf8());

    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        reply->deleteLater();
        manager->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit syncStatusChanged(QStringLiteral("Sync failed: ") + reply->errorString());
            return;
        }

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            emit syncStatusChanged(QStringLiteral("Sync failed: HTTP %1")
                                       .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray()) {
            emit syncStatusChanged(QStringLiteral("Sync failed: invalid response"));
            return;
        }

        const QJsonArray arr = doc.array();
        if (!m_db.beginTransaction()) {
            emit syncStatusChanged(QStringLiteral("Sync failed: could not start transaction"));
            return;
        }

        QSqlQuery clear(m_db.handle());
        if (!clear.exec(QStringLiteral("DELETE FROM products"))) {
            m_db.rollback();
            emit syncStatusChanged(QStringLiteral("Sync failed: clear products"));
            return;
        }

        QSqlQuery insert(m_db.handle());
        insert.prepare(QStringLiteral("INSERT INTO products (id, barcode, name, sale_price_cents, cost_price_cents, quantity, unit, sold_by_weight, pieces_per_package, package_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        for (const QJsonValue& v : arr) {
            const QJsonObject obj = v.toObject();
            insert.addBindValue(obj.value(QStringLiteral("id")).toInt());
            insert.addBindValue(obj.value(QStringLiteral("barcode")).toString());
            insert.addBindValue(obj.value(QStringLiteral("name")).toString());
            insert.addBindValue(obj.value(QStringLiteral("salePriceCents")).toVariant().toLongLong());
            insert.addBindValue(obj.value(QStringLiteral("costPriceCents")).toVariant().toLongLong());
            insert.addBindValue(obj.value(QStringLiteral("quantity")).toVariant().toLongLong());
            insert.addBindValue(obj.value(QStringLiteral("unit")).toString());
            insert.addBindValue(obj.value(QStringLiteral("soldByWeight")).toBool() ? 1 : 0);
            insert.addBindValue(obj.value(QStringLiteral("piecesPerPackage")).toInt());
            insert.addBindValue(obj.value(QStringLiteral("packageName")).toString());
            if (!insert.exec()) {
                m_db.rollback();
                emit syncStatusChanged(QStringLiteral("Sync failed: insert product"));
                return;
            }
        }

        if (!m_db.commit()) {
            emit syncStatusChanged(QStringLiteral("Sync failed: commit"));
            return;
        }

        refresh();
        emit syncStatusChanged(QStringLiteral("Sync successful: %1 products.").arg(arr.size()));
    });
}
