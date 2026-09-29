#include "product_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "data/product_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

std::optional<core::Product> showProductDialog(QWidget* parent, app::data::Database& db,
                                               const core::Product& initial)
{
    const bool forNew = initial.id == 0;

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QCoreApplication::translate("ProductDialog", "منتج جديد") : QCoreApplication::translate("ProductDialog", "تعديل المنتج"));
    dialog.setModal(true);

    auto* barcode = new QLineEdit(initial.barcode);
    auto* name = new QLineEdit(initial.name);
    auto* cost = new QLineEdit(initial.costPriceCents ? formatMoney(initial.costPriceCents) : QString());
    auto* sale = new QLineEdit(initial.salePriceCents ? formatMoney(initial.salePriceCents) : QString());
    auto* unit = new QLineEdit(initial.unit.isEmpty() ? QString() : initial.unit);
    auto* package = new QSpinBox;
    package->setRange(1, 1000000);
    package->setValue(initial.packageSize ? initial.packageSize : 1);
    auto* active = new QCheckBox;
    active->setChecked(forNew || initial.active);

    QFormLayout* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("ProductDialog", "الباركود"), barcode);
    form->addRow(QCoreApplication::translate("ProductDialog", "الاسم"), name);
    form->addRow(QCoreApplication::translate("ProductDialog", "سعر التكلفة"), cost);
    form->addRow(QCoreApplication::translate("ProductDialog", "سعر البيع"), sale);
    form->addRow(QCoreApplication::translate("ProductDialog", "الوحدة"), unit);
    form->addRow(QCoreApplication::translate("ProductDialog", "المحتوى (عدد وحدات الوجبة)"), package);
    form->addRow(QCoreApplication::translate("ProductDialog", "مُفعّل"), active);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    okBtn->setText(QStringLiteral("OK"));
    cancelBtn->setText(QStringLiteral("Annuler"));
    okBtn->setIcon(QIcon());
    cancelBtn->setIcon(QIcon());
    // A barcode scanner appends Enter to every scan, so no button may claim the
    // default action: Enter must walk the form instead of saving and closing.
    for (QAbstractButton* b : buttons->buttons()) {
        if (auto* pb = qobject_cast<QPushButton*>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    }
    // The scanner's Enter walks the form: barcode -> name, it must not submit.
    QObject::connect(barcode, &QLineEdit::returnPressed, &dialog, [name]() {
        name->setFocus();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Validation runs before accept(), not after exec(): a rejected field has to
    // leave the form on screen with what was typed still in it.
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const auto saleCents = parseMoney(sale->text());
        if (!saleCents || *saleCents <= 0) {
            QMessageBox::warning(&dialog, QCoreApplication::translate("ProductDialog", "خطأ"), QCoreApplication::translate("ProductDialog", "سعر البيع مطلوب ويجب أن يكون أكبر من صفر"));
            sale->setFocus();
            sale->selectAll();
            return;
        }

        // products.barcode is UNIQUE, so saving over a live code fails and the
        // typed-in values would be lost. Caught here, while the form is still
        // open, instead of at the repository.
        const QString typedBarcode = barcode->text().trimmed();
        if (!typedBarcode.isEmpty()) {
            const auto clash = data::ProductRepository(db).findByBarcode(typedBarcode);
            if (clash && clash->id != initial.id) {
                QMessageBox::warning(&dialog, QCoreApplication::translate("ProductDialog", "خطأ"), QCoreApplication::translate("ProductDialog", "الباركود مستخدم مسبقاً"));
                barcode->setFocus();
                barcode->selectAll();
                return;
            }
        }
        dialog.accept();
    });

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    // A blank cost means "not recorded", not zero filled in by mistake, so the
    // field is allowed to sit empty and lands as 0.
    long long costCents = 0;
    if (!cost->text().trimmed().isEmpty()) {
        const auto parsed = parseMoney(cost->text());
        if (!parsed || *parsed < 0) {
            return std::nullopt;
        }
        costCents = *parsed;
    }

    const QString typedUnit = unit->text().trimmed();
    core::Product product = initial;
    product.barcode = barcode->text().trimmed();
    product.name = name->text().trimmed();
    product.costPriceCents = costCents;
    product.salePriceCents = *parseMoney(sale->text());
    product.unit = typedUnit.isEmpty() ? QStringLiteral("piece") : typedUnit;
    product.packageSize = package->value();
    product.active = active->isChecked();
    return product;
}

} // namespace app::ui
