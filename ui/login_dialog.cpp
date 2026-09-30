#include "login_dialog.h"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "data/admin_secret_repository.h"
#include "data/user_repository.h"
#include "theme.h"
#include "widgets/app_icon.h"

namespace app::ui {

namespace {

// The wordmark and the profile rows are painted by the theme, so the icons we
// put on them have to follow the active theme instead of hard-coding a colour.
QColor accentColor()
{
    return activeTheme() == QLatin1String("dark") ? QColor(QStringLiteral("#d4a017"))
                                                   : QColor(QStringLiteral("#2563eb"));
}

QColor textColor()
{
    return activeTheme() == QLatin1String("dark") ? QColor(QStringLiteral("#fafafa"))
                                                   : QColor(QStringLiteral("#0f172a"));
}

// The frame is fixed, so the fields get a width of their own rather than being
// stretched across all of it.
constexpr int kFieldMaxWidth = 360;

void addLabeledField(QFormLayout* form, const QString& text, QWidget* field)
{
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(label, field);
}

QFormLayout* makeForm()
{
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // Wrapping every row puts the label above its field instead of beside it.
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    return form;
}

// Every screen reports its failure the same way: one line, below the fields,
// and only while there is something to say.
QLabel* makeErrorLabel()
{
    auto* label = new QLabel;
    label->setObjectName(QStringLiteral("loginError"));
    label->setWordWrap(true);
    label->setVisible(false);
    return label;
}

QLineEdit* makePasswordField(const QString& objectName, int maxLength)
{
    auto* field = new QLineEdit;
    field->setObjectName(objectName);
    field->setEchoMode(QLineEdit::Password);
    field->setMaximumWidth(kFieldMaxWidth);
    if (maxLength > 0) {
        field->setMaxLength(maxLength);
    }
    return field;
}

} // namespace

LoginDialog::LoginDialog(app::data::Database& db, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
{
    setObjectName(QStringLiteral("loginDialog"));
    setModal(true);
    // No direction is pinned here. The login gate is reached before anything has
    // set a language of its own, so it inherits the application direction that
    // core::applyLanguage() pinned from the stored language at startup, and a
    // French install gets a left-to-right gate without this having to know.
    // The stylesheet pads the dialog itself, so the root layout adds no margins.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(16);

    mainLayout->addWidget(buildHeader());

    m_errorLabel = makeErrorLabel();

    m_stack = new QStackedWidget;
    mainLayout->addWidget(m_stack, 1);
    // The pages are added in the order of the State enum.
    m_stack->addWidget(buildConnexionPage());
    m_stack->addWidget(buildConfigurationPage());
    m_stack->addWidget(buildRecuperationPage());

    // All three screens are known, so the frame is fixed: a wider dialog would
    // only spread the content over empty space.
    setFixedSize(520, 620);

    // No profile yet means the first administrator still has to be created.
    data::UserRepository repo(m_db);
    goTo(repo.listActive().isEmpty() ? State::Configuration : State::Connexion);
}

QWidget* LoginDialog::buildHeader()
{
    auto* header = new QWidget;
    auto* layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Both the logo and the wordmark answer to the eight-click gesture.
    m_logoLabel = new QLabel;
    m_logoLabel->setAlignment(Qt::AlignCenter);
    m_logoLabel->setPixmap(appIcon(Icon::Shop, accentColor(), 40).pixmap(40, 40));
    m_logoLabel->setCursor(Qt::PointingHandCursor);
    m_logoLabel->installEventFilter(this);
    layout->addWidget(m_logoLabel);

    m_headerLabel = new QLabel(tr("محلي"));
    m_headerLabel->setObjectName(QStringLiteral("loginHeader"));
    m_headerLabel->setAlignment(Qt::AlignCenter);
    m_headerLabel->setCursor(Qt::PointingHandCursor);
    m_headerLabel->installEventFilter(this);
    layout->addWidget(m_headerLabel);

    m_subtitleLabel = new QLabel;
    m_subtitleLabel->setObjectName(QStringLiteral("loginSubtitle"));
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    layout->addWidget(m_subtitleLabel);

    return header;
}

QWidget* LoginDialog::buildConnexionPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // Profiles: the list can grow past the dialog height on a busy till.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_userListWidget = new QWidget;
    auto* listLayout = new QVBoxLayout(m_userListWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    scroll->setWidget(m_userListWidget);
    layout->addWidget(scroll, 1);

    auto* pinLabel = new QLabel(tr("رمز PIN (حرفان)"));
    pinLabel->setObjectName(QStringLiteral("pinLabel"));
    layout->addWidget(pinLabel);

    m_pinInput = makePasswordField(QStringLiteral("pinInput"), 2);
    m_pinInput->setAlignment(Qt::AlignCenter);
    m_pinInput->setFont(QFont(QStringLiteral("monospace"), 24));
    m_pinInput->setEnabled(false);
    layout->addWidget(m_pinInput);

    m_loginButton = new QPushButton(tr("دخول"));
    m_loginButton->setObjectName(QStringLiteral("primary"));
    m_loginButton->setCursor(Qt::PointingHandCursor);
    m_loginButton->setEnabled(false);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    layout->addWidget(m_loginButton);
    m_errorSlots[static_cast<int>(State::Connexion)] = {layout, m_loginButton};

    buildUserList();
    return page;
}

QWidget* LoginDialog::buildConfigurationPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("إعداد المدير الأول"));
    title->setObjectName(QStringLiteral("setupTitle"));
    layout->addWidget(title);

