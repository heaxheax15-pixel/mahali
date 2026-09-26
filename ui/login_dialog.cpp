#include "login_dialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "core/session.h"
#include "data/admin_secret_repository.h"
#include "data/user_repository.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

LoginDialog::LoginDialog(app::data::Database& db, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
{
    setWindowTitle(tr("تسجيل الدخول — محلي"));
    setLayoutDirection(Qt::RightToLeft);
    setModal(true);
    setObjectName(QStringLiteral("loginDialog"));

    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 24);
    mainLayout->setSpacing(12);

    // Header area - clickable for recovery mode
    m_headerLabel = new QLabel(tr("محلي"));
    m_headerLabel->setObjectName(QStringLiteral("loginHeader"));
    m_headerLabel->setAlignment(Qt::AlignCenter);
    m_headerLabel->setFixedHeight(56);
    m_headerLabel->setCursor(Qt::PointingHandCursor);
    m_headerLabel->installEventFilter(this);
    mainLayout->addWidget(m_headerLabel);

    // Subtitle
    auto* subtitle = new QLabel(tr("اختر مستخدمًا وأدخل رمز PIN"));
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

    auto* pinLabel = new QLabel(tr("رمز PIN (حرفان)"));
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

    m_loginButton = new QPushButton(tr("دخول"));
    m_loginButton->setObjectName(QStringLiteral("primary"));
    m_loginButton->setFixedHeight(44);
    m_loginButton->setCursor(Qt::PointingHandCursor);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    pinLayout->addWidget(m_loginButton);

    // Recovery input (initially hidden)
    m_recoveryInput = new QLineEdit;
    m_recoveryInput->setObjectName(QStringLiteral("recoveryInput"));
    m_recoveryInput->setPlaceholderText(tr("كلمة الاستعادة"));
    m_recoveryInput->setEchoMode(QLineEdit::Password);
    m_recoveryInput->setVisible(false);
    connect(m_recoveryInput, &QLineEdit::returnPressed, this, &LoginDialog::onRecoveryReturnPressed);
    pinLayout->addWidget(m_recoveryInput);

    mainLayout->addWidget(m_pinWidget);

    // Setup mode widget (initially hidden)
    m_setupWidget = new QWidget;
    auto* setupLayout = new QVBoxLayout(m_setupWidget);
    setupLayout->setContentsMargins(0, 0, 0, 0);
    setupLayout->setSpacing(8);

    auto* setupTitle = new QLabel(tr("إعداد المدير الأول"));
    setupTitle->setObjectName(QStringLiteral("setupTitle"));
    setupTitle->setAlignment(Qt::AlignCenter);
    setupLayout->addWidget(setupTitle);

    auto* nameLabel = new QLabel(tr("الاسم"));
    setupLayout->addWidget(nameLabel);
    m_setupNameInput = new QLineEdit;
    m_setupNameInput->setObjectName(QStringLiteral("setupNameInput"));
    m_setupNameInput->setPlaceholderText(tr("مثال: المدير"));
    setupLayout->addWidget(m_setupNameInput);

    auto* pinSetupLabel = new QLabel(tr("رمز PIN (حرفان)"));
    setupLayout->addWidget(pinSetupLabel);
    m_setupPinInput = new QLineEdit;
    m_setupPinInput->setObjectName(QStringLiteral("setupPinInput"));
    m_setupPinInput->setMaxLength(2);
    m_setupPinInput->setEchoMode(QLineEdit::Password);
    m_setupPinInput->setAlignment(Qt::AlignCenter);
    m_setupPinInput->setFont(QFont(QStringLiteral("monospace"), 24));
    m_setupPinInput->setFixedHeight(56);
    setupLayout->addWidget(m_setupPinInput);

    auto* recoverySetupLabel = new QLabel(tr("كلمة الاستعادة (4 أحرف على الأقل)"));
    setupLayout->addWidget(recoverySetupLabel);
    m_setupRecoveryInput = new QLineEdit;
    m_setupRecoveryInput->setObjectName(QStringLiteral("setupRecoveryInput"));
    m_setupRecoveryInput->setEchoMode(QLineEdit::Password);
    m_setupRecoveryInput->setPlaceholderText(tr("كلمة الاستعادة"));
    setupLayout->addWidget(m_setupRecoveryInput);

    auto* confirmLabel = new QLabel(tr("تأكيد كلمة الاستعادة"));
    setupLayout->addWidget(confirmLabel);
    m_setupConfirmInput = new QLineEdit;
    m_setupConfirmInput->setObjectName(QStringLiteral("setupConfirmInput"));
    m_setupConfirmInput->setEchoMode(QLineEdit::Password);
    m_setupConfirmInput->setPlaceholderText(tr("تأكيد كلمة الاستعادة"));
    setupLayout->addWidget(m_setupConfirmInput);

    m_setupErrorLabel = new QLabel;
    m_setupErrorLabel->setObjectName(QStringLiteral("setupError"));
    m_setupErrorLabel->setAlignment(Qt::AlignCenter);
    m_setupErrorLabel->setVisible(false);
    setupLayout->addWidget(m_setupErrorLabel);

    m_setupButton = new QPushButton(tr("إنشاء"));
    m_setupButton->setObjectName(QStringLiteral("primary"));
    m_setupButton->setFixedHeight(44);
    m_setupButton->setCursor(Qt::PointingHandCursor);
    connect(m_setupButton, &QPushButton::clicked, this, &LoginDialog::onSetupClicked);
    setupLayout->addWidget(m_setupButton);

    mainLayout->addWidget(m_setupWidget);

    // Determine mode
    data::UserRepository repo(m_db);
    if (repo.listActive().isEmpty()) {
        showSetupMode();
    } else {
        showLoginMode();
    }
}

