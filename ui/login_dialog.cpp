#include "login_dialog.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "core/session.h"
#include "data/user_repository.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

LoginDialog::LoginDialog(app::data::Database& db, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
{
    setWindowTitle(QStringLiteral("تسجيل الدخول — محلي"));
    setLayoutDirection(Qt::RightToLeft);
    setModal(true);
    setFixedSize(420, 520);
    setObjectName(QStringLiteral("loginDialog"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 24);
    mainLayout->setSpacing(12);

    // Header area - clickable for recovery mode
    m_headerLabel = new QLabel(QStringLiteral("محلي"));
    m_headerLabel->setObjectName(QStringLiteral("loginHeader"));
    m_headerLabel->setAlignment(Qt::AlignCenter);
    m_headerLabel->setFixedHeight(56);
    m_headerLabel->setCursor(Qt::PointingHandCursor);
    m_headerLabel->installEventFilter(this);
    mainLayout->addWidget(m_headerLabel);

    // Subtitle
    auto* subtitle = new QLabel(QStringLiteral("اختر مستخدمًا وأدخل رمز PIN"));
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    // User list area (scrollable)
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_userListWidget = new QWidget;
    m_userListWidget->setLayout(new QVBoxLayout);
    m_userListWidget->layout()->setContentsMargins(0, 0, 0, 0);
    m_userListWidget->layout()->setSpacing(8);
    scroll->setWidget(m_userListWidget);
    mainLayout->addWidget(scroll, 1);

    // PIN input area (initially hidden)
    m_pinWidget = new QWidget;
    auto* pinLayout = new QVBoxLayout(m_pinWidget);
    pinLayout->setContentsMargins(0, 0, 0, 0);
    pinLayout->setSpacing(8);

    auto* pinLabel = new QLabel(QStringLiteral("رمز PIN (حرفان)"));
    pinLabel->setObjectName(QStringLiteral("pinLabel"));
    pinLayout->addWidget(pinLabel);

    m_pinInput = new QLineEdit;
    m_pinInput->setObjectName(QStringLiteral("pinInput"));
    m_pinInput->setMaxLength(2);
    m_pinInput->setEchoMode(QLineEdit::Password);
    m_pinInput->setAlignment(Qt::AlignCenter);
    m_pinInput->setFont(QFont(QStringLiteral("monospace"), 24));
    m_pinInput->setFixedHeight(56);
    pinLayout->addWidget(m_pinInput);

    m_errorLabel = new QLabel;
    m_errorLabel->setObjectName(QStringLiteral("loginError"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setVisible(false);
    pinLayout->addWidget(m_errorLabel);

    m_loginButton = new QPushButton(QStringLiteral("دخول"));
    m_loginButton->setObjectName(QStringLiteral("primary"));
    m_loginButton->setFixedHeight(44);
    m_loginButton->setCursor(Qt::PointingHandCursor);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    pinLayout->addWidget(m_loginButton);

    // Recovery input (initially hidden)
    m_recoveryInput = new QLineEdit;
    m_recoveryInput->setObjectName(QStringLiteral("recoveryInput"));
    m_recoveryInput->setPlaceholderText(QStringLiteral("كلمة الاستعادة"));
    m_recoveryInput->setEchoMode(QLineEdit::Password);
    m_recoveryInput->setVisible(false);
    pinLayout->addWidget(m_recoveryInput);

    mainLayout->addWidget(m_pinWidget);

    createDefaultUserIfNeeded();
    buildUserList();
}

void LoginDialog::createDefaultUserIfNeeded()
{
    data::UserRepository repo(m_db);
    if (repo.listActive().isEmpty()) {
        core::User admin;
        admin.name = QStringLiteral("المدير");
        admin.role = QStringLiteral("admin");
        const int id = repo.save(admin);
        if (id > 0) {
            repo.savePin(id, QStringLiteral("00"));
        }
    }
}

void LoginDialog::buildUserList()
{
    data::UserRepository repo(m_db);
    const auto users = repo.listActive();

    auto* layout = qobject_cast<QVBoxLayout*>(m_userListWidget->layout());
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    for (const core::User& user : users) {
        auto* btn = new QPushButton(user.name);
        btn->setObjectName(QStringLiteral("userButton"));
        btn->setProperty("userId", user.id);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(52);
        btn->setFont(QFont(QStringLiteral("system"), 14));
        connect(btn, &QPushButton::clicked, this, [this, user]() {
            onUserButtonClicked(user.id);
        });
        layout->addWidget(btn);
    }
    layout->addStretch(1);
}

void LoginDialog::onUserButtonClicked(int userId)
{
    data::UserRepository repo(m_db);
    const auto user = repo.findById(userId);
    if (!user.has_value()) {
        return;
    }
    m_selectedUserId = userId;
    showPinInput(userId, user->name);
}

void LoginDialog::showPinInput(int userId, const QString& userName)
{
    m_headerLabel->setText(userName);
    m_pinWidget->setVisible(true);
    m_userListWidget->setVisible(false);
    clearPinInput();
    m_pinInput->setFocus();
}

void LoginDialog::clearPinInput()
{
    m_pinInput->clear();
    m_errorLabel->setVisible(false);
    m_errorLabel->clear();
}

void LoginDialog::onLoginClicked()
{
    if (m_selectedUserId <= 0) {
        return;
    }
    const QString pin = m_pinInput->text();
    if (pin.length() != 2) {
        m_errorLabel->setText(QStringLiteral("أدخل حرفين فقط"));
        m_errorLabel->setVisible(true);
        return;
    }

    data::UserRepository repo(m_db);
    const auto user = repo.findByPin(pin);
    if (user.has_value() && user->id == m_selectedUserId && user->active) {
        app::core::Session::instance().setCurrentUser(*user);
        accept();
    } else {
        m_errorLabel->setText(QStringLiteral("PIN خاطئ"));
        m_errorLabel->setVisible(true);
        m_pinInput->clear();
    }
}

void LoginDialog::onHeaderClicked()
{
    m_headerClickCount++;
    if (m_headerClickCount >= 8) {
        m_recoveryInput->setVisible(true);
        m_recoveryInput->setFocus();
    }
}

bool LoginDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_headerLabel && event->type() == QEvent::MouseButtonPress) {
        onHeaderClicked();
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

} // namespace app::ui