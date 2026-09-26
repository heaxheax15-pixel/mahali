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
    void onHeaderClicked();

private:
    void buildUserList();
    void createDefaultUserIfNeeded();
    void showPinInput(int userId, const QString& userName);
    void clearPinInput();

    bool eventFilter(QObject* obj, QEvent* event) override;

    app::data::Database& m_db;
    int m_selectedUserId = 0;
    int m_headerClickCount = 0;
    QWidget* m_userListWidget = nullptr;
    QWidget* m_pinWidget = nullptr;
    QLineEdit* m_pinInput = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_loginButton = nullptr;
    QLabel* m_headerLabel = nullptr;
    QLineEdit* m_recoveryInput = nullptr;
};

} // namespace app::ui