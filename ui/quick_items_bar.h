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
    // A quick item card was clicked. The cart is not wired up yet, so nothing
    // on the POS page listens to this yet.
    void productClicked(int productId);

private:
    void clearCards();

    app::data::Database& m_db;
    QLineEdit* m_search;
    QScrollArea* m_scroll;
    QWidget* m_cards;
    QHBoxLayout* m_cardsLayout;
    QLabel* m_empty;
};

} // namespace app::ui
