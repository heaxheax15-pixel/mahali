#pragma once

#include <QObject>
#include <QVariant>

namespace app::data {
class Database;
}

// The entire C++ surface the QML side sees in this phase: the product list.
class AndroidBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList products READ products NOTIFY productsChanged)

public:
    explicit AndroidBackend(app::data::Database& db, QObject* parent = nullptr);

    QVariantList products() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void syncProducts(const QString& serverUrl, const QString& key);

signals:
    void productsChanged();
    void syncStatusChanged(const QString& status);

private:
    app::data::Database& m_db;
    QVariantList m_cache;
};

