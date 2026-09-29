#include "supplier_payment_dialog.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/purchase.h"
#include "data/date_utils.h"
#include "data/purchase_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_payment_service.h"
#include "data/supplier_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("SupplierPaymentDialog", text);
}

// A scanner presses Return wherever it is standing, so no button here may claim
// the default action: Enter must walk the form instead of recording a payment
// with half the amount typed.
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

SupplierPaymentDialogResult showSupplierPaymentDialog(QWidget* parent, app::data::Database& db,
                                                      int supplierId, std::optional<int> purchaseId)
{
    SupplierPaymentDialogResult result;

    data::SupplierRepository suppliers(db);
    data::PurchaseRepository purchases(db);
    data::SupplierPaymentRepository paymentRows(db);
    data::SupplierPaymentService service(db, paymentRows, suppliers, purchases);

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("Nouveau paiement"));
    dialog.setModal(true);
    dialog.resize(500, 400);

    // What the shop currently owes this supplier. Shown up front because it is
    // the ceiling on what can be paid, and an operator who cannot see it has no
    // way to know how much to type.
    const long long balance = suppliers.balanceCentsFor(supplierId);
    auto* balanceLabel = new QLabel(tr("Solde actuel : %1").arg(formatMoney(balance)));
    balanceLabel->setObjectName(QStringLiteral("faintText"));

    // The invoices still open, oldest first, and a general payment above them:
    // money that is not against any one invoice is taken off the balance as a
    // whole, which is what the service does when no invoice is chosen.
    auto* invoiceCombo = new QComboBox;
    invoiceCombo->addItem(tr("Aucune (paiement général)"), QVariant());
    int preselect = 0;
    for (const data::SupplierPaymentService::UnpaidInvoice& invoice : service.unpaidInvoicesFor(supplierId)) {
        const core::Purchase& purchase = invoice.purchase;
        const QString label = purchase.invoiceNumber.isEmpty()
                                  ? tr("Facture du %1").arg(
                                        data::fromIso(purchase.purchasedAt).has_value()
                                            ? data::fromIso(purchase.purchasedAt)->date().toString(
                                                  QStringLiteral("dd/MM/yyyy"))
                                            : QString())
                                  : purchase.invoiceNumber;
        invoiceCombo->addItem(tr("%1 — reste %2").arg(label).arg(formatMoney(invoice.remainingCents)),
                              purchase.id);
        // Only an open invoice can be named here. A settled one is not in the
        // list, and paying it would be an advance this list cannot show.
        if (purchaseId.has_value() && *purchaseId == purchase.id) {
            preselect = invoiceCombo->count() - 1;
        }
    }
    invoiceCombo->setCurrentIndex(preselect);

    // No validator: parseMoney already accepts "1234,50", "1 234.50", Arabic
    // digits and a trailing currency symbol, which is what an operator types
    // and what a figure copied out of the ledger looks like. A QDoubleValidator
    // in the C locale would refuse the comma that the rest of the app writes.
    auto* amountField = new QLineEdit;
    amountField->setPlaceholderText(tr("0"));
    // What is left is the largest useful figure, so it is offered up front
    // instead of being retyped.
    if (balance > 0) {
        amountField->setPlaceholderText(tr("Reste : %1").arg(formatMoney(balance)));
    }

    auto* dateEdit = new QDateEdit;
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    dateEdit->setDate(QDate::currentDate());

    auto* noteField = new QLineEdit;

    // Refusals land here rather than in a dialog box: a rejected amount is
    // something to correct in place, and a modal box would hide the field that
    // has to change.
    auto* errorLabel = new QLabel;
    errorLabel->setObjectName(QStringLiteral("noticeErr"));
    errorLabel->setWordWrap(true);
    errorLabel->hide();

    const auto fail = [&errorLabel](const QString& message) {
        errorLabel->setText(message);
        errorLabel->show();
    };

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->addRow(balanceLabel);
    form->addRow(tr("Facture liée"), invoiceCombo);
    form->addRow(tr("Montant"), amountField);
    form->addRow(tr("Date"), dateEdit);
    form->addRow(tr("Note"), noteField);

    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    card->setLayout(form);

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
    root->addWidget(card, 1);
    root->addWidget(errorLabel);
    root->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        errorLabel->hide();

        const std::optional<long long> amount = parseMoney(amountField->text());
        if (!amount.has_value() || *amount <= 0) {
            fail(tr("Le montant doit être supérieur à zéro"));
            amountField->setFocus();
            amountField->selectAll();
            return;
        }
        // Paying more than the shop owes would leave the supplier in credit, and
        // the balance would carry that credit as a debt nobody can explain. The
        // service itself does not refuse this, so the form does.
        if (*amount > balance) {
            fail(tr("Le montant dépasse le solde dû (%1)").arg(formatMoney(balance)));
            amountField->setFocus();
            amountField->selectAll();
            return;
        }

        // An invalid QVariant on the combo is the "no invoice" entry, which is
        // what a payment against the balance as a whole is.
        const QVariant chosen = invoiceCombo->currentData();
        const std::optional<int> against = chosen.isValid() ? std::optional<int>(chosen.toInt())
                                                            : std::nullopt;
        // The date is the operator's; the clock of the moment is carried over so
        // the payment sorts the same way an invoice stamped today would.
        const QString paidAt = data::toIso(
            QDateTime(dateEdit->date(), QDateTime::currentDateTime().time()));

        const data::SupplierPaymentResult recorded =
            service.recordPayment(supplierId, against, *amount, paidAt, noteField->text().trimmed());
        if (!recorded.ok) {
            fail(recorded.error);
            return;
        }
        result.saved = true;
        result.paymentId = recorded.paymentId;
        dialog.accept();
    });

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
    return result;
}

} // namespace app::ui
