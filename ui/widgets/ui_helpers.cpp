#include "ui_helpers.h"

#include <QFrame>
#include <QStyle>
#include <QVBoxLayout>

#include "app_icon.h"

namespace app::ui {

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    return card;
}

QLabel* makeChip(const QString& text, const QString& state, QWidget* parent)
{
    auto* chip = new QLabel(text, parent);
    chip->setProperty("state", state);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

void setChipState(QLabel* chip, const QString& state)
{
    if (chip->property("state").toString() == state) {
        return;
    }
    chip->setProperty("state", state);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    chip->update();
}

QLineEdit* makeSearchField(const QString& placeholder, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setObjectName(QStringLiteral("searchField"));
    field->setPlaceholderText(placeholder);
    field->setClearButtonEnabled(true);
    field->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                     QLineEdit::LeadingPosition);
    return field;
}

QLabel* makeCardTitle(const QString& text, QWidget* parent)
{
    auto* title = new QLabel(text, parent);
    title->setObjectName(QStringLiteral("cardTitle"));
    return title;
}

void padPageLayout(QVBoxLayout* layout)
{
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(12);
}

} // namespace app::ui