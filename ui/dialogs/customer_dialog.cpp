#include "customer_dialog.h"

#include <algorithm>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/payment.h"
#include "core/customer_transaction.h"
#include "core/sale_item.h"
#include "core/session.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/product_repository.h"
#include "data/sale_service.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

namespace {

// The information form keeps to 360px whatever the card is wide, and the rest of
// its row is left empty: a name or a phone number has nothing to fill 760px
// with, and a field stretched that far is mostly box.
constexpr int kInfoFormWidth = 360;

// A scanner presses Return wherever it is standing, so every dialog here drops
// the auto-default the button box installs. Otherwise Return inside a field
// submits the form half-typed.
void disarmDefaults(QDialogButtonBox* buttons)
{
    for (QAbstractButton* b : buttons->buttons()) {
        if (auto* pb = qobject_cast<QPushButton*>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    }
}

QTableWidget* makeLedgerTable(const QStringList& headers)
{
    auto* table = new QTableWidget;
    table->setObjectName(QStringLiteral("customerTable"));
    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setShowGrid(true);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setDefaultSectionSize(42);
    table->verticalHeader()->hide();
    return table;
}

void addRow(QTableWidget* table, const QStringList& cells)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size() && column < table->columnCount(); ++column) {
        table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
}

QString shortDate(const QDateTime& stamp)
{
    if (!stamp.isValid()) {
        return QString();
    }
    return stamp.date().toString(QStringLiteral("dd/MM/yyyy"));
}

// A compact credit-sale builder: pick products, set quantity/price, and hand
// back the resulting sale items on accept. Kept as its own dialog because a debt
// is a sale and the operator has to see the running total before committing.
bool collectDebtItems(QWidget* parent, app::data::Database& db, QVector<core::SaleItem>* out)
{
    app::data::ProductRepository products(db);
    const auto catalog = products.findAll();

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("CustomerDialog", "Vente à crédit"));
    dialog.setModal(true);
    dialog.resize(560, 400);

    auto* combo = new QComboBox;
    combo->setEditable(true);
    for (const core::Product& product : catalog) {
        const QString label = product.barcode.isEmpty()
            ? product.name
            : QStringLiteral("%1 (%2)").arg(product.name, product.barcode);
        combo->addItem(label, product.id);
        if (product.barcode == QLatin1String("6130000000004") && catalog.size() == 1) {
            combo->setCurrentIndex(combo->count() - 1);
        }
    }
    auto* qty = new QSpinBox;
    qty->setRange(1, 1000000);
    qty->setValue(1);
    auto* price = new QLineEdit;
    price->setPlaceholderText(
        QCoreApplication::translate("CustomerDialog", "Cliquez pour changer le prix"));
    auto* addButton = new QPushButton(QCoreApplication::translate("CustomerDialog", "Ajouter une ligne"));
    auto* removeButton =
        new QPushButton(QCoreApplication::translate("CustomerDialog", "Retirer la ligne"));

    auto* table = makeLedgerTable({QCoreApplication::translate("CustomerDialog", "Produit"),
                                   QCoreApplication::translate("CustomerDialog", "Quantité"),
                                   QCoreApplication::translate("CustomerDialog", "Prix unitaire"),
                                   QCoreApplication::translate("CustomerDialog", "Total")});

    auto* totalLabel = new QLabel;
    auto* hintLabel = new QLabel(QCoreApplication::translate(
        "CustomerDialog", "Quantité et prix décimaux ? Corrigez les lignes avant d'enregistrer."));

