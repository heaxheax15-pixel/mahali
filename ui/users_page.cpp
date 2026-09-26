#include "users_page.h"

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
#include "format_utils.h"

namespace app::ui {

namespace {

std::optional<core::User> userDialog(QWidget* parent, bool forNew, const core::User& initial)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QCoreApplication::translate("app::ui::UsersPage", "إضافة كاشير") : QCoreApplication::translate("app::ui::UsersPage", "تعديل المستخدم"));
    dialog.setModal(true);
    dialog.setLayoutDirection(Qt::RightToLeft);

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
    m_add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    connect(m_add, &QPushButton::clicked, this, &UsersPage::onAddClicked);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(header, 1);
    headerRow->addWidget(m_add, 0, Qt::AlignLeft);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("usersTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("الاسم"), tr("الدور"), tr("PIN"), tr("نشط"), tr("إجراءات")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 24);
    mainLayout->setSpacing(12);
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
        checkBox->setStyleSheet(QStringLiteral("QCheckBox::indicator { width: 18px; height: 18px; }"));
        connect(checkBox, &QCheckBox::stateChanged, this, [this, row](int state) {
            onActiveChanged(row, state);
        });
        m_table->setCellWidget(row, 3, checkBox);

        auto* actionsWidget = new QWidget;
        auto* actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(4, 2, 4, 2);
        actionsLayout->setSpacing(6);

        auto* editBtn = new QPushButton(tr("تعديل"));
        editBtn->setObjectName(QStringLiteral("secondary"));
        editBtn->setFixedWidth(70);
        editBtn->setProperty("userId", user.id);
        connect(editBtn, &QPushButton::clicked, this, [this, row]() {
            onEditClicked(row);
        });

        auto* removeBtn = new QPushButton(tr("حذف"));
        removeBtn->setObjectName(QStringLiteral("danger"));
        removeBtn->setFixedWidth(70);
        removeBtn->setProperty("userId", user.id);
        connect(removeBtn, &QPushButton::clicked, this, [this, row]() {
            onRemoveClicked(row);
        });

        actionsLayout->addWidget(editBtn);
        actionsLayout->addWidget(removeBtn);
        actionsLayout->addStretch(1);

        m_table->setCellWidget(row, 4, actionsWidget);
    }
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