void LoginDialog::showSetupMode()
{
    m_setupMode = true;
    m_headerLabel->setText(tr("محلي"));
    m_headerLabel->setCursor(Qt::ArrowCursor);
    m_userListWidget->setVisible(false);
    m_pinWidget->setVisible(false);
    m_setupWidget->setVisible(true);
    clearSetupInputs();
    m_setupNameInput->setFocus();
}

void LoginDialog::showLoginMode()
{
    m_setupMode = false;
    m_headerLabel->setText(tr("محلي"));
    m_headerLabel->setCursor(Qt::PointingHandCursor);
    m_setupWidget->setVisible(false);
    m_userListWidget->setVisible(true);
    m_pinWidget->setVisible(false);
    buildUserList();
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
        // This is a QDialog: without this, Enter in the PIN field would also fire
        // the first user button. The login/setup buttons below stay the default
        // action on purpose — submitting a form with Enter is expected there.
        btn->setAutoDefault(false);
        btn->setDefault(false);
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
    m_userListWidget->setVisible(false);
    m_pinWidget->setVisible(true);
    clearPinInput();
    m_pinInput->setFocus();
}

void LoginDialog::clearPinInput()
{
    m_pinInput->clear();
    m_recoveryInput->clear();
    m_recoveryInput->setVisible(false);
    m_errorLabel->setVisible(false);
    m_errorLabel->clear();
    m_headerClickCount = 0;
}

void LoginDialog::clearSetupInputs()
{
    m_setupNameInput->clear();
    m_setupPinInput->clear();
    m_setupRecoveryInput->clear();
    m_setupConfirmInput->clear();
    m_setupErrorLabel->setVisible(false);
    m_setupErrorLabel->clear();
}

void LoginDialog::onLoginClicked()
{
    if (m_selectedUserId <= 0) {
        return;
    }
    const QString pin = m_pinInput->text();
    if (pin.length() != 2) {
        m_errorLabel->setText(tr("أدخل حرفين فقط"));
        m_errorLabel->setVisible(true);
        return;
    }

    data::UserRepository repo(m_db);
    const auto user = repo.findByPin(pin);
    if (user.has_value() && user->id == m_selectedUserId && user->active) {
        app::core::Session::instance().setCurrentUser(*user);
        accept();
    } else {
        m_errorLabel->setText(tr("PIN خاطئ"));
        m_errorLabel->setVisible(true);
        m_pinInput->clear();
    }
}

void LoginDialog::onSetupClicked()
{
    const QString name = m_setupNameInput->text().trimmed();
    const QString pin = m_setupPinInput->text();
    const QString recovery = m_setupRecoveryInput->text();
    const QString confirm = m_setupConfirmInput->text();

    if (name.isEmpty()) {
        m_setupErrorLabel->setText(tr("أدخل اسماً"));
        m_setupErrorLabel->setVisible(true);
        return;
    }
    if (pin.length() != 2) {
        m_setupErrorLabel->setText(tr("PIN يجب أن يكون حرفين"));
        m_setupErrorLabel->setVisible(true);
        return;
    }
    if (recovery.length() < 4) {
        m_setupErrorLabel->setText(tr("كلمة الاستعادة: 4 أحرف على الأقل"));
        m_setupErrorLabel->setVisible(true);
        return;
    }
    if (recovery != confirm) {
        m_setupErrorLabel->setText(tr("كلمة الاستعادة غير متطابقة"));
        m_setupErrorLabel->setVisible(true);
        return;
    }

    data::UserRepository repo(m_db);
    core::User admin;
    admin.name = name;
    admin.role = QStringLiteral("admin");
    const int id = repo.save(admin);
    if (id <= 0) {
        m_setupErrorLabel->setText(tr("فشل إنشاء المستخدم"));
        m_setupErrorLabel->setVisible(true);
        return;
    }
    if (!repo.savePin(id, pin)) {
        m_setupErrorLabel->setText(tr("فشل حفظ PIN"));
        m_setupErrorLabel->setVisible(true);
        return;
    }

    data::AdminSecretRepository secretRepo(m_db);
    if (!secretRepo.setMaster(id, recovery)) {
        m_setupErrorLabel->setText(tr("فشل حفظ كلمة الاستعادة"));
        m_setupErrorLabel->setVisible(true);
        return;
    }

    const auto user = repo.findById(id);
    if (user.has_value()) {
        app::core::Session::instance().setCurrentUser(*user);
        accept();
    }
}

void LoginDialog::onHeaderClicked()
{
    if (m_setupMode) {
        return;
    }
    m_headerClickCount++;
    if (m_headerClickCount >= 8) {
        // The recovery field lives inside m_pinWidget, which login mode hides.
        // Reveal the container too, otherwise the field can never be shown or
        // focused and the word can never be submitted.
        m_userListWidget->setVisible(false);
        m_pinWidget->setVisible(true);
        m_recoveryInput->setVisible(true);
        m_recoveryInput->setFocus();
        m_recoveryInput->selectAll();
    }
}

void LoginDialog::onRecoveryReturnPressed()
{
    const QString password = m_recoveryInput->text();
    if (password.isEmpty()) {
        return;
    }

    data::AdminSecretRepository secretRepo(m_db);
    const std::optional<int> userId = secretRepo.findAdminByMaster(password);
    if (userId.has_value()) {
        data::UserRepository repo(m_db);
        const auto user = repo.findById(*userId);
        if (user.has_value() && user->active) {
            app::core::Session::instance().setCurrentUser(*user);
            accept();
            return;
        }
    }
    m_errorLabel->setText(tr("كلمة الاستعادة غير صحيحة"));
    m_errorLabel->setVisible(true);
    m_recoveryInput->clear();
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