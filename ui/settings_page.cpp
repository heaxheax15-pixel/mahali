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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "core/i18n.h"
#include "core/update_checker.h"
#include "data/setting_repository.h"
#include "data/zakat_setting_repository.h"
#include "format_utils.h"
#include "theme.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// The settings column, and with it every field inside it. A settings form is a
// fixed set of known fields, so its width is a design decision rather than
// something to derive from the window: left to grow it became 960px wide and
// pushed the field away from its own label.
constexpr int kFormWidth = 500;

// The labels are captions now, sitting above the field they name, and a caption
// does not end in a colon. It is taken off the translated text rather than off
// the source string because the source is the key the French catalogue is
// matched on: editing it would leave the French labels falling back to Arabic
// until the catalogue was rebuilt.
QString caption(const QString& text)
{
    QString out = text.trimmed();
    if (out.endsWith(QLatin1Char(':'))) {
        out.chop(1);
    }
    return out.trimmed();
}

} // namespace

SettingsPage::SettingsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_shopName = new QLineEdit;
    m_currency = new QLineEdit;
    m_currency->setPlaceholderText(tr("مثال: دج  أو  DA"));
    m_zakat = new QCheckBox(tr("احتساب الزكاة (2.5%) في التقارير"));
    m_syncKey = new QLineEdit;
    m_syncKey->setEchoMode(QLineEdit::Password);

    m_theme = new QComboBox;
    m_theme->addItem(tr("فاتح"), QStringLiteral("light"));
    m_theme->addItem(tr("داكن"), QStringLiteral("dark"));

    m_language = new QComboBox;
    m_language->setObjectName(QStringLiteral("languageCombo"));
    m_language->addItem(tr("العربية"), QStringLiteral("ar"));
    m_language->addItem(tr("Français"), QStringLiteral("fr"));
    m_language->addItem(tr("English"), QStringLiteral("en"));

    // A combo box asks for Preferred, so under an expanding field policy it kept
    // its size hint — 129px next to 466px line edits, which is the ragged column
    // this form is meant to replace. Both are marked Expanding so the two
    // dropdowns measure the same as every other field.
    for (QComboBox* combo : {m_theme, m_language}) {
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    m_preview = new QLabel;
    m_preview->setWordWrap(true);
    m_preview->setObjectName(QStringLiteral("faintText"));

    m_save = new QPushButton(tr("حفظ الإعدادات"));
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 18));

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("noticeOk"));
    m_notice->setVisible(false);

    // The same shape the login dialog uses: WrapAllRows puts each label on its
    // own line above its field. Side by side they fought each other — an Arabic
    // label is short and a French one long, so a shared label column left a gap
    // in the middle of the card that neither language could use, and the zakat
    // checkbox had no label of its own to sit under, which left it adrift in
    // the field column.
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(14);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->addRow(caption(tr("اسم المتجر:")), m_shopName);
    form->addRow(caption(tr("رمز العملة:")), m_currency);
    // The checkbox carries its own text and spans the row, so it reads as a
    // sentence in its own right rather than as a box with a gap beside it.
    form->addRow(m_zakat);
    form->addRow(caption(tr("السمة:")), m_theme);
    form->addRow(caption(tr("اللغة:")), m_language);
    form->addRow(caption(tr("مفتاح المزامنة (يتطلب إعادة تشغيل):")), m_syncKey);

    // The fields are bounded by the card; the wrapper is the cap on the form
    // itself, so the fields cannot outgrow the column if the card is ever
    // resized to something wider.
    auto* formBox = new QWidget;
    formBox->setMaximumWidth(kFormWidth);
    auto* formBoxLayout = new QVBoxLayout(formBox);
    formBoxLayout->setContentsMargins(0, 0, 0, 0);
    formBoxLayout->addLayout(form);

    m_checkUpdates = new QPushButton(tr("Vérifier les mises à jour"));
    m_checkUpdates->setObjectName(QStringLiteral("linkButton"));
    m_checkUpdates->setCursor(Qt::PointingHandCursor);

    auto* card = makeCard();
    // Fixed rather than merely capped: a maximum alone leaves the card at its
    // size hint, which measured 140px in Arabic and 291px in French for the same
    // screen — two different columns depending on the language.
    card->setFixedWidth(kFormWidth);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 16);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(formBox);
    cardLayout->addWidget(m_preview);
    cardLayout->addSpacing(21);
    cardLayout->addWidget(m_save);
    cardLayout->addWidget(m_checkUpdates);
    cardLayout->addWidget(m_notice);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("الإعدادات"),
                                   tr("اسم المتجر، العملة، السمة ومفتاح المزامنة")));

    // Six stacked rows do not fit a short page: the card measures 863px against
    // the 748px the window gives it, and the save button and the update link sit
    // at the bottom of it, so left alone they were simply off the page. The card
    // scrolls instead. At a window tall enough to hold the form there is nothing
    // to scroll and no scrollbar appears.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // A scroll area centres a widget narrower than its viewport, and AlignLeft
    // is mirrored by the layout direction, so it lands on the reading-start
    // edge: the right in the Arabic UI, the left in the French one.
    scroll->setAlignment(Qt::AlignLeft);
    // The theme hands the sidebar's scroll area a transparent surface by name
    // and this one is not in that rule, so without a background of its own it
    // would paint the window colour over the page.
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget"
                                         " { background: transparent; border: none; }"));
    scroll->setWidget(card);
    root->addWidget(scroll, 1);

    connect(m_currency, &QLineEdit::textChanged, m_preview,
            [this](const QString& symbol) {
                m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
                Q_UNUSED(symbol);
            });
    connect(m_theme, &QComboBox::currentIndexChanged, this,
            [this]() {
                applyTheme(m_theme->currentData().toString(), *qApp);
                // Announced before the notice, so the shell's icons are already
                // the new colour by the time the page says the theme changed.
                emit themeChanged();
                m_notice->setText(tr("طُبّقت السمة الجديدة — احفظ للإبقاء عليها"));
                m_notice->setVisible(!m_notice->text().isEmpty());
            });

    // Phase A1: the choice is stored and the catalogue is swapped in, but the
    // already-built widgets keep their source strings until the restart prompt
    // is honoured (full in-place retranslate lands in A3).
    connect(m_language, &QComboBox::currentIndexChanged, this,
            [this]() {
                const QString code = m_language->currentData().toString();
                data::SettingRepository settings(m_db);
                settings.set(QStringLiteral("language"), code);
                core::applyLanguage(code);
                QMessageBox::information(
                    this, tr("اللغة"),
                    tr("أعد تشغيل البرنامج لتطبيق اللغة بالكامل"));
            });

    connect(m_save, &QPushButton::clicked, this, &SettingsPage::save);
    connect(m_checkUpdates, &QPushButton::clicked, this, &SettingsPage::checkForUpdates);

    refresh();
}