    auto* form = makeForm();

    auto* nameInput = new QLineEdit;
    nameInput->setObjectName(QStringLiteral("setupNameInput"));
    nameInput->setPlaceholderText(tr("مثال: المدير"));
    nameInput->setMaximumWidth(kFieldMaxWidth);
    addLabeledField(form, tr("الاسم"), nameInput);
    m_setupNameInput = nameInput;

    m_setupPinInput = makePasswordField(QStringLiteral("setupPinInput"), 2);
    addLabeledField(form, tr("رمز PIN (حرفان)"), m_setupPinInput);

    // The labels name the fields, so the inputs need no placeholder of their own.
    m_setupRecoveryInput = makePasswordField(QStringLiteral("setupRecoveryInput"), 0);
    addLabeledField(form, tr("كلمة الاستعادة (4 أحرف على الأقل)"), m_setupRecoveryInput);

    m_setupConfirmInput = makePasswordField(QStringLiteral("setupConfirmInput"), 0);
    addLabeledField(form, tr("تأكيد كلمة الاستعادة"), m_setupConfirmInput);

    layout->addLayout(form);

    m_setupButton = new QPushButton(tr("إنشاء"));
    m_setupButton->setObjectName(QStringLiteral("primary"));
    m_setupButton->setCursor(Qt::PointingHandCursor);
    connect(m_setupButton, &QPushButton::clicked, this, &LoginDialog::onSetupClicked);
    layout->addWidget(m_setupButton);
    m_errorSlots[static_cast<int>(State::Configuration)] = {layout, m_setupButton};
    layout->addStretch(1);

    return page;
}

QWidget* LoginDialog::buildRecuperationPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("كلمة الاستعادة"));
    title->setObjectName(QStringLiteral("setupTitle"));
    layout->addWidget(title);

    // One field only, and the title already names it: a label or a placeholder
    // here would just repeat the heading.
    auto* form = makeForm();
    m_recoveryInput = makePasswordField(QStringLiteral("recoveryInput"), 0);
    form->addRow(m_recoveryInput);
    layout->addLayout(form);

    m_recoveryButton = new QPushButton(tr("دخول"));
    m_recoveryButton->setObjectName(QStringLiteral("primary"));
    m_recoveryButton->setCursor(Qt::PointingHandCursor);
    connect(m_recoveryButton, &QPushButton::clicked, this, &LoginDialog::onRecoveryReturnPressed);
    layout->addWidget(m_recoveryButton);
    m_errorSlots[static_cast<int>(State::Recuperation)] = {layout, m_recoveryButton};
    layout->addStretch(1);

    return page;
}

void LoginDialog::goTo(State state)
{
    m_state = state;
    m_stack->setCurrentIndex(static_cast<int>(state));

    // Exactly one button per screen submits on Enter. Qt 6 dropped
    // QDialog::setDefaultButton(), so the default travels with the button:
    // the profile rows are never default, and neither is the primary button of
    // the two screens that are not showing.
    QPushButton* primary = nullptr;
    switch (state) {
    case State::Connexion:
        primary = m_loginButton;
        break;
    case State::Configuration:
        primary = m_setupButton;
        break;
    case State::Recuperation:
        primary = m_recoveryButton;
        break;
    }
    for (QPushButton* button : {m_loginButton, m_setupButton, m_recoveryButton}) {
        const bool isCurrent = button == primary;
        button->setAutoDefault(isCurrent);
        button->setDefault(isCurrent);
    }

    // Move the one error line under the fields of the screen now showing.
    const ErrorSlot& slot = m_errorSlots[static_cast<int>(state)];
    if (slot.layout != nullptr && m_errorLabel->parentWidget() != slot.layout->parentWidget()) {
        if (m_errorLabel->parentWidget() != nullptr) {
            m_errorLabel->parentWidget()->layout()->removeWidget(m_errorLabel);
        }
        slot.layout->insertWidget(slot.layout->indexOf(slot.before), m_errorLabel);
    }

    refreshChrome();
}

