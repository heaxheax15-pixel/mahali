#pragma once

#include <QWidget>

#include "data/database.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace app::ui {

// Shop settings (الإعدادات): name, currency symbol used by formatMoney, zakat
// on/off, and the sync HMAC key. Persisted through SettingRepository and
// ZakatSettingRepository; currency is applied live to rendered money.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();

    QString shopName() const;
    QString currencySymbol() const;
    bool zakatEnabled() const;
    QString syncKey() const;
    QString noticeText() const;

    void setShopName(const QString& name);
    void setCurrencySymbol(const QString& symbol);
    void setZakatEnabled(bool enabled);
    void setSyncKey(const QString& key);

public slots:
    void save();

private:
    app::data::Database& m_db;
    QLineEdit* m_shopName;
    QLineEdit* m_currency;
    QCheckBox* m_zakat;
    QLineEdit* m_syncKey;
    QComboBox* m_theme;
    QComboBox* m_language;
    QPushButton* m_save;
    QLabel* m_preview;
    QLabel* m_notice;
};

} // namespace app::ui