#include "suppliers_page.h"

#include <QBrush>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/purchase.h"
#include "core/supplier_payment.h"
#include "data/purchase_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_repository.h"
#include "dialogs/supplier_dialog.h"
#include "format_utils.h"
#include "theme.h"
#include "theme_tokens.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// Column order shared by the grid, the footer counters and the row readers.
constexpr int kColName = 0;
constexpr int kColPhone = 1;
constexpr int kColBalance = 2;
constexpr int kColUnpaid = 3;

constexpr int kSearchDebounceMs = 200;

void addRow(QTableWidget* table, const QStringList& cells)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size() && column < table->columnCount(); ++column) {
        table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
}

// A balance is a claim on the supplier, not a debt we owe: it is worth noticing,
// so it is painted rather than left as another number in the row. A settled
// supplier is deliberately quiet — the muted grey says "nothing to chase here"
// without adding a chip to the column.
QColor balanceBrush(long long balance)
{
    if (balance > 0) {
        return QColor(activeTheme() == QLatin1String("dark") ? QStringLiteral("#fbbf24")
                                                             : QStringLiteral("#b45309"));
    }
    return QColor(activeTheme() == QLatin1String("dark") ? QStringLiteral("#7c8b96")
                                                         : QStringLiteral("#94a3b8"));
}

} // namespace

SuppliersPage::SuppliersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    root->addWidget(new PageHeader(tr("Fournisseurs"),
                                   tr("Fournisseurs et leurs comptes à crédit")));

    // ---- card 1: search, filters, add ----
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setMinimumHeight(48);
    m_search->setPlaceholderText(tr("Rechercher un fournisseur…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                        QLineEdit::LeadingPosition);

    // Typing refreshes straight away, but one keystroke at a time over a whole
    // supplier book is a lot of queries for nothing: the grid follows 200ms
    // after the last key instead.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);

    // "Tous" is every supplier on file. Unlike the customer list, an inactive
    // supplier is not hidden here: what is owed to them stays owed while they are
    // marked inactive, so the row has to keep answering for it.
    const QList<QPair<QString, QString>> filters = {
        {QStringLiteral("all"), tr("Tous")},
        {QStringLiteral("debt"), tr("Avec dette")},
        {QStringLiteral("clear"), tr("Sans dette")},
    };
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(8);
    for (const auto& [key, label] : filters) {
        auto* chip = new QPushButton(label);
        chip->setObjectName(QStringLiteral("filterChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setProperty("filterKey", key);
        m_chips.insert(key, chip);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);

    auto* add = new QPushButton(tr("Ajouter"));
    add->setObjectName(QStringLiteral("primary"));
    add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    toolbar->addWidget(m_search, 1);
    toolbar->addLayout(chipRow);
    toolbar->addWidget(add);

    auto* toolbarCard = makeCard();
    auto* toolbarLayout = new QVBoxLayout(toolbarCard);
    padCardLayout(toolbarLayout);
    toolbarLayout->addLayout(toolbar);

    // ---- card 2: the grid ----
    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("supplierTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Nom"), tr("Téléphone"), tr("Solde"), tr("Factures impayées")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColPhone, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColBalance, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColUnpaid, QHeaderView::Fixed);
    m_table->setColumnWidth(kColPhone, 140);
    m_table->setColumnWidth(kColBalance, 120);
    m_table->setColumnWidth(kColUnpaid, 150);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    padCardLayout(gridLayout);
    gridLayout->addWidget(makeCardTitle(tr("Fournisseurs et soldes")));
    gridLayout->addWidget(m_table, 1);

    // ---- footer ----
    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("faintText"));

    root->addWidget(toolbarCard);
    root->addWidget(gridCard, 1);
    root->addWidget(m_footer);

    connect(m_search, &QLineEdit::textChanged, this, &SuppliersPage::onSearchChanged);
    connect(m_searchDebounce, &QTimer::timeout, this, &SuppliersPage::refresh);
    connect(add, &QPushButton::clicked, this, &SuppliersPage::onAddClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &SuppliersPage::onRowActivated);
    for (auto* chip : m_chips) {
        connect(chip, &QPushButton::clicked, this, &SuppliersPage::onFilterChipClicked);
    }

    setFilterActive(m_filterKey);
    refresh();
}

int SuppliersPage::unpaidInvoiceCount(int supplierId) const
{
    // Paid is read from the payments tied to the invoice, the same figure the
    // supplier card shows in its "Reste" column: two places answering "is this
    // invoice settled" with two different rules would be worse than either.
    data::PurchaseRepository purchases(m_db);
    data::SupplierPaymentRepository payments(m_db);
    int unpaid = 0;
    for (const core::Purchase& purchase : purchases.findBySupplier(supplierId)) {
        long long paid = 0;
        for (const core::SupplierPayment& payment : payments.findByPurchaseId(purchase.id)) {
            paid += payment.amountCents;
        }
        if (purchase.totalCents - paid > 0) {
            ++unpaid;
        }
    }
    return unpaid;
}

void SuppliersPage::refresh()
{
    const QString query = m_search->text().trimmed();
    // Every balance comes from one place, so the grid, the footer and the
    // supplier card cannot end up quoting three different numbers.
    data::SupplierRepository suppliers(m_db);
    const auto all = suppliers.findAll();

    int shown = 0;
    int withDebt = 0;
    long long totalDue = 0;

    // The grid is rebuilt from scratch, so a selection change made while filling
    // it would otherwise come back through the signal and fight the new rows.
    const QSignalBlocker blocker(m_table);
    m_updating = true;
    m_table->setRowCount(0);

    for (const core::Supplier& supplier : all) {
        if (!query.isEmpty() && !supplier.name.contains(query, Qt::CaseInsensitive)
            && !supplier.phone.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        const long long balance = suppliers.balanceCentsFor(supplier.id);
        if (m_filterKey == QLatin1String("debt") && balance <= 0) {
            continue;
        }
        if (m_filterKey == QLatin1String("clear") && balance > 0) {
            continue;
        }

        ++shown;
        if (balance > 0) {
            ++withDebt;
            totalDue += balance;
        }

        addRow(m_table, {supplier.name, supplier.phone, formatMoney(balance),
                         QString::number(unpaidInvoiceCount(supplier.id))});
        const int row = m_table->rowCount() - 1;
        m_table->item(row, kColBalance)->setForeground(QBrush(balanceBrush(balance)));
        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->item(row, column)->setData(Qt::UserRole, supplier.id);
        }
    }

    m_updating = false;
    m_footer->setText(tr("Total : %1 fournisseurs · %2 avec dette · Solde total : %3")
                          .arg(shown)
                          .arg(withDebt)
                          .arg(formatMoney(totalDue)));
}

int SuppliersPage::rowCount() const
{
    return m_table->rowCount();
}

int SuppliersPage::supplierCount() const
{
    return rowCount();
}

QString SuppliersPage::balanceAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return QString();
    }
    return m_table->item(row, kColBalance)->text();
}

