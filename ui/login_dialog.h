#pragma once

#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include "core/session.h"
#include "data/database.h"

class QBoxLayout;
class QStackedWidget;

namespace app::ui {

// The login gate in front of the whole application. It owns three screens and
// only one of them is reachable at a time: the profile list with the PIN form,
// the first-administrator configuration, and the recovery word form that the
// eight-click gesture on the wordmark opens.
class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(app::data::Database& db, QWidget* parent = nullptr);

private slots:
    void onUserButtonClicked(int userId);
    void onLoginClicked();
    void onSetupClicked();
    void onHeaderClicked();
    void onRecoveryReturnPressed();

private:
    // Order matters: the values are the QStackedWidget page indexes.
    enum class State { Connexion, Configuration, Recuperation };

    QWidget* buildHeader();
    QWidget* buildConnexionPage();
    QWidget* buildConfigurationPage();
    QWidget* buildRecuperationPage();

    void goTo(State state);
    // Re-applies everything derived from the current state and selection:
    // window title, wordmark, subtitle, focus and the enabled fields.
    void refreshChrome();
    void buildUserList();
    void clearPinInput();
    void clearSetupInputs();
    void showError(const QString& text);
    void clearError();

    bool eventFilter(QObject* obj, QEvent* event) override;

    app::data::Database& m_db;

    State m_state = State::Connexion;
    int m_selectedUserId = 0;
    QString m_selectedUserName;
    int m_headerClickCount = 0;

    // The dialog owns a single error line and re-parents it into the screen
    // that is showing, so it always sits below that screen's fields and there
    // is only ever one #loginError to look up.
    struct ErrorSlot {
        QBoxLayout* layout = nullptr;
        QWidget* before = nullptr;
    };
    QLabel* m_errorLabel = nullptr;
    ErrorSlot m_errorSlots[3];

    QStackedWidget* m_stack = nullptr;

    QLabel* m_logoLabel = nullptr;
    QLabel* m_headerLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;

    QWidget* m_userListWidget = nullptr;
    QLineEdit* m_pinInput = nullptr;
    QPushButton* m_loginButton = nullptr;

    QLineEdit* m_setupNameInput = nullptr;
    QLineEdit* m_setupPinInput = nullptr;
    QLineEdit* m_setupRecoveryInput = nullptr;
    QLineEdit* m_setupConfirmInput = nullptr;
    QPushButton* m_setupButton = nullptr;

    QLineEdit* m_recoveryInput = nullptr;
    QPushButton* m_recoveryButton = nullptr;
};

} // namespace app::ui
