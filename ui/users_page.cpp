#include "users_page.h"

#include <QIcon>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "scan_safe_dialog.h"
#include "core/user.h"
#include "data/user_repository.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "theme_tokens.h"
#include "format_utils.h"

namespace app::ui {

namespace {

std::optional<core::User> userDialog(QWidget* parent, bool forNew, const core::User& initial)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QCoreApplication::translate("app::ui::UsersPage", "إضافة كاشير") : QCoreApplication::translate("app::ui::UsersPage", "تعديل المستخدم"));
    dialog.setModal(true);
    // The dialog inherits the direction from the application, so it follows the
    // stored language instead of being pinned to Arabic.

    auto* name = new QLineEdit(initial.name);
    name->setPlaceholderText(QCoreApplication::translate("app::ui::UsersPage", "الاسم"));
    auto* pin = new QLineEdit(initial.pin);
    pin->setPlaceholderText(QCoreApplication::translate("app::ui::UsersPage", "PIN (حرفان)"));
    pin->setMaxLength(2);
    pin->setEchoMode(QLineEdit::Password);
    pin->setAlignment(Qt::AlignCenter);
    pin->setFont(QFont(QStringLiteral("monospace"), 20));
    if (!initial.pin.isEmpty()) {
        // The field caps at two characters, so the existing value has to be
        // selected or typing would append and be rejected.
        pin->selectAll();
    }
    auto* confirm = new QLineEdit;
    confirm->setPlaceholderText(QCoreApplication::translate("app::ui::UsersPage", "تأكيد PIN"));
    confirm->setMaxLength(2);
    confirm->setEchoMode(QLineEdit::Password);
    confirm->setAlignment(Qt::AlignCenter);
    confirm->setFont(QFont(QStringLiteral("monospace"), 20));
    auto* role = new QComboBox;
    role->addItem(QStringLiteral("cashier"), QStringLiteral("cashier"));
    role->addItem(QStringLiteral("admin"), QStringLiteral("admin"));
    role->setCurrentText(initial.role);
    auto* active = new QCheckBox;
    active->setChecked(initial.active);

    QFormLayout* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("app::ui::UsersPage", "الاسم"), name);
    form->addRow(QCoreApplication::translate("app::ui::UsersPage", "PIN"), pin);
    form->addRow(QCoreApplication::translate("app::ui::UsersPage", "تأكيد PIN"), confirm);
    form->addRow(QCoreApplication::translate("app::ui::UsersPage", "الدور"), role);
    form->addRow(QCoreApplication::translate("app::ui::UsersPage", "نشط"), active);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    okBtn->setText(QStringLiteral("OK"));
    cancelBtn->setText(QStringLiteral("Annuler"));
    okBtn->setIcon(QIcon());
    cancelBtn->setIcon(QIcon());
    for (QAbstractButton* b : buttons->buttons()) {
        if (auto* pb = qobject_cast<QPushButton*>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    }
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
    if (pin->text().length() != 2) {
        return std::nullopt;
    }
    if (pin->text() != confirm->text()) {
        return std::nullopt;
    }

    core::User user = initial;
    user.name = name->text().trimmed();
    user.pin = pin->text();
    user.role = role->currentData().toString();
    user.active = active->isChecked();
    return user;
}

} // namespace

UsersPage::UsersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* header = new PageHeader(
        tr("إدارة المستخدمين"),
        tr("إدارة حسابات الكاشيرين وصلاحياتهم"));

    m_add = new QPushButton(tr("إضافة كاشير"));
    m_add->setObjectName(QStringLiteral("primary"));
    m_add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    connect(m_add, &QPushButton::clicked, this, &UsersPage::onAddClicked);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(header, 1);
    headerRow->addWidget(m_add, 0, Qt::AlignLeft);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("usersTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("الاسم"), tr("الدور"), tr("PIN"), tr("نشط"), tr("إجراءات")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The actions column is sized to its own buttons in rebuildTable() rather
    // than left to share whatever the window has left over, so stretchLastSection
    // stays off: with it on the last section is stretched and the header ignores
    // the width set for it.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(themeTokens::space12, themeTokens::space12,
                                   themeTokens::space12, themeTokens::space12);
    mainLayout->setSpacing(themeTokens::space12);
    mainLayout->addLayout(headerRow);
    mainLayout->addWidget(m_table, 1);

    refresh();
}

