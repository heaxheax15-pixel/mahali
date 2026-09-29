#include "supplier_dialog.h"

#include <algorithm>

#include <optional>

#include <QAbstractButton>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVector>
#include <QVBoxLayout>

#include "core/purchase.h"
#include "core/session.h"
#include "core/supplier_payment.h"
#include "core/supplier_return.h"
#include "data/date_utils.h"
#include "data/purchase_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_return_repository.h"
#include "format_utils.h"
#include "purchase_dialog.h"
#include "scan_safe_dialog.h"
#include "supplier_payment_dialog.h"
#include "supplier_return_dialog.h"

namespace app::ui {

namespace {

// The information form keeps to 360px whatever the card is wide, and the rest of
// its row is left empty: a name or a phone number has nothing to fill 800px
// with, and a line edit stretched that far is mostly box.
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
    table->setObjectName(QStringLiteral("supplierTable"));
    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setShowGrid(false);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setDefaultSectionSize(42);
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

// Purchases, payments and returns store their dates as ISO text, so the same
// helper renders all three tabs. A value that will not parse is shown as it
// stands rather than blanked: hiding it would look like a missing line.
QString shortStamp(const QString& iso)
{
    if (iso.isEmpty()) {
        return QString();
    }
    const std::optional<QDateTime> parsed = data::fromIso(iso);
    if (parsed.has_value()) {
        return parsed->date().toString(QStringLiteral("dd/MM/yyyy"));
    }
    return iso.left(10);
}

QDateTime stampOf(const QString& iso)
{
    return data::fromIso(iso).value_or(QDateTime());
}

QString tr(const char* text)
{
    return QCoreApplication::translate("SupplierDialog", text);
}

} // namespace

std::optional<core::Supplier> showSupplierInfoDialog(QWidget* parent, app::data::Database& db,
                                                     const core::Supplier& initial)
{
    const bool forNew = initial.id == 0;
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? tr("Nouveau fournisseur") : tr("Modifier le fournisseur"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);
    auto* phone = new QLineEdit(initial.phone);
    auto* address = new QLineEdit(initial.address);
    auto* notes = new QTextEdit(initial.notes);
    notes->setFixedHeight(72);

    data::SupplierRepository repo(db);
    auto* balance = new QLabel(formatMoney(repo.balanceCentsFor(initial.id)));
    auto* opening = new QLineEdit(initial.openingBalanceCents > 0
                                      ? formatMoney(initial.openingBalanceCents)
                                      : QString());
    opening->setPlaceholderText(tr("Solde repris d'un ancien registre"));
    auto* active = new QCheckBox(tr("Fournisseur actif"));
    active->setChecked(initial.active);

    QFormLayout* form = new QFormLayout;
    form->addRow(tr("Nom"), name);
    form->addRow(tr("Téléphone"), phone);
    form->addRow(tr("Adresse"), address);
    form->addRow(tr("Notes"), notes);
    form->addRow(tr("Solde actuel"), balance);
    form->addRow(tr("Solde d'ouverture"), opening);
    form->addRow(QString(), active);

    // The opening figure rewrites history rather than recording anything, so it
    // belongs to the manager. Read-only for anyone else, and kept as shown text
    // so the balance still adds up on screen in front of them.
    const bool manager =
        app::core::Session::instance().currentUser().role == QStringLiteral("admin");
    if (!manager) {
        if (forNew) {
            opening->clear();
        }
        opening->setReadOnly(true);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    saveBtn->setText(tr("Enregistrer"));
    cancelBtn->setText(tr("Annuler"));
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

    // An empty opening field means "nothing carried in", not "zero typed on
    // purpose" — the placeholder says what the box is for, so a blank must not
    // quietly wipe a figure someone brought over on paper.
    long long openingCents = initial.openingBalanceCents;
    if (!forNew && !opening->text().trimmed().isEmpty()) {
        const auto parsed = parseMoney(opening->text());
        if (!parsed || *parsed < 0) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Solde d'ouverture invalide"));
            return std::nullopt;
        }
        openingCents = *parsed;
    }

    core::Supplier supplier = initial;
    supplier.name = name->text().trimmed();
    supplier.phone = phone->text().trimmed();
    supplier.address = address->text().trimmed();
    supplier.notes = notes->toPlainText().trimmed();
    if (!forNew) {
        supplier.openingBalanceCents = openingCents;
    }
    supplier.active = active->isChecked();
    return supplier;
}

void showSupplierCardDialog(QWidget* parent, app::data::Database& db, int supplierId)
{
    data::SupplierRepository repo(db);
    const auto supplier = repo.findById(supplierId);
    if (!supplier) {
        return;
    }

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(supplier->name);
    dialog.setModal(true);
    dialog.resize(820, 560);

    // ---- tab 1: informations ----
    auto* info = new QWidget;
    auto* infoLayout = new QVBoxLayout(info);
    auto* infoForm = new QFormLayout;
    // Without this the form stretches every field to the card's full width, and a
    // line edit sized for "Algeria Nord" is a lot of empty box to look at. A
    // layout cannot be sized, so the form sits in a box that can be.
    infoForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* infoFormBox = new QWidget;
    infoFormBox->setLayout(infoForm);
    infoFormBox->setMaximumWidth(kInfoFormWidth);
    auto* infoFields = new QHBoxLayout;
    infoFields->addStretch(1);
    infoFields->addWidget(infoFormBox);
    auto* nameField = new QLineEdit(supplier->name);
    auto* phoneField = new QLineEdit(supplier->phone);
    auto* addressField = new QLineEdit(supplier->address);
    auto* notesField = new QTextEdit(supplier->notes);
    notesField->setFixedHeight(72);
    auto* balanceValue = new QLabel(formatMoney(repo.balanceCentsFor(supplierId)));
    auto* openingField = new QLineEdit(supplier->openingBalanceCents > 0
                                           ? formatMoney(supplier->openingBalanceCents)
                                           : QString());
    openingField->setPlaceholderText(tr("Solde repris d'un ancien registre"));
    auto* activeBox = new QCheckBox(tr("Fournisseur actif"));
    activeBox->setChecked(supplier->active);

    infoForm->addRow(tr("Nom"), nameField);
    infoForm->addRow(tr("Téléphone"), phoneField);
    infoForm->addRow(tr("Adresse"), addressField);
    infoForm->addRow(tr("Notes"), notesField);
    infoForm->addRow(tr("Solde actuel"), balanceValue);
    infoForm->addRow(tr("Solde d'ouverture"), openingField);
    infoForm->addRow(QString(), activeBox);

    const bool manager =
        app::core::Session::instance().currentUser().role == QStringLiteral("admin");
    if (!manager) {
        openingField->setReadOnly(true);
    }

    auto* saveButton = new QPushButton(tr("Enregistrer"));
    saveButton->setObjectName(QStringLiteral("primary"));
    infoLayout->addLayout(infoFields);
    infoLayout->addStretch(1);

    // ---- tab 2: factures d'achat ----
    auto* invoicesTab = new QWidget;
    auto* invoicesLayout = new QVBoxLayout(invoicesTab);
    auto* newInvoiceButton = new QPushButton(tr("Nouvelle facture"));
    newInvoiceButton->setObjectName(QStringLiteral("primary"));
    auto* invoiceTable = makeLedgerTable({tr("Date"), tr("N° Facture"), tr("Total"), tr("Payé"),
                                          tr("Reste")});
    auto* invoiceTotal = new QLabel;
    invoiceTotal->setObjectName(QStringLiteral("faintText"));
    invoicesLayout->addWidget(newInvoiceButton);
    invoicesLayout->addWidget(invoiceTable, 1);
    invoicesLayout->addWidget(invoiceTotal);

    // ---- tab 3: paiements ----
    auto* paymentsTab = new QWidget;
    auto* paymentsLayout = new QVBoxLayout(paymentsTab);
    auto* newPaymentButton = new QPushButton(tr("Nouveau paiement"));
    newPaymentButton->setObjectName(QStringLiteral("primary"));
    auto* paymentTable = makeLedgerTable(
        {tr("Date"), tr("Montant"), tr("Facture liée"), tr("Note")});
    paymentsLayout->addWidget(newPaymentButton);
    paymentsLayout->addWidget(paymentTable, 1);

    // ---- tab 4: retours ----
    auto* returnsTab = new QWidget;
    auto* returnsLayout = new QVBoxLayout(returnsTab);
    auto* newReturnButton = new QPushButton(tr("Nouveau retour"));
    newReturnButton->setObjectName(QStringLiteral("primary"));
    auto* returnTable =
        makeLedgerTable({tr("Date"), tr("Montant"), tr("Facture liée")});
    returnsLayout->addWidget(newReturnButton);
    returnsLayout->addWidget(returnTable, 1);

    // ---- tab 5: historique ----
    auto* historyTable =
        makeLedgerTable({tr("Date"), tr("Type"), tr("Montant"), tr("Solde après")});
    auto* historyTab = new QWidget;
    auto* historyLayout = new QVBoxLayout(historyTab);
    historyLayout->addWidget(historyTable, 1);

    auto* tabs = new QTabWidget;
    tabs->addTab(info, tr("Informations"));
    tabs->addTab(invoicesTab, tr("Factures d'achat"));
    tabs->addTab(paymentsTab, tr("Paiements"));
    tabs->addTab(returnsTab, tr("Retours"));
    tabs->addTab(historyTab, tr("Historique"));

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs, 1);

