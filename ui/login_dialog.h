#pragma once

#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include "data/database.h"
#include "core/session.h"

namespace app::ui {

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
    void buildUserList();
    void showSetupMode();
    void showLoginMode();
    void showPinInput(int userId, const QString& userName);
    void clearPinInput();
    void clearSetupInputs();

    bool eventFilter(QObject* obj, QEvent* event) override;

    app::data::Database& m_db;
    int m_selectedUserId = 0;
    int m_headerClickCount = 0;
    bool m_setupMode = false;

    QWidget* m_userListWidget = nullptr;
    QWidget* m_pinWidget = nullptr;
    QWidget* m_setupWidget = nullptr;

    QLineEdit* m_pinInput = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_loginButton = nullptr;
    QLabel* m_headerLabel = nullptr;
    QLineEdit* m_recoveryInput = nullptr;

    // Setup mode widgets
    QLineEdit* m_setupNameInput = nullptr;
    QLineEdit* m_setupPinInput = nullptr;
    QLineEdit* m_setupRecoveryInput = nullptr;
    QLineEdit* m_setupConfirmInput = nullptr;
    QLabel* m_setupErrorLabel = nullptr;
    QPushButton* m_setupButton = nullptr;
};

} // namespace app::ui