#include "customers_page.h"

#include <QColor>
#include <QDateTime>
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

#include "core/payment.h"
#include "core/customer_transaction.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/sale_service.h"
#include "dialogs/customer_dialog.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// Column order shared by the grid and the row readers. Solde stays at index 2
// because balanceAt() is read by the UI test and by anything else that grew up
// with this page.
constexpr int kColName = 0;
constexpr int kColPhone = 1;
constexpr int kColBalance = 2;
constexpr int kColLastOperation = 3;

constexpr int kSearchDebounceMs = 200;

void addRow(QTableWidget* table, const QStringList& cells)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size() && column < table->columnCount(); ++column) {
        table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
}

} // namespace

CustomersPage::CustomersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    root->addWidget(new PageHeader(tr("Clients"), tr("Suivez les dettes et les remboursements")));

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setMinimumHeight(42);
    m_notice->setObjectName(QStringLiteral("noticeOk"));
    m_notice->setText(QString());
    m_notice->setVisible(false);

    // ---- card 1: search, filters, add ----
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setMinimumHeight(48);
    m_search->setPlaceholderText(tr("Rechercher un nom ou un téléphone..."));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                        QLineEdit::LeadingPosition);

    // Typing refreshes straight away, but one keystroke at a time over a whole
    // customer book is a lot of queries for nothing: the grid follows 200ms
    // after the last key instead.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);

    // "Tous" is the customers still being served. A closed-out account is kept,
    // not shown, so an old debt can be settled without the everyday list filling
    // up with names nobody serves any more.
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
    toolbarLayout->setContentsMargins(18, 16, 18, 16);
    toolbarLayout->setSpacing(12);
    toolbarLayout->addLayout(toolbar);

    // ---- card 2: the grid ----
    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("customerTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Nom"), tr("Téléphone"), tr("Solde"), tr("Dernière opération")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    gridLayout->setContentsMargins(18, 16, 18, 16);
    gridLayout->setSpacing(12);
    gridLayout->addWidget(makeCardTitle(tr("Clients et soldes")));
    gridLayout->addWidget(m_table, 1);

    // ---- footer ----
    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("faintText"));

    root->addWidget(toolbarCard);
    root->addWidget(gridCard, 1);
    root->addWidget(m_footer);
    root->addWidget(m_notice);

    connect(m_search, &QLineEdit::textChanged, this, &CustomersPage::onSearchChanged);
    connect(m_searchDebounce, &QTimer::timeout, this, &CustomersPage::refresh);
    connect(add, &QPushButton::clicked, this, &CustomersPage::onAddClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &CustomersPage::onRowActivated);
    for (auto* chip : m_chips) {
        connect(chip, &QPushButton::clicked, this, &CustomersPage::onFilterChipClicked);
    }

    setFilterActive(m_filterKey);
    refresh();
}

QString CustomersPage::lastOperationAt(int customerId) const
{
    // The newer of the two ledgers. A blank cell is honest: a customer who has
    // never been given credit and never paid has no last operation, and showing
    // a date there would invent one.
    QDateTime latest;
    for (const core::CustomerTransaction& tx :
         data::CustomerTransactionRepository(m_db).findByCustomerId(customerId)) {
        if (!latest.isValid() || tx.createdAt > latest) {
            latest = tx.createdAt;
        }
    }
    for (const core::Payment& payment : data::PaymentRepository(m_db).findByCustomerId(customerId)) {
        if (!latest.isValid() || payment.createdAt > latest) {
            latest = payment.createdAt;
        }
    }
    if (!latest.isValid()) {
        return QString();
    }
    return latest.date().toString(QStringLiteral("dd/MM/yyyy"));
}

