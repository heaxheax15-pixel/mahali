#include "settings_page.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "data/setting_repository.h"
#include "data/zakat_setting_repository.h"
#include "format_utils.h"
#include "theme.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

SettingsPage::SettingsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_shopName = new QLineEdit;
    m_currency = new QLineEdit;
    m_currency->setPlaceholderText(QStringLiteral("مثال: دج  أو  DA"));
    m_zakat = new QCheckBox(QStringLiteral("احتساب الزكاة (2.5%) في التقارير"));
    m_syncKey = new QLineEdit;
    m_syncKey->setEchoMode(QLineEdit::Password);

    m_theme = new QComboBox;
    m_theme->addItem(QStringLiteral("فاتح"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("داكن"), QStringLiteral("dark"));

    m_preview = new QLabel;
    m_preview->setWordWrap(true);
    m_preview->setObjectName(QStringLiteral("faintText"));

    m_save = new QPushButton(QStringLiteral("حفظ الإعدادات"));
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 18));

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("noticeOk"));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->addRow(QStringLiteral("اسم المتجر:"), m_shopName);
    form->addRow(QStringLiteral("رمز العملة:"), m_currency);
    form->addRow(QString(), m_zakat);
    form->addRow(QStringLiteral("السمة:"), m_theme);
    form->addRow(QStringLiteral("مفتاح المزامنة (يتطلب إعادة تشغيل):"), m_syncKey);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 16);
    cardLayout->setSpacing(12);
    cardLayout->addLayout(form);
    cardLayout->addWidget(m_preview);
    cardLayout->addWidget(m_save);
    cardLayout->addWidget(m_notice);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(QStringLiteral("الإعدادات"),
                                   QStringLiteral("اسم المتجر، العملة، السمة ومفتاح المزامنة")));
    root->addWidget(card);
    root->addStretch(1);

    connect(m_currency, &QLineEdit::textChanged, m_preview,
            [this](const QString& symbol) {
                m_preview->setText(QStringLiteral("معاينة: %1").arg(formatMoney(12345)));
                Q_UNUSED(symbol);
            });
    connect(m_theme, &QComboBox::currentIndexChanged, this,
            [this]() {
                applyTheme(m_theme->currentData().toString(), *qApp);
                m_notice->setText(QStringLiteral("طُبّقت السمة الجديدة — احفظ للإبقاء عليها"));
            });

    connect(m_save, &QPushButton::clicked, this, &SettingsPage::save);

    refresh();
}

void SettingsPage::refresh()
{
    data::SettingRepository settings(m_db);
    m_shopName->setText(settings.value(QStringLiteral("shop_name")).value_or(QString()));
    m_currency->setText(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));
    m_syncKey->setText(settings.value(QStringLiteral("sync_hmac_key")).value_or(QStringLiteral("mahali-local-key")));
    const QString theme = settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light"));
    const int idx = m_theme->findData(theme);
    m_theme->setCurrentIndex(idx >= 0 ? idx : m_theme->findData(QStringLiteral("light")));

    data::ZakatSettingRepository zakat(m_db);
    const auto enabledRow = zakat.findByKey(QStringLiteral("enabled"));
    m_zakat->setChecked(!enabledRow.has_value() || enabledRow->value == QLatin1String("1"));

    m_notice->clear();
    m_preview->setText(QStringLiteral("معاينة: %1").arg(formatMoney(12345)));
}

QString SettingsPage::shopName() const
{
    return m_shopName->text().trimmed();
}

QString SettingsPage::currencySymbol() const
{
    return m_currency->text().trimmed();
}

bool SettingsPage::zakatEnabled() const
{
    return m_zakat->isChecked();
}

QString SettingsPage::syncKey() const
{
    return m_syncKey->text().trimmed();
}

QString SettingsPage::noticeText() const
{
    return m_notice->text();
}

void SettingsPage::setShopName(const QString& name)
{
    m_shopName->setText(name);
}

void SettingsPage::setCurrencySymbol(const QString& symbol)
{
    m_currency->setText(symbol);
}

void SettingsPage::setZakatEnabled(bool enabled)
{
    m_zakat->setChecked(enabled);
}

void SettingsPage::setSyncKey(const QString& key)
{
    m_syncKey->setText(key);
}

void SettingsPage::save()
{
    data::SettingRepository settings(m_db);
    settings.set(QStringLiteral("shop_name"), shopName());
    settings.set(QStringLiteral("currency_symbol"), currencySymbol());
    settings.set(QStringLiteral("theme"), m_theme->currentData().toString());
    if (!syncKey().isEmpty()) {
        settings.set(QStringLiteral("sync_hmac_key"), syncKey());
    }
    data::ZakatSettingRepository zakat(m_db);
    zakat.set(QStringLiteral("enabled"), zakatEnabled() ? QStringLiteral("1") : QStringLiteral("0"));

    app::ui::setCurrencySymbol(currencySymbol());
    m_preview->setText(QStringLiteral("معاينة: %1").arg(formatMoney(12345)));
    m_notice->setText(QStringLiteral("حُفظت الإعدادات"));
}

} // namespace app::ui