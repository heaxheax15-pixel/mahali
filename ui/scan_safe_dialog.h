#pragma once

#include <QDialog>
#include <QKeyEvent>

namespace app::ui {

// A barcode scanner appends Enter to every scan it emits. In a plain QDialog that
// key is handed to the dialog's default button (QDialog::showEvent promotes one,
// and QDialog::keyPressEvent then clicks it), so a scan in the middle of a form
// saves and closes the form.
//
// This subclass drops Return/Enter before QDialog can act on it. Fields keep
// working: QLineEdit emits returnPressed on its own, which is how the barcode
// field moves focus to the next one. Every other key, Escape included, keeps its
// normal QDialog meaning, and buttons stay clickable with the mouse or Space.
class ScanSafeDialog : public QDialog {
public:
    using QDialog::QDialog;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }
};

} // namespace app::ui