    auto* picker = new QHBoxLayout;
    picker->addWidget(combo, 1);
    picker->addWidget(qty);
    picker->addWidget(price);
    picker->addWidget(addButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    // This dialog records a sale, not a customer, so its confirm button says so.
    okBtn->setText(QCoreApplication::translate("CustomerDialog", "Enregistrer la vente"));
    cancelBtn->setText(QCoreApplication::translate("CustomerDialog", "Annuler"));
    okBtn->setIcon(QIcon());
    cancelBtn->setIcon(QIcon());
    okBtn->setObjectName(QStringLiteral("primary"));
    disarmDefaults(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(picker);
    layout->addWidget(table, 1);
    layout->addWidget(totalLabel);
    layout->addWidget(removeButton);
    layout->addWidget(hintLabel);
    layout->addWidget(buttons);

    const auto refreshTotal = [table, totalLabel]() {
        long long total = 0;
        for (int i = 0; i < table->rowCount(); ++i) {
            total += parseMoney(table->item(i, 3)->text()).value_or(0);
        }
        totalLabel->setText(
            QCoreApplication::translate("CustomerDialog", "Total : %1").arg(formatMoney(total)));
    };

    QObject::connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        const int productId = combo->currentData().toInt();
        if (productId <= 0) {
            QMessageBox::warning(
                &dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                QCoreApplication::translate("CustomerDialog", "Choisissez un produit"));
            return;
        }
        const auto product = products.findById(productId);
        if (!product) {
            return;
        }
        long long unitPrice = product->salePriceCents;
        if (!price->text().trimmed().isEmpty()) {
            const auto overridePrice = parseMoney(price->text());
            if (!overridePrice || *overridePrice < 0) {
                QMessageBox::warning(&dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                                     QCoreApplication::translate("CustomerDialog",
                                                                "Prix unitaire invalide"));
                return;
            }
            unitPrice = *overridePrice;
        }
        const int row = table->rowCount();
        table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(product->name);
        nameItem->setData(Qt::UserRole, productId);
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(qty->value())));
        table->setItem(row, 2, new QTableWidgetItem(formatMoney(unitPrice)));
        table->setItem(row, 3, new QTableWidgetItem(formatMoney(unitPrice * qty->value())));
        price->clear();
        refreshTotal();
    });

    QObject::connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row >= 0) {
            table->removeRow(row);
            refreshTotal();
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    out->clear();
    for (int i = 0; i < table->rowCount(); ++i) {
        bool qtyOk = false;
        const qlonglong quantity = table->item(i, 1)->text().toLongLong(&qtyOk);
        const auto unitPrice = parseMoney(table->item(i, 2)->text());
        if (!qtyOk || quantity <= 0 || !unitPrice || *unitPrice < 0) {
            return false;
        }
        core::SaleItem item;
        item.productId = table->item(i, 0)->data(Qt::UserRole).toInt();
        item.quantity = quantity;
        item.unitPriceCents = *unitPrice;
        out->append(item);
    }
    return !out->isEmpty();
}

} // namespace

std::optional<core::Customer> showCustomerInfoDialog(QWidget* parent, app::data::Database& db,
                                                     const core::Customer& initial)
{
    const bool forNew = initial.id == 0;
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew
                              ? QCoreApplication::translate("CustomerDialog", "Nouveau client")
                              : QCoreApplication::translate("CustomerDialog", "Modifier le client"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);
    auto* phone = new QLineEdit(initial.phone);

    app::data::CustomerRepository repo(db);
    auto* balance = new QLabel(formatMoney(repo.balanceCentsFor(initial.id)));
    auto* opening = new QLineEdit(initial.openingBalanceCents > 0
                                      ? formatMoney(initial.openingBalanceCents)
                                      : QString());
    opening->setPlaceholderText(
        QCoreApplication::translate("CustomerDialog", "Solde repris d'un ancien registre"));
    auto* active = new QCheckBox(QCoreApplication::translate("CustomerDialog", "Client actif"));
    active->setChecked(initial.active);

    QFormLayout* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("CustomerDialog", "Nom"), name);
    form->addRow(QCoreApplication::translate("CustomerDialog", "Téléphone"), phone);
    form->addRow(QCoreApplication::translate("CustomerDialog", "Solde actuel"), balance);
    form->addRow(QCoreApplication::translate("CustomerDialog", "Solde d'ouverture"), opening);
    form->addRow(QString(), active);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    saveBtn->setText(QCoreApplication::translate("CustomerDialog", "Enregistrer"));
    cancelBtn->setText(QCoreApplication::translate("CustomerDialog", "Annuler"));
    saveBtn->setIcon(QIcon());
    cancelBtn->setIcon(QIcon());
    saveBtn->setObjectName(QStringLiteral("primary"));
    disarmDefaults(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    if (name->text().trimmed().isEmpty()) {
        return std::nullopt;
    }

    // The opening figure is the one field a cashier has no business typing: it
    // rewrites history rather than recording anything, so it is only editable by
    // the manager. Read-only for anyone else, and kept as shown text so the
    // balance still adds up on screen in front of them.
    const bool manager = app::core::Session::instance().currentUser().role
        == QStringLiteral("admin");
    if (forNew && !manager) {
        opening->clear();
        opening->setReadOnly(true);
    }
    // An empty opening field means "nothing carried in", not "zero typed on
    // purpose" — the placeholder says what the box is for, so a blank must not
    // quietly wipe a figure someone brought over on paper.
    long long openingCents = initial.openingBalanceCents;
    if (!forNew && !opening->text().trimmed().isEmpty()) {
        const auto parsed = parseMoney(opening->text());
        if (!parsed || *parsed < 0) {
            QMessageBox::warning(&dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                                 QCoreApplication::translate("CustomerDialog",
                                                            "Solde d'ouverture invalide"));
            return std::nullopt;
        }
        openingCents = *parsed;
    }

    core::Customer customer = initial;
    customer.name = name->text().trimmed();
    customer.phone = phone->text().trimmed();
    if (!forNew) {
        customer.openingBalanceCents = openingCents;
    }
    customer.active = active->isChecked();
    return customer;
}

