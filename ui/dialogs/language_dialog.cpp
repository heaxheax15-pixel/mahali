#include "language_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace app::ui {

LanguageDialog::LanguageDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("اختر اللغة"));
    setModal(true);
    setMinimumWidth(560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 40, 40, 40);
    root->setSpacing(32);

    auto* title = new QLabel(QStringLiteral("اختر اللغة"));
    title->setObjectName(QStringLiteral("languageTitle"));
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    auto* row = new QHBoxLayout;
    row->setSpacing(20);

    struct Choice { const char* label; const char* code; };
    const Choice choices[] = {
        { "العربية",  "ar" },
        { "Français", "fr" },
        { "English",  "en" },
    };

    for (const Choice& c : choices) {
        auto* btn = new QPushButton(QString::fromUtf8(c.label));
        btn->setObjectName(QStringLiteral("languageChoice"));
        btn->setMinimumSize(150, 100);
        btn->setCursor(Qt::PointingHandCursor);
        const QString code = QString::fromUtf8(c.code);
        connect(btn, &QPushButton::clicked, this, [this, code]() {
            m_chosenCode = code;
            accept();
        });
        row->addWidget(btn);
    }

    root->addLayout(row);
}

} // namespace app::ui
