#pragma once

#include <QDialog>
#include <QString>

namespace app::ui {

// Shown once, before LoginDialog, on the first launch. The three buttons name
// the three catalogues the app ships. The choice is stored in the settings
// table under "language"; on every subsequent launch the dialog is skipped
// because the setting already holds a value.
//
// No tr() anywhere in this class: the strings it shows are the three language
// names, which are the same word in every language, and the title is Arabic on
// purpose so the shopkeeper recognises it before any catalogue is loaded.
class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanguageDialog(QWidget* parent = nullptr);

    // "ar", "fr", or "en" once the user has clicked a button.
    // Empty if the dialog was closed without choosing.
    QString chosenCode() const { return m_chosenCode; }

private:
    QString m_chosenCode;
};

} // namespace app::ui