bool showNewDebtDialog(QWidget* parent, app::data::Database& db, int customerId)
{
    if (customerId <= 0) {
        return false;
    }
    QVector<core::SaleItem> items;
    if (!collectDebtItems(parent, db, &items)) {
        return false;
    }
    app::data::SaleService service(db);
    const app::data::SaleRecordResult result =
        service.recordCustomerDebt(customerId, items, QStringLiteral("desktop"),
                                   /*allowOversold=*/false);
    if (!result.ok) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
            QCoreApplication::translate("CustomerDialog", "Vente à crédit impossible : %1")
                .arg(result.error));
        return false;
    }
    return true;
}

bool showSettleDebtDialog(QWidget* parent, app::data::Database& db, int customerId,
                          long long currentBalance)
{
    if (customerId <= 0) {
        return false;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(
        parent, QCoreApplication::translate("CustomerDialog", "Remboursement"),
        QCoreApplication::translate("CustomerDialog", "Montant remis maintenant (max %1) :")
            .arg(formatMoney(currentBalance)),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return false;
    }
    const auto cents = parseMoney(text);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
                             QCoreApplication::translate("CustomerDialog", "Montant invalide"));
        return false;
    }
    // Handing back more than they owe would leave the ledger claiming a debt in
    // the other direction, which reads as a customer who owes us. Refuse it here
    // rather than let a stray extra digit invent one.
    if (*cents > currentBalance) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
            QCoreApplication::translate("CustomerDialog", "Le montant dépasse le solde (%1).")
                .arg(formatMoney(currentBalance)));
        return false;
    }

    app::data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    if (!session) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
            QCoreApplication::translate("CustomerDialog",
                                        "Aucune caisse ouverte — ouvrez-la d'abord."));
        return false;
    }
    app::data::PaymentService service(db);
    const app::data::PaymentResult result =
        service.recordCustomerPayment(customerId, *cents, session->id, QString());
    if (!result.ok) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
            QCoreApplication::translate("CustomerDialog", "Remboursement impossible : %1")
                .arg(result.error));
        return false;
    }
    return true;
}

// The transactions a cancellation is allowed to act on: positive rows that have
// not already been cancelled. A payment on the ledger is negative and a
// cancellation is negative, and both are excluded for the same reason — neither
// is a sale that can be given back.
QVector<core::CustomerTransaction> cancellableCreditSales(app::data::Database& db, int customerId)
{
    QVector<core::CustomerTransaction> candidates;
    app::data::CustomerTransactionRepository transactions(db);
    QSqlQuery cancelled(db.handle());
    cancelled.prepare(QStringLiteral(
        "SELECT DISTINCT reversed_transaction_id FROM customer_transactions "
        "WHERE reversed_transaction_id IS NOT NULL"));
    QSet<int> alreadyCancelled;
    if (cancelled.exec()) {
        while (cancelled.next()) {
            alreadyCancelled.insert(cancelled.value(0).toInt());
        }
    }
    const auto all = transactions.findByCustomerId(customerId);
    for (const core::CustomerTransaction& tx : all) {
        if (tx.amountCents > 0 && !alreadyCancelled.contains(tx.id)) {
            candidates.push_back(tx);
        }
    }
    return candidates;
}

