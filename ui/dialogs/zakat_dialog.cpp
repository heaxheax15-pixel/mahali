#include "zakat_dialog.h"

#include <QCoreApplication>
#include <QDate>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "core/zakat_notifier.h"
#include "data/report_service.h"
#include "data/setting_repository.h"
#include "data/zakat_history_repository.h"

#include "format_utils.h"
#include "scan_safe_dialog.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ZakatDialog", text);
}

// The nisab is 85 grams of gold, so a per-gram price in whole dinars becomes a
// nisab in minor units the same way money does everywhere else in the app: the
// typed price is multiplied by 100 first. Computing 85 * 200 and calling that
// dinars would put the threshold at 17,000 DA instead of 17,000 * 100.
constexpr long long kNisabGrams = 85;

// What one percent of the shop's trading goods comes to, in the same minor
// units the base is already held in. 2.5% is 25/1000 rather than a float, so
// the halala are rounded the way ZakatCalculator rounds them and the two
// numbers cannot disagree.
long long zakatOn(long long baseCents)
{
    return baseCents * 25 / 1000;
}

} // namespace

void showZakatDialog(QWidget* parent, app::data::Database& db)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("الزكاة السنوية"));
    dialog.setModal(true);
    dialog.resize(600, 560);

    auto* root = new QVBoxLayout(&dialog);
    padPageLayout(root);

    auto* title = new QLabel(tr("🕌 الوقت السنوي لإخراج الزكاة"));
    QFont titleFont = title->font();
    titleFont.setPixelSize(21);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    root->addWidget(new QLabel(tr("أدخل سعر غرام الذهب اليوم لحساب النصاب.")));

    // The figure the operator types is in dinars, but everything below it is in
    // minor units, so the two are converted once here and never mixed after.
    auto* goldPrice = new QLineEdit;
    goldPrice->setPlaceholderText(QStringLiteral("0.00"));

    auto* nisabLine = new QLabel;
    nisabLine->setObjectName(QStringLiteral("faintText"));
    nisabLine->setWordWrap(true);

    // Stands in for the moment of saving: it sits next to the field it is about,
    // and fades on its own so it cannot be read as part of the nisab figure.
    auto* savedNote = new QLabel;
    savedNote->setObjectName(QStringLiteral("noticeOk"));
    savedNote->setVisible(false);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->addRow(tr("سعر الغرام الواحد (DA/gram)"), goldPrice);
    form->addRow(QString(), nisabLine);
    form->addRow(QString(), savedNote);
    root->addLayout(form);

    // Saving the threshold is a small errand and does not end the conversation,
    // so it stays a secondary button beside the field rather than one of the two
    // decisions at the foot of the dialog.
    auto* saveNisab = new QPushButton(tr("Enregistrer le Nisab"));
    saveNisab->setObjectName(QStringLiteral("secondary"));
    saveNisab->setEnabled(false);
    root->addWidget(saveNisab, 0, Qt::AlignLeft);

    auto* rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    root->addWidget(rule);

    auto* calculate = new QPushButton(tr("حساب الزكاة"));
    calculate->setObjectName(QStringLiteral("secondary"));
    calculate->setEnabled(false);
    root->addWidget(calculate);

    // The breakdown of what the base is made of, filled in when the operator
    // asks for the calculation.
    auto* breakdown = new QFrame;
    breakdown->setObjectName(QStringLiteral("card"));
    auto* breakdownLayout = new QVBoxLayout(breakdown);
    padCardLayout(breakdownLayout);
    breakdownLayout->setSpacing(6);
    breakdown->setVisible(false);
    root->addWidget(breakdown);

    auto* verdict = new QLabel;
    verdict->setWordWrap(true);
    verdict->setVisible(false);
    root->addWidget(verdict);

    root->addStretch(1);

    auto* later = new QPushButton(tr("Plus tard"));
    later->setObjectName(QStringLiteral("ghost"));

    auto* markPaid = new QPushButton(tr("Marquer comme payée cette année"));
    markPaid->setObjectName(QStringLiteral("primary"));
    markPaid->setVisible(false);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addWidget(later);
    footer->addStretch(1);
    footer->addWidget(markPaid);
    root->addLayout(footer);

    // The nisab the dialog works with, in minor units, or -1 while the field
    // holds nothing usable. Held outside the lambdas because the verdict, both
    // buttons and the payment all need it and none may recompute it.
    long long nisabCents = -1;
    long long dueCents = 0;
    long long baseCents = 0;
    // The per-gram price, kept separately from the nisab it produced: 85 x price
    // is the threshold, but only the price itself explains where it came from.
    long long goldPriceCents = 0;

    const auto recomputeNisab = [&]() {
        const std::optional<long long> price = parseMoney(goldPrice->text());
        if (!price.has_value() || *price <= 0) {
            nisabCents = -1;
            goldPriceCents = 0;
            nisabLine->clear();
            savedNote->setVisible(false);
            // A price that cannot be read takes the figures and both buttons
            // with it: leaving a stale "you owe this" on screen under a field the
            // operator has just emptied is how the wrong figure gets saved.
            breakdown->setVisible(false);
            verdict->setVisible(false);
            markPaid->setVisible(false);
            saveNisab->setEnabled(false);
            calculate->setEnabled(false);
            return;
        }
        nisabCents = *price * kNisabGrams;
        goldPriceCents = *price;
        nisabLine->setText(tr("النصاب = %1").arg(formatMoney(nisabCents)));
        saveNisab->setEnabled(true);
        calculate->setEnabled(true);
    };

    QObject::connect(goldPrice, &QLineEdit::textChanged, &dialog,
                     [&recomputeNisab](const QString&) { recomputeNisab(); });

    QObject::connect(saveNisab, &QPushButton::clicked, &dialog, [&]() {
        if (nisabCents < 0) {
            return;
        }
        // The threshold and nothing else. Recording a payment is a separate
        // decision with its own button, so typing a gold price can never be
        // mistaken for having paid.
        data::SettingRepository settings(db);
        settings.set(QStringLiteral("nisab_cents"), QString::number(nisabCents));

        savedNote->setText(tr("Nisab enregistré."));
        savedNote->setVisible(true);
        // Left up, it would still be sitting there minutes later next to a
        // nisab the operator has since changed.
        QTimer::singleShot(3000, &dialog, [savedNote]() { savedNote->setVisible(false); });
    });

    QObject::connect(calculate, &QPushButton::clicked, &dialog, [&]() {
        recomputeNisab();
        if (nisabCents < 0) {
            return;
        }

        // A full year to now: the base itself ignores the period, so this only
        // has to be a range that reads as the trading year rather than a day.
        const QDateTime from(QDate::currentDate().addYears(-1), QTime(0, 0));
        const data::StoreReport report =
            data::ReportService(db).build(from, QDateTime::currentDateTime());

        const auto row = [](const QString& label, long long cents) {
            auto* line = new QLabel(QStringLiteral("%1: %2").arg(label, formatMoney(cents)));
            line->setTextInteractionFlags(Qt::TextSelectableByMouse);
            return line;
        };
        // Cleared first: a second calculation must not stack a second set of rows
        // under the first, which is what a plain addWidget would do. Deleted
        // outright rather than deleteLater, because deleteLater only queues the
        // teardown and these labels would still be children of the card, counted
        // and hit-tested, until the event loop came round to it.
        while (QLayoutItem* stale = breakdownLayout->takeAt(0)) {
            if (QWidget* w = stale->widget()) {
                delete w;
            }
            delete stale;
        }
        breakdownLayout->addWidget(row(QStringLiteral("Valeur du stock"), report.stockValueCents));
        breakdownLayout->addWidget(row(QStringLiteral("Trésorerie"), report.cashOnHandCents));
        breakdownLayout->addWidget(row(QStringLiteral("Créances clients"), report.receivablesCents));

        auto* separator = new QFrame;
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        breakdownLayout->addWidget(separator);

        auto* total = row(QStringLiteral("Total imposable"), report.zakatBaseCents);
        QFont totalFont = total->font();
        totalFont.setBold(true);
        total->setFont(totalFont);
        breakdownLayout->addWidget(total);
        breakdown->setVisible(true);

        dueCents = zakatOn(report.zakatBaseCents);
        baseCents = report.zakatBaseCents;
        const bool reached = report.zakatBaseCents >= nisabCents;
        if (reached) {
            // A single % is correct here: QString::arg takes %1 and %2 as its
            // placeholders and leaves any other percent exactly as written, so
            // escaping it would print the escape.
            verdict->setText(tr("الزكاة الواجبة = %1 × 2.5% = %2")
                                 .arg(formatMoney(report.zakatBaseCents), formatMoney(dueCents)));
        } else {
            verdict->setText(tr("لم تبلغ النصاب (%1 من %2)")
                                 .arg(formatMoney(report.zakatBaseCents), formatMoney(nisabCents)));
        }
        verdict->setVisible(true);
        // Nothing to pay means nothing to mark paid: the button is the only way
        // to record a payment, so it exists only when there is one to record.
        markPaid->setVisible(reached);
    });

    QObject::connect(markPaid, &QPushButton::clicked, &dialog, [&]() {
        if (nisabCents < 0) {
            return;
        }
        const QDate today = QDate::currentDate();

        // Which zakat year this payment settles. It reads the date rather than
        // taking the calendar year, because a shop whose zakat date falls later
        // in the year than today is still in the year before.
        const QString storedDate =
            data::SettingRepository(db).value(QStringLiteral("zakat_date")).value_or(QString());
        const QDate zakatDate = QDate::fromString(storedDate.trimmed(), QStringLiteral("yyyy-MM-dd"));
        const int year = core::zakatYearFor(today, zakatDate);

        data::SettingRepository settings(db);
        // The nisab as well: marking the payment is the operator saying they
        // worked this out and acted on it, and they should not also have to
        // press the threshold button first for it to be on file.
        settings.set(QStringLiteral("nisab_cents"), QString::number(nisabCents));
        settings.set(QStringLiteral("zakat_paid_year"), QString::number(year));
        settings.set(QStringLiteral("zakat_paid_cents"), QString::number(dueCents));
        settings.set(QStringLiteral("zakat_paid_date"), today.toString(Qt::ISODate));

        // The year is recorded in the history, not just in settings: settings
        // keeps only the latest and would lose every year before it. The
        // settings writes above stay for the notifier, but they are a mirror of
        // this row and not the record of it.
        //
        // insertOrIgnore first, because markPaid only updates a row that is
        // already there — a payment for a year nobody assessed would otherwise
        // be silently dropped.
        data::ZakatHistoryRepository history(db);
        history.insertOrIgnore(year, nisabCents, baseCents, dueCents, goldPriceCents);
        history.markPaid(year, dueCents, today.toString(Qt::ISODate));

        // Paying is the only thing that settles the year, so paying is what claims
        // it. shouldNotifyZakat no longer writes, so without this the reminder
        // would keep asking about a year that has been paid.
        core::markZakatYearAsSeen(db, year);

        QMessageBox::information(&dialog, tr("الزكاة"),
                                 tr("Zakat payée pour %1. بارك الله فيك.").arg(year));
        dialog.accept();
    });

    // Deliberately nothing here, and that is the behaviour rather than an
    // omission: "Plus tard" means not now, not settled. The year stays unclaimed,
    // so shouldNotifyZakat still sees it as owed and the reminder comes back on
    // the next run. Claiming it here is what used to lose the shop its reminder
    // for a whole year.
    QObject::connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

} // namespace app::ui