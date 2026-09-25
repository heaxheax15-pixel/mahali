#pragma once

// Small shared helpers used to build the flat card/chip/search look without
// repeating object-name bookkeeping across the pages.

#include <QLabel>
#include <QLineEdit>
#include <QWidget>

class QFrame;
class QHBoxLayout;
class QVBoxLayout;

namespace app::ui {

// A white (or themed) rounded card frame. Callers add content to its layout.
QFrame* makeCard(QWidget* parent = nullptr);

// A rounded status chip (state: ok / danger / muted / info).
QLabel* makeChip(const QString& text, const QString& state, QWidget* parent = nullptr);

// A chip whose color state can change at runtime.
void setChipState(QLabel* chip, const QString& state);

// A search-shaped line edit with a leading magnifier icon.
QLineEdit* makeSearchField(const QString& placeholder, QWidget* parent = nullptr);

// An in-card title label.
QLabel* makeCardTitle(const QString& text, QWidget* parent = nullptr);

// Adds margins to a page's root layout so content breathes.
void padPageLayout(QVBoxLayout* layout);

} // namespace app::ui