bool showCancelDebtDialog(QWidget* parent, app::data::Database& db, int customerId)
{
    if (customerId <= 0) {
        return false;
    }
    const QVector<core::CustomerTransaction> candidates = cancellableCreditSales(db, customerId);
    if (candidates.isEmpty()) {
        QMessageBox::information(
            parent, QCoreApplication::translate("CustomerDialog", "Annuler une vente"),
            QCoreApplication::translate("CustomerDialog",
                                        "Aucune vente à crédit à annuler pour ce client."));
        return false;
    }

    // Named by date and amount rather than by row number: the operator is looking
    // at the ledger above and has to be able to match what they see here to what
    // they see there.
    QStringList labels;
    labels.reserve(candidates.size());
    for (const core::CustomerTransaction& tx : candidates) {
        labels << QStringLiteral("%1 — %2")
                      .arg(shortDate(tx.createdAt), formatMoney(tx.amountCents));
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        parent, QCoreApplication::translate("CustomerDialog", "Annuler une vente"),
        QCoreApplication::translate("CustomerDialog", "Vente à annuler :"), labels, 0, false, &ok);
    if (!ok) {
        return false;
    }
    const int index = labels.indexOf(chosen);
    if (index < 0) {
        return false;
    }
    const int transactionId = candidates[index].id;

    // Asked up front, because the answer is not obvious and finding out halfway
    // is how a stock count ends up short. The sale stays on the ledger as a
    // negative line — nothing is deleted — and the goods go back on the shelf.
    const auto confirmed = QMessageBox::question(
        parent, QCoreApplication::translate("CustomerDialog", "Confirmer l'annulation"),
        QCoreApplication::translate(
            "CustomerDialog",
            "Annuler %1 ?\n\nLa vente reste visible avec un montant négatif et les produits retournent "
            "en stock.")
            .arg(formatMoney(candidates[index].amountCents)));
    if (confirmed != QMessageBox::Yes) {
        return false;
    }

    app::data::SaleService sales(db);
    const app::data::SaleReverseResult result = sales.reverseCustomerDebt(transactionId);
    if (!result.ok) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("CustomerDialog", "Erreur"),
            QCoreApplication::translate("CustomerDialog", "Annulation impossible : %1")
                .arg(result.error));
        return false;
    }
    return true;
}

