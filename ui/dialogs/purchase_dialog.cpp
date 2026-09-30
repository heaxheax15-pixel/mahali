#include "purchase_dialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <vector>

#include "core/barcode_utils.h"
#include "core/product.h"
#include "data/date_utils.h"
#include "data/purchase_item_repository.h"
#include "data/purchase_repository.h"
#include "data/purchase_service.h"
#include "data/product_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

namespace {

// Column order shared by the grid and every reader of a row.
constexpr int kColProduct = 0;
constexpr int kColQuantity = 1;
constexpr int kColUnit = 2;
constexpr int kColPrice = 3;
constexpr int kColTotal = 4;
constexpr int kColRemove = 5;

// An invoice is a piece of paper of a known length. Past this the form stops
// being a form and the operator is transcribing by hand, which is the job an
// import is for.
constexpr int kMaxLines = 200;

QString tr(const char* text)
{
    return QCoreApplication::translate("PurchaseDialog", text);
}

// A faint caption for the labels sitting next to a field, so a form of twenty
// controls reads as pairs rather than as a wall of boxes.
QLabel* caption(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("faintText"));
    return l;
}

// A scanner presses Return wherever it is standing, so no button here may claim
// the default action: Enter must walk the form instead of recording the invoice
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

PurchaseDialogResult showPurchaseDialog(QWidget* parent, app::data::Database& db, int supplierId,
                                        const core::Purchase& initial)
{
    PurchaseDialogResult result;

    data::ProductRepository products(db);
    data::SupplierRepository suppliers(db);

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("Nouvelle facture d'achat"));
    dialog.setModal(true);
    dialog.resize(900, 650);

    // ---- header: the invoice itself ----
    auto* supplierCombo = new QComboBox;
    for (const core::Supplier& supplier : suppliers.listActive()) {
        supplierCombo->addItem(supplier.name, supplier.id);
    }
    // 0 means "no opinion": the combo is left on its first entry only when the
    // caller actually named a supplier that exists.
    const int preselect = supplierCombo->findData(supplierId > 0 ? supplierId : initial.supplierId);
    if (preselect >= 0) {
        supplierCombo->setCurrentIndex(preselect);
    }

    auto* invoiceNumber = new QLineEdit(initial.invoiceNumber);

    auto* dateEdit = new QDateEdit;
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    const auto initialStamp = data::fromIso(initial.purchasedAt);
    dateEdit->setDate(initialStamp.has_value() ? initialStamp->date() : QDate::currentDate());

    auto* addToStock = new QCheckBox(tr("Ajouter au stock"));
    addToStock->setChecked(initial.addToStock);

    auto* vatField = new QLineEdit(initial.vatCents > 0 ? formatMoney(initial.vatCents) : QString());
    vatField->setPlaceholderText(tr("0"));

    auto* noteField = new QTextEdit(initial.note);
    noteField->setFixedHeight(64);

    auto* headerGrid = new QGridLayout;
    headerGrid->setHorizontalSpacing(12);
    headerGrid->setVerticalSpacing(8);
    headerGrid->addWidget(caption(tr("Fournisseur")), 0, 0);
    headerGrid->addWidget(supplierCombo, 0, 1);
    headerGrid->addWidget(caption(tr("N° Facture")), 0, 2);
    headerGrid->addWidget(invoiceNumber, 0, 3);
    headerGrid->addWidget(caption(tr("Date")), 0, 4);
    headerGrid->addWidget(dateEdit, 0, 5);
    headerGrid->addWidget(addToStock, 1, 0, 1, 2);
    headerGrid->addWidget(caption(tr("TVA")), 1, 2);
    headerGrid->addWidget(vatField, 1, 3);
    headerGrid->addWidget(caption(tr("Note")), 1, 4);
    headerGrid->addWidget(noteField, 1, 5);
    headerGrid->setColumnStretch(1, 1);
    headerGrid->setColumnStretch(3, 1);
    headerGrid->setColumnStretch(5, 2);

    auto* headerCard = new QFrame;
    headerCard->setObjectName(QStringLiteral("card"));
    headerCard->setLayout(headerGrid);

    // ---- middle: the lines ----
    auto* searchField = new QLineEdit;
    searchField->setObjectName(QStringLiteral("searchField"));
    searchField->setPlaceholderText(tr("Scanner ou rechercher..."));
    searchField->setClearButtonEnabled(true);

    auto* itemsTable = new QTableWidget;
    // productTable is the one table object name whose cells are styled for
    // editing: the general QLineEdit rule is sized for a form field, which would
    // burst the 46px row height.
    itemsTable->setObjectName(QStringLiteral("productTable"));
    itemsTable->setAlternatingRowColors(true);
    itemsTable->setFrameShape(QFrame::NoFrame);
    itemsTable->setShowGrid(false);
    itemsTable->setColumnCount(6);
    itemsTable->setHorizontalHeaderLabels({tr("Produit"), tr("Qté"), tr("Unité"), tr("Prix unitaire"),
                                           tr("Total"), QString()});
    itemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    itemsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    itemsTable->horizontalHeader()->setSectionResizeMode(kColRemove, QHeaderView::Fixed);
    itemsTable->setColumnWidth(kColRemove, 64);
    itemsTable->verticalHeader()->setDefaultSectionSize(46);

    auto* addLineButton = new QPushButton(tr("+"));
    addLineButton->setObjectName(QStringLiteral("secondary"));
    addLineButton->setMaximumWidth(52);

    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    searchRow->addWidget(searchField, 1);
    searchRow->addWidget(addLineButton);

    // ---- bottom: what the invoice adds up to ----
    auto* subtotalValue = new QLabel;
    auto* vatValue = new QLabel;
    auto* totalValue = new QLabel;
    auto* resteValue = new QLabel;
    QFont totalFont = totalValue->font();
    totalFont.setPointSize(totalFont.pointSize() + 4);
    totalFont.setBold(true);
    totalValue->setFont(totalFont);

    auto* paidAll = new QCheckBox(tr("Payé intégralement"));
    paidAll->setChecked(initial.paidCents > 0);
    auto* paidField = new QLineEdit(initial.paidCents > 0 ? formatMoney(initial.paidCents) : QString());
    paidField->setPlaceholderText(tr("0"));

    auto* totalsGrid = new QGridLayout;
    totalsGrid->setHorizontalSpacing(12);
    totalsGrid->setVerticalSpacing(6);
    totalsGrid->addWidget(caption(tr("Sous-total")), 0, 0);
    totalsGrid->addWidget(subtotalValue, 0, 1);
    totalsGrid->addWidget(caption(tr("TVA")), 1, 0);
    totalsGrid->addWidget(vatValue, 1, 1);
    totalsGrid->addWidget(caption(tr("Total")), 2, 0);
    totalsGrid->addWidget(totalValue, 2, 1);
    totalsGrid->addWidget(paidAll, 0, 2, 1, 2);
    totalsGrid->addWidget(caption(tr("Montant payé")), 1, 2);
    totalsGrid->addWidget(paidField, 1, 3);
    totalsGrid->addWidget(caption(tr("Reste")), 2, 2);
    totalsGrid->addWidget(resteValue, 2, 3);
    totalsGrid->setColumnStretch(1, 1);
    totalsGrid->setColumnStretch(3, 1);

    // ---- buttons ----
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
    root->addWidget(headerCard);
    root->addLayout(searchRow);
    root->addWidget(itemsTable, 1);
    root->addLayout(totalsGrid);
    root->addWidget(buttons);

    // ---- the lines ----

    // Everything below reads the widgets back rather than keeping a shadow copy
    // of the invoice, so what is saved is what is on screen.
    const auto lineTotal = [](int row, QTableWidget* table) {
        const auto* qty = qobject_cast<QSpinBox*>(table->cellWidget(row, kColQuantity));
        const auto* price = qobject_cast<QLineEdit*>(table->cellWidget(row, kColPrice));
        if (!qty || !price) {
            return 0LL;
        }
        const std::optional<long long> unitPrice = parseMoney(price->text());
        if (!unitPrice.has_value()) {
            return 0LL;
        }
        return static_cast<long long>(qty->value()) * *unitPrice;
    };

    const auto recompute = [&]() {
        long long subtotal = 0;
        for (int row = 0; row < itemsTable->rowCount(); ++row) {
            const long long total = lineTotal(row, itemsTable);
            subtotal += total;
            if (QTableWidgetItem* cell = itemsTable->item(row, kColTotal)) {
                cell->setText(formatMoney(total));
            }
        }
        const long long vat = parseMoney(vatField->text()).value_or(0);
        const long long total = subtotal + vat;

        subtotalValue->setText(formatMoney(subtotal));
        vatValue->setText(formatMoney(vat));
        totalValue->setText(formatMoney(total));

        // "Payé intégralement" is the same statement as typing the total, so it
        // writes the figure rather than hiding the paid box: the operator still
        // sees what the invoice is being recorded as settled by.
        if (paidAll->isChecked()) {
            paidField->setText(formatMoney(total));
        }
        const long long paid = parseMoney(paidField->text()).value_or(0);
        resteValue->setText(formatMoney(total - paid));
    };

    // A row is identified by the widget sitting in it, not by a number captured
    // when it was built: removing a line renumbers every row below it, and a
    // captured index would then point at the wrong product.
    const auto rowOf = [](QTableWidget* table, QWidget* widget, int column) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->cellWidget(row, column) == widget) {
                return row;
            }
        }
        return -1;
    };

    const auto bindRow = [&](int row, const core::Product& product) {
        auto* name = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColProduct));
        if (!name) {
            return;
        }
        // The id rides on the field itself rather than in a parallel table, so a
        // row carries its own meaning and survives the renumbering above.
        name->setProperty("productId", product.id);
        name->setText(product.name);

        if (QTableWidgetItem* unit = itemsTable->item(row, kColUnit)) {
            unit->setText(product.unit.isEmpty() ? QStringLiteral("piece") : product.unit);
        }
        // What the article currently costs is the honest first guess for a
        // purchase price; it still has to be confirmed line by line.
        auto* price = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColPrice));
        if (price && price->text().trimmed().isEmpty()) {
            price->setText(formatMoney(product.costPriceCents > 0 ? product.costPriceCents
                                                                  : product.salePriceCents));
        }
        recompute();
    };

    // What the typed text could be: an exact barcode first, since that is what a
    // scanner sends, and a name search otherwise.
    const auto matchesFor = [&products](const QString& text) {
        if (const auto byBarcode = products.findByBarcode(text); byBarcode.has_value()) {
            return std::vector<core::Product>{*byBarcode};
        }
        std::vector<core::Product> byName;
        for (const core::Product& product : products.findAll()) {
            if (product.name.contains(text, Qt::CaseInsensitive)) {
                byName.push_back(product);
            }
        }
        return byName;
    };

    // Puts a menu under the field the text was typed in and returns the article
    // the operator picked, if any. More than one article carries the same name,
    // so guessing would bill the wrong one and move the wrong stock.
    const auto askWhich = [&](QWidget* anchor,
                              const std::vector<core::Product>& matches) -> std::optional<core::Product> {
        QMenu menu(anchor);
        for (const core::Product& product : matches) {
            QAction* action = menu.addAction(product.name);
            action->setData(product.id);
        }
        QAction* chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
        if (!chosen) {
            return std::optional<core::Product>();
        }
        for (const core::Product& product : matches) {
            if (product.id == chosen->data().toInt()) {
                return product;
            }
        }
        return std::optional<core::Product>();
    };

    // Turns whatever is in a product cell into a bound product.
    const auto resolveRow = [&](int row) {
        auto* field = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColProduct));
        if (!field) {
            return;
        }
        const QString text = field->text().trimmed();
        if (text.isEmpty()) {
            field->setProperty("productId", 0);
            if (QTableWidgetItem* unit = itemsTable->item(row, kColUnit)) {
                unit->setText(QString());
            }
            recompute();
            return;
        }
        const std::vector<core::Product> matches = matchesFor(text);
        if (matches.empty()) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Produit introuvable"));
            return;
        }
        const std::optional<core::Product> chosen =
            matches.size() == 1 ? std::optional<core::Product>(matches.front())
                                : askWhich(field, matches);
        if (chosen.has_value()) {
            bindRow(row, *chosen);
        }
    };

    // An empty line is a line waiting to be filled in, not a nameless product:
    // the id stays 0 and the save check refuses it until a product is chosen.
    const auto addRow = [&]() {
        if (itemsTable->rowCount() >= kMaxLines) {
            QMessageBox::warning(&dialog, tr("Erreur"),
                                 tr("Une facture ne peut pas dépasser %1 lignes.").arg(kMaxLines));
            return false;
        }
        const int row = itemsTable->rowCount();
        itemsTable->insertRow(row);

        auto* name = new QLineEdit;
        name->setProperty("productId", 0);
        name->setPlaceholderText(tr("Produit…"));
        QObject::connect(name, &QLineEdit::returnPressed, &dialog, [&, name]() {
            resolveRow(rowOf(itemsTable, name, kColProduct));
        });
        itemsTable->setCellWidget(row, kColProduct, name);

        auto* qty = new QSpinBox;
        qty->setRange(1, 999999);
        qty->setValue(1);
        QObject::connect(qty, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
                         [&]() { recompute(); });
        itemsTable->setCellWidget(row, kColQuantity, qty);

        auto* price = new QLineEdit;
        price->setPlaceholderText(tr("0"));
        QObject::connect(price, &QLineEdit::editingFinished, &dialog, [&]() { recompute(); });
        itemsTable->setCellWidget(row, kColPrice, price);

        auto* remove = new QPushButton(QStringLiteral("X"));
        remove->setObjectName(QStringLiteral("danger"));
        remove->setMaximumWidth(64);
        QObject::connect(remove, &QPushButton::clicked, &dialog, [&, remove]() {
            const int victim = rowOf(itemsTable, remove, kColRemove);
            if (victim >= 0) {
                itemsTable->removeRow(victim);
                recompute();
            }
        });
        itemsTable->setCellWidget(row, kColRemove, remove);

        const auto readOnlyItem = [&]() {
            auto* item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        itemsTable->setItem(row, kColUnit, readOnlyItem());
        itemsTable->setItem(row, kColTotal, readOnlyItem());

        return true;
    };

    const auto addMatch = [&](const core::Product& product) {
        if (!addRow()) {
            return false;
        }
        bindRow(itemsTable->rowCount() - 1, product);
        return true;
    };

    // The whole dialog speaks two sentences: what a scan finds, and what a name
    // finds. A hit lands as a new line, the field empties, and the field keeps
    // the caret so the next scan goes straight in.
    const auto lookupTyped = [&](QLineEdit* field) {
        const QString text = field->text().trimmed();
        if (text.isEmpty()) {
            return;
        }
        const std::vector<core::Product> matches = matchesFor(text);
        if (matches.empty()) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Produit introuvable"));
            return;
        }
        const std::optional<core::Product> chosen =
            matches.size() == 1 ? std::optional<core::Product>(matches.front())
                                : askWhich(field, matches);
        if (!chosen.has_value() || !addMatch(*chosen)) {
            return;
        }
        field->clear();
        field->setFocus();
    };

    QObject::connect(searchField, &QLineEdit::textChanged, &dialog, [searchField](const QString& text) {
        // An AZERTY scanner types the number row as symbols; put the digits back.
        const QString normalized = core::normalizeScannedBarcode(text);
        if (normalized == text) {
            return;
        }
        const QSignalBlocker blocker(searchField);
        searchField->setText(normalized);
        searchField->setCursorPosition(normalized.length());
    });
    QObject::connect(searchField, &QLineEdit::returnPressed, &dialog,
                     [&, searchField]() { lookupTyped(searchField); });
    // A click on the product column is a request to search from that line, so the
    // text is selected and typing replaces it.
    QObject::connect(itemsTable, &QTableWidget::cellClicked, &dialog, [&](int row, int column) {
        if (column != kColProduct) {
            return;
        }
        if (auto* field = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColProduct))) {
            field->setFocus();
            field->selectAll();
        }
    });
    QObject::connect(addLineButton, &QPushButton::clicked, &dialog, [&]() {
        if (addRow()) {
            const int row = itemsTable->rowCount() - 1;
            if (auto* field = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColProduct))) {
                field->setFocus();
            }
        }
    });

    QObject::connect(vatField, &QLineEdit::editingFinished, &dialog, [&]() { recompute(); });
    QObject::connect(paidField, &QLineEdit::editingFinished, &dialog, [&]() { recompute(); });
    QObject::connect(paidAll, &QCheckBox::toggled, &dialog, [&](bool on) {
        paidField->setReadOnly(on);
        recompute();
    });
    paidField->setReadOnly(paidAll->isChecked());

    // ---- save ----
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const int chosenSupplier = supplierCombo->currentData().toInt();
        if (chosenSupplier <= 0) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("Choisissez un fournisseur"));
            return;
        }
        if (itemsTable->rowCount() == 0) {
            QMessageBox::warning(&dialog, tr("Erreur"),
                                 tr("Ajoutez au moins un produit à la facture"));
            return;
        }

        QVector<core::PurchaseItem> lines;
        lines.reserve(itemsTable->rowCount());
        long long subtotal = 0;
        for (int row = 0; row < itemsTable->rowCount(); ++row) {
            const int number = row + 1;
            const auto* field = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColProduct));
            const auto* qty = qobject_cast<QSpinBox*>(itemsTable->cellWidget(row, kColQuantity));
            const auto* price = qobject_cast<QLineEdit*>(itemsTable->cellWidget(row, kColPrice));
            const int productId = field ? field->property("productId").toInt() : 0;

            // A field edited after the product was bound would save a name that
            // belongs to a different article, so the two are compared before the
            // line is trusted.
            const std::optional<core::Product> product =
                productId > 0 ? products.findById(productId) : std::nullopt;
            if (!product.has_value() || !field || field->text().trimmed() != product->name) {
                QMessageBox::warning(&dialog, tr("Erreur"),
                                     tr("Ligne %1 : produit introuvable").arg(number));
                return;
            }
            if (!qty || qty->value() <= 0) {
                QMessageBox::warning(&dialog, tr("Erreur"),
                                     tr("Ligne %1 : quantité invalide").arg(number));
                return;
            }
            const std::optional<long long> unitPrice =
                price ? parseMoney(price->text()) : std::nullopt;
            if (!unitPrice.has_value() || *unitPrice <= 0) {
                QMessageBox::warning(&dialog, tr("Erreur"),
                                     tr("Ligne %1 : prix unitaire obligatoire").arg(number));
                return;
            }

            core::PurchaseItem line;
            line.productId = product->id;
            line.description = product->name;
            line.quantity = qty->value();
            line.unit = product->unit.isEmpty() ? QStringLiteral("piece") : product->unit;
            line.unitPriceCents = *unitPrice;
            line.totalCents = line.quantity * line.unitPriceCents;
            subtotal += line.totalCents;
            lines.append(line);
        }

        const long long vat = parseMoney(vatField->text()).value_or(0);
        const long long total = subtotal + vat;
        const long long paid = paidAll->isChecked() ? total : parseMoney(paidField->text()).value_or(0);

        core::Purchase purchase;
        purchase.supplierId = chosenSupplier;
        purchase.invoiceNumber = invoiceNumber->text().trimmed();
        // The date is the operator's; the time of day is not part of what they
        // picked, so today's clock is carried over to keep the stamp sortable.
        purchase.purchasedAt =
            data::toIso(QDateTime(dateEdit->date(), QDateTime::currentDateTime().time()));
        purchase.subtotalCents = subtotal;
        purchase.vatCents = vat;
        purchase.totalCents = total;
        purchase.paidCents = paid;
        purchase.addToStock = addToStock->isChecked();
        purchase.note = noteField->toPlainText().trimmed();

        data::PurchaseRepository purchases(db);
        data::PurchaseItemRepository purchaseItems(db);
        data::StockMovementRepository stockMovements(db);
        data::SupplierPaymentRepository supplierPayments(db);
        data::PurchaseService service(db, purchases, purchaseItems, products, stockMovements,
                                      suppliers, supplierPayments);
        const data::PurchaseResult recorded = service.recordPurchase(purchase, lines);
        if (!recorded.ok) {
            QMessageBox::warning(&dialog, tr("Erreur"),
                                 tr("Enregistrement impossible : %1").arg(recorded.error));
            return;
        }
        result.saved = true;
        result.purchaseId = recorded.purchaseId;
        dialog.accept();
    });

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    addRow();
    recompute();

    dialog.exec();
    return result;
}

} // namespace app::ui