void UsersPage::refresh()
{
    rebuildTable();
}

int UsersPage::rowCount() const
{
    return m_table->rowCount();
}

void UsersPage::rebuildTable()
{
    data::UserRepository repo(m_db);
    const auto users = repo.listAll();

    m_table->setRowCount(0);
    m_table->setRowCount(static_cast<int>(users.size()));

    for (int row = 0; row < static_cast<int>(users.size()); ++row) {
        const core::User& user = users[row];

        auto* nameItem = new QTableWidgetItem(user.name);
        nameItem->setData(Qt::UserRole, user.id);
        m_table->setItem(row, 0, nameItem);

        auto* roleItem = new QTableWidgetItem(user.role);
        roleItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 1, roleItem);

        auto* pinItem = new QTableWidgetItem(user.pin);
        pinItem->setTextAlignment(Qt::AlignCenter);
        pinItem->setFont(QFont(QStringLiteral("monospace"), 14));
        m_table->setItem(row, 2, pinItem);

        auto* checkBox = new QCheckBox;
        checkBox->setChecked(user.active);
        checkBox->setProperty("userId", user.id);
        connect(checkBox, &QCheckBox::stateChanged, this, [this, row](int state) {
            onActiveChanged(row, state);
        });
        m_table->setCellWidget(row, 3, checkBox);

        auto* actionsWidget = new QWidget;
        auto* actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(4, 2, 4, 2);
        actionsLayout->setSpacing(6);

        // No width is set on either button, and that is the fix rather than an
        // omission: the themes pad a button by 21px a side, so "تعديل" needs
        // 87px of button to show its 42px of text and "حذف" needs 81. The 70
        // that was here left 26px for the label, which Qt elided to nothing --
        // the red button had no readable text. Sizing to the label lets the
        // ResizeToContents section be as wide as the buttons actually are, and
        // the themes scale the padding down inside this table (see the
        // #usersTable rules) so both fit a 42px row.
        auto* editBtn = new QPushButton(tr("تعديل"));
        editBtn->setObjectName(QStringLiteral("secondary"));
        editBtn->setProperty("userId", user.id);
        connect(editBtn, &QPushButton::clicked, this, [this, row]() {
            onEditClicked(row);
        });

        auto* removeBtn = new QPushButton(tr("حذف"));
        removeBtn->setObjectName(QStringLiteral("danger"));
        removeBtn->setProperty("userId", user.id);
        connect(removeBtn, &QPushButton::clicked, this, [this, row]() {
            onRemoveClicked(row);
        });

        actionsLayout->addWidget(editBtn);
        actionsLayout->addWidget(removeBtn);
        actionsLayout->addStretch(1);
        // A stylesheet button's size hint is the text, but a cell widget is not
        // what ResizeToContents measures -- it measures the item delegate. So the
        // column can land narrower than the buttons and the labels elide instead
        // of the cell growing. Holding the buttons at their own size hint makes
        // that impossible for any translation, not just the two shipped.
        for (QPushButton* b : {editBtn, removeBtn}) {
            b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }

        m_table->setCellWidget(row, 4, actionsWidget);
    }

    // Size the actions section to what the buttons need, and to nothing else.
    //
    // It used to be the last section with stretchLastSection on, which meant its
    // width came from the window rather than from its contents: the header
    // measures the item delegate for a ResizeToContents section, and a cell
    // widget is not the delegate, so sizeHintForColumn(4) was 0 and the column
    // was in effect stretched. At the window's 900px minimum the section came out
    // 196px against the 217px the two buttons need, the cell widget could not grow
    // past it, and the row clipped the buttons to 77px -- "Modifier" and
    // "Supprimer" rendered as half a word. Measuring the buttons is also what
    // makes this survive a translation longer than the two shipped, which a
    // hardcoded width would not.
    //
    // 120px is the floor for a table with no rows yet, where there are no buttons
    // to measure; below that the header text itself starts to elide.
    //
    // The themes' QTableWidget::item rule carries 10px 14px of padding for the
    // text columns, and QTableView insets a cell widget's rect by that same
    // horizontal padding -- 28px of the section is gone before the layout inside
    // the cell ever sees it. Measured: a 217px section handed the cell widget
    // 188px. The padding cannot be waived for cell widgets from the stylesheet,
    // so it is added back to the width here instead, and the number is the
    // stylesheet's rather than a figure of its own.
    constexpr int kItemPaddingX = 14;
    // The section is one pixel wider than the rect the view hands the cell: the
    // grid draws its line on the section's trailing edge, so of a 245px section
    // the cell rect is 244px and the cell widget gets 216 of it.
    constexpr int kGridLine = 1;
    int actionsWidth = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto* cell = m_table->cellWidget(row, 4)) {
            int buttons = 0;
            for (const auto* b : cell->findChildren<QPushButton*>()) {
                buttons += b->sizeHint().width();
            }
            // +6 for the spacing between the two buttons, +8 for the layout's 4px
            // margins either side of them.
            actionsWidth = qMax(actionsWidth, buttons + 6 + 8);
        }
    }
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(
        4, qMax(actionsWidth + 2 * kItemPaddingX + kGridLine, 120));
}