void CustomersPage::refresh()
{
    const QString query = m_search->text().trimmed();
    // Every balance comes from one place, so the grid, the footer and the
    // customer card cannot end up quoting three different numbers.
    data::CustomerRepository customers(m_db);
    const auto all = customers.findAll();

    int shown = 0;
    int withDebt = 0;
    long long totalDebt = 0;

    // The grid is rebuilt from scratch, so a selection change made while filling
    // it would otherwise come back through the signal and fight the new rows.
    const QSignalBlocker blocker(m_table);
    m_updating = true;
    m_table->setRowCount(0);

    for (const core::Customer& customer : all) {
        if (m_filterKey == QLatin1String("all") && !customer.active) {
            continue;
        }
        if (!query.isEmpty() && !customer.name.contains(query, Qt::CaseInsensitive)
            && !customer.phone.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        const long long balance = customers.balanceCentsFor(customer.id);
        if (m_filterKey == QLatin1String("debt") && balance <= 0) {
            continue;
        }
        if (m_filterKey == QLatin1String("clear") && balance > 0) {
            continue;
        }

        ++shown;
        if (balance > 0) {
            ++withDebt;
            totalDebt += balance;
        }

        addRow(m_table, {customer.name, customer.phone, formatMoney(balance),
                         lastOperationAt(customer.id)});
        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->item(m_table->rowCount() - 1, column)->setData(Qt::UserRole, customer.id);
        }
    }

    m_updating = false;
    m_footer->setText(tr("Total : %1 clients · %2 avec dette · %3 dus")
                          .arg(shown)
                          .arg(withDebt)
                          .arg(formatMoney(totalDebt)));

    // refresh() also runs right after a debt or a payment is recorded, so this
    // only bites when the notice genuinely has nothing in it. The guard keeps a
    // bar that was cleared elsewhere from being left showing as an empty strip.
    if (m_notice->text().isEmpty()) {
        m_notice->setVisible(false);
    }
}

int CustomersPage::rowCount() const
{
    return m_table->rowCount();
}

QString CustomersPage::balanceAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return QString();
    }
    return m_table->item(row, kColBalance)->text();
}

QString CustomersPage::noticeText() const
{
    return m_notice->text();
}

int CustomersPage::customerIdAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return 0;
    }
    const QTableWidgetItem* cell = m_table->item(row, kColName);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

int CustomersPage::selectedCustomerId() const
{
    return customerIdAt(m_table->currentRow());
}

void CustomersPage::setFilterActive(const QString& key)
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

void CustomersPage::onSearchChanged()
{
    m_searchDebounce->start();
}

void CustomersPage::onFilterChipClicked()
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

void CustomersPage::onAddClicked()
{
    // A new customer starts with nothing owed and an opening figure of zero: the
    // "all" filter hides nothing and the balance reads 0.00 until a debt is
    // recorded.
    const auto maybeCustomer = showCustomerInfoDialog(this, m_db, core::Customer{});
    if (!maybeCustomer) {
        return;
    }
    if (data::CustomerRepository(m_db).save(*maybeCustomer) == 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'enregistrer le client"));
        return;
    }
    refresh();
}

void CustomersPage::onRowActivated(int row, int column)
{
    // Any column opens the card: the name, the phone and the balance are all
    // labels here, so a double click anywhere on the line means "open this
    // customer".
    Q_UNUSED(column)
    const int id = customerIdAt(row);
    if (id == 0) {
        return;
    }
    const auto existing = data::CustomerRepository(m_db).findById(id);
    if (!existing) {
        return;
    }
    if (showCustomerCardDialog(this, m_db, *existing)) {
        refresh();
    }
}

void CustomersPage::recordDebt(int customerId, const QVector<core::SaleItem>& items)
{
    m_notice->clear();
    m_notice->setVisible(false);
    if (customerId <= 0 || items.isEmpty()) {
        m_notice->setText(tr("Aucun article à facturer"));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    data::SaleService service(m_db);
    const data::SaleRecordResult result =
        service.recordCustomerDebt(customerId, items, QStringLiteral("desktop"),
                                   /*allowOversold=*/false);
    if (!result.ok) {
        m_notice->setText(tr("Vente à crédit impossible : %1").arg(result.error));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    m_notice->setText(tr("Dette enregistrée : %1").arg(formatMoney(result.totalCents)));
    m_notice->setVisible(!m_notice->text().isEmpty());
    refresh();
}

void CustomersPage::recordPayment(int customerId, long long amountCents, const QString& note)
{
    m_notice->clear();
    m_notice->setVisible(false);
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("Aucune caisse ouverte — ouvrez-la d'abord"));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    data::PaymentService service(m_db);
    const data::PaymentResult result =
        service.recordCustomerPayment(customerId, amountCents, session->id, note);
    if (!result.ok) {
        m_notice->setText(tr("Remboursement impossible : %1").arg(result.error));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    m_notice->setText(tr("Remboursement enregistré : %1").arg(formatMoney(result.amountCents)));
    m_notice->setVisible(!m_notice->text().isEmpty());
    refresh();
}

} // namespace app::ui