void LoginDialog::refreshChrome()
{
    clearError();
    switch (m_state) {
    case State::Connexion: {
        const bool picked = m_selectedUserId > 0;
        setWindowTitle(tr("تسجيل الدخول — محلي"));
        m_headerLabel->setText(picked ? m_selectedUserName : tr("محلي"));
        m_subtitleLabel->setText(tr("اختر مستخدمًا وأدخل رمز PIN"));
        m_subtitleLabel->setVisible(true);
        // No profile picked yet: there is nothing for a PIN to belong to.
        m_pinInput->setEnabled(picked);
        m_loginButton->setEnabled(picked);
        break;
    }
    case State::Configuration:
        setWindowTitle(tr("إعداد المدير الأول"));
        m_headerLabel->setText(tr("محلي"));
        m_subtitleLabel->setVisible(false);
        clearSetupInputs();
        m_setupNameInput->setFocus();
        break;
    case State::Recuperation:
        setWindowTitle(tr("كلمة الاستعادة"));
        m_headerLabel->setText(tr("محلي"));
        m_subtitleLabel->setVisible(false);
        m_recoveryInput->clear();
        m_recoveryInput->setFocus();
        m_recoveryInput->selectAll();
        break;
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

    if (users.isEmpty()) {
        auto* empty = new QLabel(tr("لا يوجد مستخدم"));
        empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(empty);
    }

    for (const core::User& user : users) {
        auto* btn = new QPushButton(user.name);
        btn->setObjectName(QStringLiteral("userButton"));
        btn->setProperty("userId", user.id);
        btn->setIcon(appIcon(Icon::People, textColor(), 18));
        btn->setCursor(Qt::PointingHandCursor);
        // This is a QDialog: without this, Enter in the PIN field would also
        // fire the first profile button. The primary button of the screen stays
        // the default on purpose — submitting a form with Enter is expected.
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
    if (!user.has_value() || !user->active) {
        return;
    }
    m_selectedUserId = user->id;
    m_selectedUserName = user->name;
    clearPinInput();
    refreshChrome();
    m_pinInput->setFocus();
}

void LoginDialog::clearPinInput()
{
    m_pinInput->clear();
    clearError();
    m_headerClickCount = 0;
}

void LoginDialog::clearSetupInputs()
{
    m_setupNameInput->clear();
    m_setupPinInput->clear();
    m_setupRecoveryInput->clear();
    m_setupConfirmInput->clear();
}

void LoginDialog::showError(const QString& text)
{
    m_errorLabel->setText(text);
    m_errorLabel->setVisible(true);
}

void LoginDialog::clearError()
{
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
}

void LoginDialog::onLoginClicked()
{
    clearError();
    if (m_selectedUserId <= 0) {
        return;
    }
    const QString pin = m_pinInput->text();
    if (pin.length() != 2) {
        showError(tr("أدخل حرفين فقط"));
        m_pinInput->clear();
        m_pinInput->setFocus();
        return;
    }

    data::UserRepository repo(m_db);
    const auto user = repo.findByPin(pin);
    if (user.has_value() && user->id == m_selectedUserId && user->active) {
        app::core::Session::instance().setCurrentUser(*user);
        accept();
        return;
    }
    showError(tr("PIN خاطئ"));
    m_pinInput->clear();
    m_pinInput->setFocus();
}

void LoginDialog::onSetupClicked()
{
    clearError();
    const QString name = m_setupNameInput->text().trimmed();
    const QString pin = m_setupPinInput->text();
    const QString recovery = m_setupRecoveryInput->text();
    const QString confirm = m_setupConfirmInput->text();

    if (name.isEmpty()) {
        showError(tr("أدخل اسماً"));
        return;
    }
    if (pin.length() != 2) {
        showError(tr("PIN يجب أن يكون حرفين"));
        return;
    }
    if (recovery.length() < 4) {
        showError(tr("كلمة الاستعادة: 4 أحرف على الأقل"));
        return;
    }
    if (recovery != confirm) {
        showError(tr("كلمة الاستعادة غير متطابقة"));
        return;
    }

    data::UserRepository repo(m_db);
    core::User admin;
    admin.name = name;
    admin.role = QStringLiteral("admin");
    const int id = repo.save(admin);
    if (id <= 0) {
        showError(tr("فشل إنشاء المستخدم"));
        return;
    }
    if (!repo.savePin(id, pin)) {
        showError(tr("فشل حفظ PIN"));
        return;
    }

    data::AdminSecretRepository secretRepo(m_db);
    if (!secretRepo.setMaster(id, recovery)) {
        showError(tr("فشل حفظ كلمة الاستعادة"));
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
    // The recovery word only unlocks an existing administrator, so the gesture
    // does nothing while the first one is still being configured.
    if (m_state != State::Connexion) {
        return;
    }
    m_headerClickCount++;
    if (m_headerClickCount < 8) {
        return;
    }
    m_headerClickCount = 0;
    goTo(State::Recuperation);
}

void LoginDialog::onRecoveryReturnPressed()
{
    clearError();
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
    showError(tr("كلمة الاستعادة غير صحيحة"));
    m_recoveryInput->clear();
    m_recoveryInput->setFocus();
}

bool LoginDialog::eventFilter(QObject* obj, QEvent* event)
{
    if ((obj == m_headerLabel || obj == m_logoLabel)
        && event->type() == QEvent::MouseButtonPress) {
        onHeaderClicked();
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

} // namespace app::ui
