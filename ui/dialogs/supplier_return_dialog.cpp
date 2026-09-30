#include "supplier_return_dialog.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/purchase.h"
#include "core/purchase_item.h"
#include "core/supplier_return_item.h"
#include "data/date_utils.h"
#include "data/product_repository.h"
#include "data/purchase_item_repository.h"
#include "data/purchase_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_return_item_repository.h"
#include "data/supplier_return_repository.h"
#include "data/supplier_return_service.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

namespace {

// Column order of the linked-return grid.
constexpr int kColProduct = 0;
constexpr int kColBought = 1;
constexpr int kColUnitPrice = 2;
constexpr int kColReturned = 3;

QString tr(const char* text)
{
    return QCoreApplication::translate("SupplierReturnDialog", text);
}

// A scanner presses Return wherever it is standing, so no button here may claim
// the default action: Enter must walk the form instead of recording a return
// half-typed.
void disarmDefaults(QDialogButtonBox* buttons)
{
    for (QAbstractButton* b : buttons->buttons()) {
        if (auto* pb = qobject_cast<QPushButton*>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    }
}

} // namespace

SupplierReturnDialogResult showSupplierReturnDialog(QWidget* parent, app::data::Database& db,
                                                    int supplierId)
{
    SupplierReturnDialogResult result;

    data::SupplierRepository suppliers(db);
    data::PurchaseRepository purchases(db);
    data::PurchaseItemRepository purchaseItems(db);
    data::ProductRepository products(db);
    data::StockMovementRepository stockMovements(db);
    data::SupplierReturnRepository returnRows(db);
    data::SupplierReturnItemRepository returnItems(db);
    data::SupplierReturnService service(db, returnRows, returnItems, suppliers, purchases, products,
                                        stockMovements);

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("Nouveau retour"));
    dialog.setModal(true);
    dialog.resize(600, 640);

    // ---- the two shapes of return ----
    auto* typeCombo = new QComboBox;
    typeCombo->addItem(tr("Retour général (montant)"));
    typeCombo->addItem(tr("Retour lié à une facture"));

    // ---- general: an amount and nothing else ----
    auto* generalBox = new QFrame;
    generalBox->setObjectName(QStringLiteral("card"));
    auto* generalForm = new QFormLayout(generalBox);
    generalForm->setHorizontalSpacing(12);
    generalForm->setVerticalSpacing(8);
    // No validator: parseMoney already accepts "1234,50", "1 234.50", Arabic
    // digits and a trailing currency symbol, which is what an operator types and
    // what a figure copied out of the ledger looks like. A QDoubleValidator in
    // the C locale would refuse the comma that the rest of the app writes.
    auto* amountField = new QLineEdit;
    amountField->setPlaceholderText(tr("0"));
    generalForm->addRow(tr("Montant"), amountField);

    // ---- linked: an invoice and the lines of it ----
    auto* invoiceCombo = new QComboBox;
    for (const core::Purchase& invoice : purchases.findBySupplier(supplierId)) {
        const std::optional<QDateTime> stamp = data::fromIso(invoice.purchasedAt);
        invoiceCombo->addItem(
            tr("%1 — %2 du %3")
                .arg(invoice.invoiceNumber.isEmpty() ? tr("Facture #%1").arg(invoice.id)
                                                    : invoice.invoiceNumber)
                .arg(formatMoney(invoice.totalCents))
                .arg(stamp.has_value() ? stamp->date().toString(QStringLiteral("dd/MM/yyyy")) : QString()),
            invoice.id);
    }

    auto* itemsTable = new QTableWidget;
    // productTable is the table object name whose cells are styled for editing:
    // the general QLineEdit rule is sized for a form field and would burst the
    // row height.
    itemsTable->setObjectName(QStringLiteral("productTable"));
    itemsTable->setAlternatingRowColors(true);
    itemsTable->setFrameShape(QFrame::NoFrame);
    itemsTable->setShowGrid(true);
    itemsTable->setColumnCount(4);
    itemsTable->setHorizontalHeaderLabels(
        {tr("Produit"), tr("Qté achetée"), tr("Prix unitaire"), tr("Qté retournée")});
    itemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    itemsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    itemsTable->verticalHeader()->setDefaultSectionSize(46);
    itemsTable->verticalHeader()->hide();

    auto* linkedBox = new QFrame;
    linkedBox->setObjectName(QStringLiteral("card"));
    auto* linkedForm = new QFormLayout(linkedBox);
    linkedForm->setHorizontalSpacing(12);
    linkedForm->setVerticalSpacing(8);
    linkedForm->addRow(tr("Facture"), invoiceCombo);
    linkedForm->addRow(itemsTable);

    // ---- what every shape has in common ----
    auto* dateEdit = new QDateEdit;
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    dateEdit->setDate(QDate::currentDate());

    auto* noteField = new QLineEdit;

    auto* commonForm = new QFormLayout;
    commonForm->setHorizontalSpacing(12);
    commonForm->setVerticalSpacing(8);
    commonForm->addRow(tr("Date"), dateEdit);
    commonForm->addRow(tr("Note"), noteField);

    // Refusals land here rather than in a dialog box: a rejected amount is
    // something to correct in place, and a modal box would hide the field that
    // has to change.
    auto* errorLabel = new QLabel;
    errorLabel->setObjectName(QStringLiteral("noticeErr"));
    errorLabel->setWordWrap(true);
    errorLabel->hide();

    // A return that was recorded but left the shelves below zero is a different
    // thing from a refusal: the goods did go back, and the operator has to hear
    // about the count that no longer adds up.
    auto* warningLabel = new QLabel;
    warningLabel->setObjectName(QStringLiteral("noticeOk"));
    warningLabel->setWordWrap(true);
    warningLabel->hide();

