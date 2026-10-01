#include "product_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/barcode_utils.h"
#include "data/product_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"

namespace app::ui {

namespace {

// A product is "nameless" when its name holds no Unicode letter at all -- the
// rows an import from another till leaves behind, like "12345" or "---". The same
// test the products list uses to hide them (ProductRepository::Visibility), kept
// here as its own copy because the dialog is not to know how the list reads the
// table: what matters here is only whether the typed-in code already belongs to
// one of those rows, so the cashier is told what it would overwrite.
//
// \p{L} rather than [A-Za-z] on purpose. An ASCII test would call every Arabic
// name nameless, which is the whole catalogue.
bool isNameless(const QString& name)
{
    static const QRegularExpression anyLetter(QStringLiteral("[\\p{L}]"));
    return !anyLetter.match(name).hasMatch();
}

// The panel that reports a nameless match. Built here and hidden; it is a plain
// QFrame because that is what the #stockNotification rule in the two themes
// selects on, and the frame is what carries the background and the border.
QFrame* buildNotice(QWidget* parent, QPushButton** fillOut)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("stockNotification"));
    frame->setFrameShape(QFrame::NoFrame);

    auto* rows = new QVBoxLayout(frame);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(6);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);

    auto* title = new QLabel(QCoreApplication::translate("ProductDialog", "⚠️  Produit existant sans nom"));
    title->setObjectName(QStringLiteral("noticeTitle"));
    head->addWidget(title);
    head->addStretch(1);

    // The dismissal sits inside the panel rather than in the dialog's button
    // box: it hides the notice and leaves the form and its typed-in values
    // exactly as they are, which is not what Cancel or OK would do.
    auto* close = new QPushButton(QCoreApplication::translate("ProductDialog", "×"));
    close->setObjectName(QStringLiteral("noticeClose"));
    close->setAutoDefault(false);
    close->setDefault(false);
    close->setFixedSize(26, 26);
    head->addWidget(close);

    auto* details = new QLabel;
    details->setObjectName(QStringLiteral("noticeBody"));
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->setWordWrap(true);

    auto* fill = new QPushButton(QCoreApplication::translate("ProductDialog", "Remplir les champs"));
    fill->setObjectName(QStringLiteral("secondary"));
    fill->setAutoDefault(false);
    fill->setDefault(false);

    rows->addLayout(head);
    rows->addWidget(details);
    rows->addWidget(fill, 0, Qt::AlignLeft);

    *fillOut = fill;

    // Hidden from the first paint, and taken out of the layout's height budget
    // while hidden, so an untouched form looks exactly as it did before.
    frame->setVisible(false);

    QObject::connect(close, &QPushButton::clicked, frame, [frame] { frame->setVisible(false); });

    return frame;
}

} // namespace

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

    // The id of the nameless product that the typed-in barcode belongs to, once
    // the cashier chooses to fill the form from it. Zero means "nothing adopted",
    // and the product the dialog returns is then a new one.
    //
    // This is the whole of the update-versus-create decision, and it works
    // because every caller passes the returned product straight to
    // ProductRepository::save(), which writes an UPDATE for a non-zero id and an
    // INSERT for id 0. So the dialog hands back a product that already carries the
    // id of the row the notice was about, and the existing row is amended rather
    // than a second product being created beside it. The callers need no change
    // and no new return value to express it.
    int adoptedId = 0;

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
    QObject::connect(barcode, &QLineEdit::textChanged, &dialog, [barcode](const QString& text) {
        // An AZERTY scanner types the number row as symbols; put the digits back.
        const QString normalized = core::normalizeScannedBarcode(text);
        if (normalized == text) {
            return;
        }
        const QSignalBlocker blocker(barcode);
        barcode->setText(normalized);
        barcode->setCursorPosition(normalized.length());
    });
    QObject::connect(barcode, &QLineEdit::returnPressed, &dialog, [name]() {
        name->setFocus();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QPushButton* fillFields = nullptr;
    QFrame* notice = buildNotice(&dialog, &fillFields);
    auto* noticeBody = notice->findChild<QLabel*>(QStringLiteral("noticeBody"));

    // Re-checked on every keystroke in the barcode field, which is also what makes
    // the AZERTY normalisation above matter here: the code is looked up after it
    // has been rewritten to digits, so the panel does not appear against a
    // half-normalised code and then vanish.
    //
    // The lookup only reports. It never edits a field and never saves: a scan that
    // lands on a nameless row tells the cashier what is already there and leaves
    // the decision to them, because they may be typing a new product whose code
    // merely collides, and only they know which it is.
    const auto refreshNotice = [barcode, notice, noticeBody, &db, &initial, &adoptedId]() {
        const QString code = barcode->text().trimmed();
        if (code.isEmpty()) {
            notice->setVisible(false);
            adoptedId = 0;
            return;
        }
        const auto clash = data::ProductRepository(db).findByBarcode(code);
        // Four cases, and only the last shows the panel:
        //   - no such code: an ordinary new product
        //   - the code is the row already being edited: that is not a clash
        //   - the code belongs to a product that has a real name: an ordinary clash,
        //     which the OK handler below reports the way it always has
        if (!clash || clash->id == initial.id || !isNameless(clash->name)) {
            notice->setVisible(false);
            adoptedId = 0;
            return;
        }
        adoptedId = clash->id;
        noticeBody->setText(QCoreApplication::translate(
                                "ProductDialog",
                                "Code-barres : %1\nPrix de vente : %2\nPrix revient : %3\nStock : %4")
                                .arg(clash->barcode, formatMoney(clash->salePriceCents),
                                     formatMoney(clash->costPriceCents))
                                .arg(clash->quantity));
        notice->setVisible(true);
    };
    QObject::connect(barcode, &QLineEdit::textChanged, &dialog, refreshNotice);
    // Shown for a form opened on a nameless row too, not only for one typed in:
    // opening a product with no name is exactly when the cashier wants to be told.
    refreshNotice();

    // Filling the form copies the values across and adopts the row's id, so
    // pressing OK afterwards amends that row rather than creating a second product
    // beside it. The barcode is left as typed: it is the code just scanned, and
    // re-typing the same string would only risk the AZERTY rewrite running again
    // on the way in.
    QObject::connect(fillFields, &QPushButton::clicked, &dialog, [barcode, name, cost, sale, unit,
                                                                     package, notice, &db]() {
        const auto found = data::ProductRepository(db).findByBarcode(barcode->text().trimmed());
        if (!found) {
            notice->setVisible(false);
            return;
        }
        name->setText(found->name);
        // An unrecorded cost is left blank rather than shown as 0.00, so filling
        // the form cannot be read as "this product costs nothing".
        cost->setText(found->costPriceCents ? formatMoney(found->costPriceCents) : QString());
        sale->setText(formatMoney(found->salePriceCents));
        unit->setText(found->unit);
        package->setValue(found->packageSize ? found->packageSize : 1);
        notice->setVisible(false);
    });

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
            // The row this save is going to write to is not always initial.id: once
            // the cashier has filled the form from a nameless row, it is that row's
            // id. Comparing against initial.id alone would then report the code as
            // taken by "another" product -- the very row being amended -- and block
            // the save the fill button exists to make possible.
            const int targetId = adoptedId ? adoptedId : initial.id;
            if (clash && clash->id != targetId) {
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
    // Above the buttons, so the panel reads as belonging to the form it describes
    // and not to the dialog's actions. Hidden by default, and a hidden widget takes
    // no space in a QVBoxLayout, so a form that never shows it is unchanged.
    layout->addWidget(notice);
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
    // The update-versus-create decision, in one line: a filled form carries the id
    // of the row it was filled from, and every caller hands this straight to
    // ProductRepository::save(), which UPDATEs a non-zero id and INSERTs a zero
    // one. Nothing else in the dialog has to know which case this is.
    product.id = adoptedId ? adoptedId : initial.id;
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
