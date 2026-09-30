#pragma once

#include <QFrame>
#include <QWidget>

#include "data/database.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;

namespace app::ui {

// One quick item, drawn as a fixed-size card. Display only: a click reports the
// product id and nothing else, so the bar stays reusable while the cart logic
// is still being built.
class QuickItemCard : public QFrame {
    Q_OBJECT

public:
    QuickItemCard(int productId, const QString& name, const QString& price, QWidget* parent = nullptr);

    int productId() const { return m_productId; }

signals:
    void clicked(int productId);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    int m_productId;
};

// The trailing "+" tile that asks for a new product. Deliberately not a
// QuickItemCard: it carries no product id, and keeping it out of that class is
// what stops it from being counted as a listed item.
class QuickAddCard : public QFrame {
    Q_OBJECT

public:
    explicit QuickAddCard(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // The glyph is a pixmap, and a pixmap keeps the colour it was drawn with, so
    // the accent has to be pushed again whenever the theme is swapped.
    void refreshGlyph();

    QLabel* m_glyph = nullptr;
};

// A horizontal strip of quick items (active products with no barcode) for the
// POS page. Typing in the search field rebuilds the strip; an empty query lists
// every quick item. Cards are rebuilt from ProductRepository on each refresh,
// so a product added elsewhere shows up after the next refresh.
class QuickItemsBar : public QWidget {
    Q_OBJECT

public:
    explicit QuickItemsBar(app::data::Database& db, QWidget* parent = nullptr);

    // Rebuilds the cards. An empty query lists all quick items, otherwise the
    // name is filtered.
    void refresh(const QString& query = QString());

    // Test accessors.
    int cardCount() const;
    QLineEdit* searchField() const { return m_search; }
    bool isEmptyMessageVisible() const;

signals:
    // A quick item card was clicked, and the product behind it was added to the
    // cart.
    void productClicked(int productId);
    // The trailing "+" tile was clicked: the page opens the new product dialog.
    void addNewRequested();

private:
    void clearCards();

    app::data::Database& m_db;
    QLineEdit* m_search;
    QScrollArea* m_scroll;
    QWidget* m_cards;
    QHBoxLayout* m_cardsLayout;
    QLabel* m_empty;
    QuickAddCard* m_addCard = nullptr;
};

} // namespace app::ui