    const auto fail = [&errorLabel](const QString& message) {
        errorLabel->setText(message);
        errorLabel->show();
    };

    auto* typeForm = new QFormLayout;
    typeForm->setHorizontalSpacing(12);
    typeForm->setVerticalSpacing(8);
    typeForm->addRow(tr("Type"), typeCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    saveBtn->setText(tr("Enregistrer"));
    saveBtn->setObjectName(QStringLiteral("primary"));
    cancelBtn->setText(tr("Annuler"));
    cancelBtn->setObjectName(QStringLiteral("secondary"));
    disarmDefaults(buttons);

    QVBoxLayout* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);
    root->addLayout(typeForm);
    root->addWidget(generalBox);
    root->addWidget(linkedBox, 1);
    root->addLayout(commonForm);
    root->addWidget(warningLabel);
    root->addWidget(errorLabel);
    root->addWidget(buttons);

    // The lines of the chosen invoice, one row each, with the quantity to hand
    // back on the row. Rebuilt whenever the invoice changes, so the grid always
    // shows the lines of the invoice actually named above it.
    const auto loadInvoiceLines = [&]() {
        itemsTable->setRowCount(0);
        const int purchaseId = invoiceCombo->currentData().toInt();
        if (purchaseId <= 0) {
            return;
        }
        for (const core::PurchaseItem& line : purchaseItems.findByPurchase(purchaseId)) {
            const int row = itemsTable->rowCount();
            itemsTable->insertRow(row);
            auto* product = new QTableWidgetItem(line.description);
            // The product id rides on the row, so the saved line is read back
            // from what is on screen rather than from a list that could drift.
            product->setData(Qt::UserRole, line.productId.value_or(0));
            product->setFlags(product->flags() & ~Qt::ItemIsEditable);
            itemsTable->setItem(row, kColProduct, product);
            const auto readOnly = [](const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                return item;
            };
            itemsTable->setItem(row, kColBought, readOnly(QString::number(line.quantity)));
            itemsTable->setItem(row, kColUnitPrice, readOnly(formatMoney(line.unitPriceCents)));

            // Capped at what the invoice says was bought: handing back more than
            // arrived is a mistake in the invoice, not a quantity to record.
            auto* returned = new QSpinBox;
            returned->setRange(0, static_cast<int>(qMin(line.quantity, 999999LL)));
            returned->setValue(0);
            // The unit price is kept on the box, so the credit for this line is
            // worked out from the figure the invoice was written with.
            returned->setProperty("unitPriceCents", line.unitPriceCents);
            itemsTable->setCellWidget(row, kColReturned, returned);
        }
    };

    // Which of the two shapes is on screen. The fields are shown and hidden
    // rather than disabled: a disabled form still invites a click.
    const auto applyMode = [&]() {
        const bool linked = typeCombo->currentIndex() == 1;
        generalBox->setVisible(!linked);
        linkedBox->setVisible(linked);
        if (linked) {
            loadInvoiceLines();
        }
    };

    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                     [&](int) { applyMode(); });
    QObject::connect(invoiceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                     [&](int) { loadInvoiceLines(); });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        errorLabel->hide();
        warningLabel->hide();

        // The date is the operator's; the clock of the moment is carried over so
        // the return sorts the same way an invoice stamped today would.
        const QString returnedAt =
            data::toIso(QDateTime(dateEdit->date(), QDateTime::currentDateTime().time()));
        const QString note = noteField->text().trimmed();

        data::SupplierReturnResult recorded;
        if (typeCombo->currentIndex() == 1) {
            // Linked: the credit is the lines the operator marked, and only
            // those. A row left at zero is a line not being returned, not a line
            // being returned for nothing.
            QVector<core::SupplierReturnItem> lines;
            for (int row = 0; row < itemsTable->rowCount(); ++row) {
                const auto* returned =
                    qobject_cast<QSpinBox*>(itemsTable->cellWidget(row, kColReturned));
                const QTableWidgetItem* product = itemsTable->item(row, kColProduct);
                if (!returned || !product || returned->value() <= 0) {
                    continue;
                }
                core::SupplierReturnItem line;
                const int productId = product->data(Qt::UserRole).toInt();
                if (productId > 0) {
                    line.productId = productId;
                }
                line.quantity = returned->value();
                line.unitPriceCents = returned->property("unitPriceCents").toLongLong();
                line.totalCents = line.quantity * line.unitPriceCents;
                lines.append(line);
            }
            if (lines.isEmpty()) {
                fail(tr("Choisissez au moins un produit à retourner"));
                return;
            }
            recorded = service.recordLinkedReturn(supplierId, invoiceCombo->currentData().toInt(),
                                                  lines, returnedAt, note);
        } else {
            const std::optional<long long> amount = parseMoney(amountField->text());
            if (!amount.has_value() || *amount <= 0) {
                fail(tr("Le montant doit être supérieur à zéro"));
                amountField->setFocus();
                amountField->selectAll();
                return;
            }
            recorded = service.recordGeneralReturn(supplierId, *amount, returnedAt, note);
        }

        if (!recorded.ok) {
            fail(recorded.error);
            return;
        }
        // The return is recorded whatever the warning says: the goods went back.
        if (!recorded.warning.isEmpty()) {
            warningLabel->setText(recorded.warning);
            warningLabel->show();
        }
        result.saved = true;
        result.returnId = recorded.returnId;
        dialog.accept();
    });

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    applyMode();

    dialog.exec();
    return result;
}

} // namespace app::ui