void UsersPage::onAddClicked()
{
    core::User newUser;
    newUser.role = QStringLiteral("cashier");
    newUser.active = true;

    const auto result = userDialog(this, true, newUser);
    if (!result.has_value()) {
        return;
    }

    data::UserRepository repo(m_db);
    const int id = repo.save(*result);
    if (id <= 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("فشل إنشاء المستخدم"));
        return;
    }
    if (!repo.savePin(id, result->pin)) {
        QMessageBox::warning(this, tr("خطأ"), tr("فشل حفظ PIN"));
        return;
    }

    refresh();
}

void UsersPage::onEditClicked(int row)
{
    auto* item = m_table->item(row, 0);
    if (!item) {
        return;
    }
    const int userId = item->data(Qt::UserRole).toInt();
    if (userId <= 0) {
        return;
    }

    data::UserRepository repo(m_db);
    const auto user = repo.findById(userId);
    if (!user.has_value()) {
        return;
    }

    const auto result = userDialog(this, false, *user);
    if (!result.has_value()) {
        return;
    }

    core::User updated = *user;
    updated.name = result->name;
    updated.role = result->role;
    updated.active = result->active;

    if (repo.save(updated) <= 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("فشل تحديث المستخدم"));
        return;
    }
    if (!result->pin.isEmpty() && result->pin != user->pin) {
        if (!repo.savePin(userId, result->pin)) {
            QMessageBox::warning(this, tr("خطأ"), tr("فشل تحديث PIN"));
            return;
        }
    }

    refresh();
}

void UsersPage::onRemoveClicked(int row)
{
    auto* item = m_table->item(row, 0);
    if (!item) {
        return;
    }
    const int userId = item->data(Qt::UserRole).toInt();
    if (userId <= 0) {
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        tr("تأكيد الحذف"),
        tr("هل أنت متأكد من حذف هذا المستخدم؟"),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    data::UserRepository repo(m_db);
    if (!repo.remove(userId)) {
        QMessageBox::warning(this, tr("خطأ"), tr("فشل حذف المستخدم"));
        return;
    }

    refresh();
}

void UsersPage::onActiveChanged(int row, int state)
{
    auto* item = m_table->item(row, 0);
    if (!item) {
        return;
    }
    const int userId = item->data(Qt::UserRole).toInt();
    if (userId <= 0) {
        return;
    }

    data::UserRepository repo(m_db);
    if (!repo.setActive(userId, state == Qt::Checked)) {
        QMessageBox::warning(this, tr("خطأ"), tr("فشل تحديث حالة المستخدم"));
        rebuildTable();
    }
}

} // namespace app::ui