QString SuppliersPage::footerText() const
{
    return m_footer->text();
}

int SuppliersPage::supplierIdAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return 0;
    }
    const QTableWidgetItem* cell = m_table->item(row, kColName);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

int SuppliersPage::selectedSupplierId() const
{
    return supplierIdAt(m_table->currentRow());
}

void SuppliersPage::setFilterActive(const QString& key)
{
    m_filterKey = key;
    for (auto it = m_chips.cbegin(); it != m_chips.cend(); ++it) {
        QPushButton* chip = it.value();
        const bool on = it.key() == key;
        // A stylesheet keyed on the object name needs a repolish to notice the
        // change; setObjectName alone repaints with the old rule still cached.
        chip->setObjectName(on ? QStringLiteral("filterChipActive") : QStringLiteral("filterChip"));
        chip->setChecked(on);
        chip->style()->unpolish(chip);
        chip->style()->polish(chip);
        chip->update();
    }
}

void SuppliersPage::onSearchChanged()
{
    m_searchDebounce->start();
}

void SuppliersPage::onFilterChipClicked()
{
    auto* chip = qobject_cast<QPushButton*>(sender());
    if (!chip) {
        return;
    }
    const QString key = chip->property("filterKey").toString();
    if (key.isEmpty()) {
        return;
    }
    setFilterActive(key);
    refresh();
}

void SuppliersPage::onAddClicked()
{
    // A new supplier starts with nothing owed and an opening figure of zero: the
    // "all" filter hides nothing and the balance reads 0.00 until an invoice is
    // recorded.
    const auto maybeSupplier = showSupplierInfoDialog(this, m_db, core::Supplier{});
    if (!maybeSupplier) {
        return;
    }
    if (data::SupplierRepository(m_db).save(*maybeSupplier) == 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'enregistrer le fournisseur"));
        return;
    }
    refresh();
}

void SuppliersPage::onRowActivated(int row, int column)
{
    // Any column opens the card: the name, the phone and the balance are all
    // labels here, so a double click anywhere on the line means "open this
    // supplier".
    Q_UNUSED(column)
    const int id = supplierIdAt(row);
    if (id == 0) {
        return;
    }
    showSupplierCardDialog(this, m_db, id);
    // The card saves and deletes in place, so the list behind it is reloaded
    // whatever the operator did in there.
    refresh();
}

} // namespace app::ui