    // A supplier with money behind them is history, not a typo: deleting one
    // would take its invoices, payments and returns with it, so the button only
    // appears while the supplier has none of the three.
    auto* deleteButton = new QPushButton(tr("Supprimer"));
    deleteButton->setObjectName(QStringLiteral("danger"));
    deleteButton->setVisible(false);
    // The card closes from this row rather than from a button box of its own: a
    // "Fermer" below the tabs said nothing about what the other two buttons do,
    // and left the eye travelling between two toolbars to close one window.
    auto* cancelButton = new QPushButton(tr("Annuler"));
    cancelButton->setObjectName(QStringLiteral("secondary"));
    cancelButton->setAutoDefault(false);
    cancelButton->setDefault(false);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* infoButtons = new QHBoxLayout;
    infoButtons->addWidget(cancelButton);
    infoButtons->addStretch(1);
    infoButtons->addWidget(deleteButton);
    infoButtons->addWidget(saveButton);
    infoLayout->addLayout(infoButtons);

    data::PurchaseRepository purchases(db);
    data::SupplierPaymentRepository payments(db);
    data::SupplierReturnRepository returns(db);

    // Every tab re-reads the database instead of trusting the supplier the card
    // was opened with, so an invoice or a payment taken in another tab shows up
    // here at once.
    const auto reload = [&]() {
        const auto invoices = purchases.findBySupplier(supplierId);
        const auto paid = payments.findBySupplierId(supplierId);
        const auto returned = returns.findBySupplierId(supplierId);

        // Invoice numbers keyed by id, so the payment and return tabs can name
        // the invoice a line belongs to without going back to the database per
        // cell.
        QHash<int, QString> invoiceNumbers;
        for (const core::Purchase& invoice : invoices) {
            invoiceNumbers.insert(invoice.id, invoice.invoiceNumber);
        }
        const auto invoiceLabel = [&invoiceNumbers](const std::optional<int>& id) {
            if (!id.has_value()) {
                return tr("—");
            }
            const QString number = invoiceNumbers.value(*id);
            return number.isEmpty() ? tr("—") : number;
        };

        // What has actually been paid against one invoice, the same figure the
        // balance uses.
        const auto paidFor = [&paid](int purchaseId) {
            long long sum = 0;
            for (const core::SupplierPayment& payment : paid) {
                if (payment.purchaseId.has_value() && *payment.purchaseId == purchaseId) {
                    sum += payment.amountCents;
                }
            }
            return sum;
        };

        invoiceTable->setRowCount(0);
        long long unpaid = 0;
        for (const core::Purchase& invoice : invoices) {
            const long long settled = paidFor(invoice.id);
            const long long reste = invoice.totalCents - settled;
            unpaid += reste;
            addRow(invoiceTable, {shortStamp(invoice.purchasedAt), invoice.invoiceNumber,
                                  formatMoney(invoice.totalCents), formatMoney(settled),
                                  formatMoney(reste)});
        }
        invoiceTotal->setText(tr("Total impayé : %1").arg(formatMoney(unpaid)));

        paymentTable->setRowCount(0);
        for (const core::SupplierPayment& payment : paid) {
            addRow(paymentTable, {shortStamp(payment.paidAt), formatMoney(payment.amountCents),
                                  invoiceLabel(payment.purchaseId), payment.note});
        }

        returnTable->setRowCount(0);
        for (const core::SupplierReturn& entry : returned) {
            addRow(returnTable, {shortStamp(entry.returnedAt), formatMoney(entry.amountCents),
                                 invoiceLabel(entry.purchaseId)});
        }

        // One ledger walked in date order carrying the running balance, so the
        // last column answers "what did we owe them after this line".
        struct Entry {
            QDateTime when;
            // Orders same-second lines the way the balance treats them: an
            // invoice first, then the money that pays it, then goods handed back.
            int rank = 0;
            QString type;
            long long amount = 0;
        };
        QVector<Entry> entries;
        for (const core::Purchase& invoice : invoices) {
            entries.append({stampOf(invoice.purchasedAt), 0, tr("Facture"), invoice.totalCents});
        }
        for (const core::SupplierPayment& payment : paid) {
            entries.append({stampOf(payment.paidAt), 1, tr("Paiement"),
                            -payment.amountCents});
        }
        for (const core::SupplierReturn& entry : returned) {
            entries.append({stampOf(entry.returnedAt), 2, tr("Retour"), -entry.amountCents});
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.when != b.when) {
                return a.when < b.when;
            }
            return a.rank < b.rank;
        });

        historyTable->setRowCount(0);
        long long running = supplier->openingBalanceCents;
        for (const Entry& entry : entries) {
            running += entry.amount;
            addRow(historyTable, {entry.when.isValid() ? entry.when.date().toString(
                                                               QStringLiteral("dd/MM/yyyy"))
                                                        : QString(),
                                  entry.type, formatMoney(entry.amount),
                                  formatMoney(running)});
        }

        balanceValue->setText(formatMoney(repo.balanceCentsFor(supplierId)));
        const bool untouched = invoices.isEmpty() && paid.empty() && returned.empty();
        deleteButton->setVisible(untouched);
    };
    reload();

    QObject::connect(newInvoiceButton, &QPushButton::clicked, &dialog, [&]() {
        // Recording from inside the supplier's own card is the common case: the
        // supplier is already known, so the dialog opens on it and only the
        // invoice itself is left to type.
        if (showPurchaseDialog(&dialog, db, supplierId).saved) {
            reload();
        }
    });
    QObject::connect(newPaymentButton, &QPushButton::clicked, &dialog, [&]() {
        // A payment against a specific invoice is the case that needs the list,
        // and this button is reached from the Payments tab rather than from an
        // invoice row, so the dialog opens on the general entry and lets the
        // operator name the invoice when they know it.
        if (showSupplierPaymentDialog(&dialog, db, supplierId).saved) {
            // reload() re-reads the ledgers rather than the supplier the card was
            // opened with, so the payment shows up in its own tab, in the
            // history with its running balance, and in the balance on the first
            // tab, all at once.
            reload();
        }
    });
    QObject::connect(newReturnButton, &QPushButton::clicked, &dialog, [&]() {
        if (showSupplierReturnDialog(&dialog, db, supplierId).saved) {
            // reload() re-reads the ledgers rather than the supplier the card was
            // opened with, so the credit shows up in its own tab, in the history
            // with its running balance, and in the balance on the first tab, all
            // at once.
            reload();
        }
    });
    QObject::connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
        const QString name = nameField->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Le nom est obligatoire"));
            return;
        }
        long long openingCents = supplier->openingBalanceCents;
        if (manager && !openingField->text().trimmed().isEmpty()) {
            const auto parsed = parseMoney(openingField->text());
            if (!parsed || *parsed < 0) {
                QMessageBox::warning(&dialog, tr("Erreur"), tr("Solde d'ouverture invalide"));
                return;
            }
            openingCents = *parsed;
        }

        core::Supplier edited = *supplier;
        edited.name = name;
        edited.phone = phoneField->text().trimmed();
        edited.address = addressField->text().trimmed();
        edited.notes = notesField->toPlainText().trimmed();
        edited.openingBalanceCents = openingCents;
        edited.active = activeBox->isChecked();
        if (repo.save(edited) == 0) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Enregistrement impossible"));
            return;
        }
        dialog.setWindowTitle(edited.name);
        reload();
    });
    QObject::connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
        // The button is hidden the moment a ledger line exists, so re-ask here:
        // the guard is what protects the invoices, not the visibility.
        if (!purchases.findBySupplier(supplierId).isEmpty()
            || !payments.findBySupplierId(supplierId).empty()
            || !returns.findBySupplierId(supplierId).empty()) {
            QMessageBox::warning(
                &dialog, tr("Erreur"),
                tr("Ce fournisseur a des factures, des paiements ou des retours. "
                   "Désactivez-le plutôt que de le supprimer."));
            return;
        }
        const auto answer = QMessageBox::question(
            &dialog, tr("Supprimer le fournisseur"),
            tr("Supprimer « %1 » ?").arg(supplier->name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        // SupplierRepository has no remove() yet, and the data layer is out of
        // this change's reach: the one statement it would grow is written here so
        // the button is not a lie, and moves to the repository with the rest of
        // phase 4b.
        QSqlQuery query(db.handle());
        query.prepare(QStringLiteral("DELETE FROM suppliers WHERE id = ?"));
        query.addBindValue(supplierId);
        if (!query.exec() || query.numRowsAffected() == 0) {
            db.recordError(query.lastError(), QStringLiteral("SupplierDialog::delete"));
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Suppression impossible"));
            return;
        }
        dialog.accept();
    });

    dialog.exec();
}

} // namespace app::ui