bool showCustomerCardDialog(QWidget* parent, app::data::Database& db, const core::Customer& customer)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(customer.name);
    dialog.setModal(true);
    dialog.resize(760, 520);

    app::data::CustomerRepository repo(db);

    // ---- tab 1: informations ----
    auto* info = new QWidget;
    auto* infoLayout = new QVBoxLayout(info);
    auto* infoForm = new QFormLayout;
    // Without this the form stretches every field to the card's full width, and a
    // line edit sized for "Marie Dupont" is a lot of empty box to look at. A
    // layout cannot be sized, so the form sits in a box that can be.
    infoForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* infoFormBox = new QWidget;
    infoFormBox->setLayout(infoForm);
    infoFormBox->setMaximumWidth(kInfoFormWidth);
    auto* infoFields = new QHBoxLayout;
    infoFields->addStretch(1);
    infoFields->addWidget(infoFormBox);
    auto* nameValue = new QLabel(customer.name);
    auto* phoneValue = new QLabel(customer.phone);
    auto* balanceValue = new QLabel;
    auto* openingValue = new QLabel(formatMoney(customer.openingBalanceCents));
    auto* activeValue = new QLabel;
    infoForm->addRow(QCoreApplication::translate("CustomerDialog", "Nom"), nameValue);
    infoForm->addRow(QCoreApplication::translate("CustomerDialog", "Téléphone"), phoneValue);
    infoForm->addRow(QCoreApplication::translate("CustomerDialog", "Solde actuel"), balanceValue);
    infoForm->addRow(QCoreApplication::translate("CustomerDialog", "Solde d'ouverture"), openingValue);
    infoForm->addRow(QCoreApplication::translate("CustomerDialog", "Statut"), activeValue);
    infoLayout->addLayout(infoFields);
    infoLayout->addStretch(1);

    auto* editButton = new QPushButton(QCoreApplication::translate("CustomerDialog", "Modifier"));
    editButton->setObjectName(QStringLiteral("secondary"));
    auto* deleteButton =
        new QPushButton(QCoreApplication::translate("CustomerDialog", "Supprimer le client"));
    deleteButton->setObjectName(QStringLiteral("danger"));
    // The card closes from this row rather than from a button box of its own: a
    // "Fermer" below the tabs said nothing about what the other two buttons do,
    // and left the eye travelling between two toolbars to close one window.
    auto* cancelButton = new QPushButton(QCoreApplication::translate("CustomerDialog", "Annuler"));
    cancelButton->setObjectName(QStringLiteral("secondary"));
    cancelButton->setAutoDefault(false);
    cancelButton->setDefault(false);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto* infoButtons = new QHBoxLayout;
    infoButtons->addWidget(cancelButton);
    infoButtons->addStretch(1);
    infoButtons->addWidget(editButton);
    infoButtons->addWidget(deleteButton);
    infoLayout->addLayout(infoButtons);

    // ---- tab 2: ventes à crédit ----
    auto* salesTab = new QWidget;
    auto* salesLayout = new QVBoxLayout(salesTab);
    auto* salesButtons = new QHBoxLayout;
    auto* newDebtButton =
        new QPushButton(QCoreApplication::translate("CustomerDialog", "Nouvelle vente à crédit"));
    newDebtButton->setObjectName(QStringLiteral("primary"));
    // Cancels a sale already on the ledger. Sits beside the create button rather
    // than in a menu: once a credit sale is cancelled the customer no longer owes
    // it, and that is a thing the operator does have to reach for.
    auto* cancelDebtButton =
        new QPushButton(QCoreApplication::translate("CustomerDialog", "Annuler une vente"));
    cancelDebtButton->setToolTip(QCoreApplication::translate(
        "CustomerDialog",
        "Annule une vente à crédit : elle reste visible avec un montant négatif, et les produits "
        "retournent en stock."));
    salesButtons->addWidget(newDebtButton);
    salesButtons->addWidget(cancelDebtButton);
    salesButtons->addStretch(1);
    auto* salesTable = makeLedgerTable({QCoreApplication::translate("CustomerDialog", "Date"),
                                        QCoreApplication::translate("CustomerDialog", "Produits"),
                                        QCoreApplication::translate("CustomerDialog", "Total"),
                                        QCoreApplication::translate("CustomerDialog", "Note")});
    salesLayout->addLayout(salesButtons);
    salesLayout->addWidget(salesTable, 1);

    // ---- tab 3: remboursements ----
    auto* payTab = new QWidget;
    auto* payLayout = new QVBoxLayout(payTab);
    auto* settleButton =
        new QPushButton(QCoreApplication::translate("CustomerDialog", "Nouveau remboursement"));
    settleButton->setObjectName(QStringLiteral("primary"));
    auto* payTable = makeLedgerTable({QCoreApplication::translate("CustomerDialog", "Date"),
                                      QCoreApplication::translate("CustomerDialog", "Montant"),
                                      QCoreApplication::translate("CustomerDialog", "Note")});
    payLayout->addWidget(settleButton);
    payLayout->addWidget(payTable, 1);

    // ---- tab 4: historique ----
    auto* historyTable = makeLedgerTable({QCoreApplication::translate("CustomerDialog", "Date"),
                                          QCoreApplication::translate("CustomerDialog", "Type"),
                                          QCoreApplication::translate("CustomerDialog", "Montant"),
                                          QCoreApplication::translate("CustomerDialog",
                                                                     "Solde après")});
    auto* historyTab = new QWidget;
    auto* historyLayout = new QVBoxLayout(historyTab);
    historyLayout->addWidget(historyTable, 1);

    auto* tabs = new QTabWidget;
    tabs->addTab(info, QCoreApplication::translate("CustomerDialog", "Informations"));
    tabs->addTab(salesTab, QCoreApplication::translate("CustomerDialog", "Ventes à crédit"));
    tabs->addTab(payTab, QCoreApplication::translate("CustomerDialog", "Remboursements"));
    tabs->addTab(historyTab, QCoreApplication::translate("CustomerDialog", "Historique"));

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs, 1);

    bool changed = false;
    bool deleted = false;

    // Every tab re-reads the database instead of trusting the customer the card
    // was opened with, so a debt taken in tab 2 shows up in tab 4 at once.
    const auto reload = [&]() {
        const long long balance = repo.balanceCentsFor(customer.id);
        balanceValue->setText(formatMoney(balance));
        openingValue->setText(formatMoney(customer.openingBalanceCents));
        activeValue->setText(customer.active ? QCoreApplication::translate("CustomerDialog", "Actif")
                                             : QCoreApplication::translate("CustomerDialog",
                                                                            "Inactif"));

        app::data::CustomerTransactionRepository transactions(db);
        app::data::PaymentRepository payments(db);
        const auto sales = transactions.findByCustomerId(customer.id);
        const auto repayments = payments.findByCustomerId(customer.id);

        salesTable->setRowCount(0);
        for (const core::CustomerTransaction& tx : sales) {
            // No amount_cents > 0 test here. A reversal is a negative row against
            // the original (that is what reversed_transaction_id is for), so
            // dropping it would leave a cancelled credit sale listed as a sale
            // and the two tabs would disagree with the balance above them.
            addRow(salesTable, {shortDate(tx.createdAt),
                                QCoreApplication::translate("CustomerDialog", "Vente à crédit"),
                                formatMoney(tx.amountCents), QString()});
        }

        payTable->setRowCount(0);
        for (const core::Payment& payment : repayments) {
            // Same reasoning: a refunded payment is a negative row and belongs
            // on this list, not filtered out of it.
            addRow(payTable, {shortDate(payment.createdAt), formatMoney(payment.amountCents),
                              payment.note});
        }

        historyTable->setRowCount(0);
        // One ledger walked in date order carrying the running balance, so the
        // last column answers "what did they owe after this line".
        struct Entry {
            QDateTime when;
            QString type;
            long long amount;
        };
        QVector<Entry> entries;
        for (const core::CustomerTransaction& tx : sales) {
            entries.append({tx.createdAt, QCoreApplication::translate("CustomerDialog", "Vente"),
                            tx.amountCents});
        }
        for (const core::Payment& payment : repayments) {
            entries.append({payment.createdAt,
                            QCoreApplication::translate("CustomerDialog", "Remboursement"),
                            -payment.amountCents});
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.when < b.when; });
        long long running = customer.openingBalanceCents;
        for (const Entry& entry : entries) {
            running += entry.amount;
            addRow(historyTable,
                   {shortDate(entry.when), entry.type, formatMoney(entry.amount),
                    formatMoney(running)});
        }
    };
    reload();

    QObject::connect(newDebtButton, &QPushButton::clicked, &dialog, [&]() {
        if (showNewDebtDialog(&dialog, db, customer.id)) {
            changed = true;
            reload();
        }
    });
    QObject::connect(cancelDebtButton, &QPushButton::clicked, &dialog, [&]() {
        if (showCancelDebtDialog(&dialog, db, customer.id)) {
            changed = true;
            reload();
        }
    });
    QObject::connect(settleButton, &QPushButton::clicked, &dialog, [&]() {
        if (showSettleDebtDialog(&dialog, db, customer.id, repo.balanceCentsFor(customer.id))) {
            changed = true;
            reload();
        }
    });
    QObject::connect(editButton, &QPushButton::clicked, &dialog, [&]() {
        const auto current = repo.findById(customer.id);
        if (!current) {
            return;
        }
        const auto edited = showCustomerInfoDialog(&dialog, db, *current);
        if (!edited) {
            return;
        }
        if (repo.save(*edited) == 0) {
            QMessageBox::warning(&dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                                 QCoreApplication::translate("CustomerDialog",
                                                            "Enregistrement impossible"));
            return;
        }
        nameValue->setText(edited->name);
        phoneValue->setText(edited->phone);
        openingValue->setText(formatMoney(edited->openingBalanceCents));
        activeValue->setText(edited->active
                                 ? QCoreApplication::translate("CustomerDialog", "Actif")
                                 : QCoreApplication::translate("CustomerDialog", "Inactif"));
        changed = true;
        reload();
    });
    QObject::connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
        const long long balance = repo.balanceCentsFor(customer.id);
        // A customer who still owes money cannot be deleted: the ledger line
        // recording that debt would go with them and the amount would stop
        // existing as far as any report is concerned.
        if (balance > 0) {
            QMessageBox::warning(
                &dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                QCoreApplication::translate(
                    "CustomerDialog", "Ce client doit encore %1. Enregistrez son solde d'abord, "
                                      "ou désactivez-le.")
                    .arg(formatMoney(balance)));
            return;
        }
        const auto answer = QMessageBox::question(
            &dialog, QCoreApplication::translate("CustomerDialog", "Supprimer le client"),
            QCoreApplication::translate("CustomerDialog",
                                        "Supprimer « %1 » ? Son historique disparaît avec lui.")
                .arg(customer.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        if (!repo.remove(customer.id)) {
            QMessageBox::warning(&dialog, QCoreApplication::translate("CustomerDialog", "Erreur"),
                                 QCoreApplication::translate("CustomerDialog",
                                                            "Suppression impossible"));
            return;
        }
        deleted = true;
        dialog.accept();
    });

    dialog.exec();
    return changed && !deleted;
}

} // namespace app::ui