// A manual check, unlike the startup one, reports its result: the user asked,
// so an answer — even a failure — is owed. The checker is parented to the page
// and simply dies with it once the answer is in.
void SettingsPage::checkForUpdates()
{
    m_checkUpdates->setEnabled(false);
    m_checkUpdates->setText(tr("جارٍ التحقق..."));

    auto* checker = new core::UpdateChecker(this);
    connect(checker, &core::UpdateChecker::updateAvailable, this,
            [this, checker](const QString& tag, const QString& notes) {
                Q_UNUSED(notes);
                QMessageBox::information(
                    this, tr("تحديث متاح"),
                    tr("الإصدار %1 متاح. افتح Mahali من جديد لاحقًا لتثبيته.").arg(tag));
                finishUpdateCheck(checker);
            });
    connect(checker, &core::UpdateChecker::upToDate, this, [this, checker]() {
        QMessageBox::information(this, tr("التحديث"), tr("أنت تستخدم أحدث إصدار."));
        finishUpdateCheck(checker);
    });
    connect(checker, &core::UpdateChecker::checkFailed, this, [this, checker](const QString& reason) {
        Q_UNUSED(reason);
        QMessageBox::warning(this, tr("التحديث"), tr("تعذّر الاتصال. تحقق من الإنترنت."));
        finishUpdateCheck(checker);
    });

    checker->check();
}

void SettingsPage::finishUpdateCheck(core::UpdateChecker* checker)
{
    m_checkUpdates->setEnabled(true);
    m_checkUpdates->setText(tr("Vérifier les mises à jour"));
    checker->deleteLater();
}

void SettingsPage::refresh()
{
    data::SettingRepository settings(m_db);
    m_shopName->setText(settings.value(QStringLiteral("shop_name")).value_or(QString()));
    m_currency->setText(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));
    m_syncKey->setText(settings.value(QStringLiteral("sync_hmac_key")).value_or(QStringLiteral("mahali-local-key")));
    // Blocked for the same reason as the language below: restoring the stored
    // value is not a change the operator made, and letting the handler run would
    // re-apply the theme and re-announce it every time the page is opened.
    const QString theme = settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light"));
    const int idx = m_theme->findData(theme);
    const QSignalBlocker themeBlocker(m_theme);
    m_theme->setCurrentIndex(idx >= 0 ? idx : m_theme->findData(QStringLiteral("light")));

    // Blocked so restoring the stored value does not fire the change handler
    // (which would pop the restart dialog every time the page is opened).
    const QString language =
        settings.value(QStringLiteral("language")).value_or(core::defaultLanguage());
    const int langIdx = m_language->findData(language);
    const QSignalBlocker blocker(m_language);
    m_language->setCurrentIndex(langIdx >= 0 ? langIdx
                                             : m_language->findData(core::defaultLanguage()));

    data::ZakatSettingRepository zakat(m_db);
    const auto enabledRow = zakat.findByKey(QStringLiteral("enabled"));
    m_zakat->setChecked(!enabledRow.has_value() || enabledRow->value == QLatin1String("1"));

    m_notice->clear();
    m_notice->setVisible(false);
    m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
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
    m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
    m_notice->setText(tr("حُفظت الإعدادات"));
    m_notice->setVisible(!m_notice->text().isEmpty());
}

} // namespace app::ui