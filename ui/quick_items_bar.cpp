#include "quick_items_bar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include "data/product_repository.h"
#include "theme.h"
#include "widgets/app_icon.h"
#include "format_utils.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

constexpr int kBarHeight = 56;
constexpr int kCardWidth = 140;
constexpr int kCardHeight = 44;
constexpr int kSearchWidth = 220;
// The themed horizontal scrollbar is 14px (light.qss:972). The strip is
// kBarHeight less the vertical margins, and the card has to fit in what is
// left of that once the scrollbar takes its band: 60 - 8 - 14 = 38, so the
// card is 36 and the strip holds it. This is why the card is a single row --
// a name above a price needs about 60px, which is the whole bar.
constexpr int kBarMarginY = 4;
constexpr int kStripHeight = kBarHeight - 2 * kBarMarginY;

} // namespace

QuickItemCard::QuickItemCard(int productId, const QString& name, const QString& price, QWidget* parent)
    : QFrame(parent)
    , m_productId(productId)
{
    setObjectName(QStringLiteral("quickCard"));
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);

    // Side by side rather than stacked. The card is 36px tall, and a name over a
    // price at that height leaves each label one line at best and clips the
    // name on anything longer than a word. Here the name takes the width it
    // needs and elides, and the price stays whole because it is short.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);

    auto* nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("quickName"));
    // No word wrap: wrapped text at a fixed height grows the label's sizeHint
    // past the card, and the card is fixed, so the two fight and the price ends
    // up pushed out of view. Eliding keeps both on one line.
    nameLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    auto* priceLabel = new QLabel(price, this);
    priceLabel->setObjectName(QStringLiteral("quickPrice"));
    priceLabel->setAlignment(Qt::AlignVCenter);

    layout->addWidget(nameLabel, 1);
    layout->addWidget(priceLabel);
}

void QuickItemCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_productId);
    }
    QFrame::mousePressEvent(event);
}

QuickAddCard::QuickAddCard(QWidget* parent)
    : QFrame(parent)
{
    // Not "quickCard": that name is the product tile, and sharing it made the add
    // card inherit the tile's fill, hover and border, so the one control that is
    // not a product looked like a product. Its own name lets the stylesheet draw
    // it as the outlined "add" target it is.
    setObjectName(QStringLiteral("quickAddCard"));
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Ajouter un produit"));
    setAccessibleName(tr("Ajouter un produit"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_glyph = new QLabel(this);
    m_glyph->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_glyph);
    refreshGlyph();
}

void QuickAddCard::refreshGlyph()
{
    if (!m_glyph) {
        return;
    }
    const bool dark = activeTheme() == QStringLiteral("dark");
    const QColor accent(dark ? QStringLiteral("#d4a017") : QStringLiteral("#2563eb"));
    m_glyph->setPixmap(appIcon(Icon::Plus, accent, 24).pixmap(24, 24));
}

void QuickAddCard::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    // The application stylesheet is replaced on a theme switch, which is a style
    // change to every widget in the tree. This is the only signal the card gets,
    // and reading the live theme rather than the stored setting keeps the glyph
    // in step with what is actually on screen.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::ThemeChange
        || event->type() == QEvent::PaletteChange) {
        refreshGlyph();
    }
}

void QuickAddCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QFrame::mousePressEvent(event);
}

QuickItemsBar::QuickItemsBar(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    setObjectName(QStringLiteral("quickItemsBar"));
    setFixedHeight(kBarHeight);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, kBarMarginY, 12, kBarMarginY);
    root->setSpacing(16);

    // Built, but never laid out in the bar: the register's own scan field above
    // already searches by name, so a second field doing the same job was a
    // duplicate on screen and 220px of width. The widget stays because it is
    // the documented way to filter the strip programmatically (searchField() in
    // the header is a test accessor, and refresh(query) takes the same string),
    // so removing it would take the filtering away with the duplicate.
    m_search = makeSearchField(tr("بحث سريع"), this);
    m_search->setFixedWidth(kSearchWidth);
    m_search->hide();

    m_cards = new QWidget;
    m_cards->setObjectName(QStringLiteral("quickCardsRow"));
    m_cardsLayout = new QHBoxLayout(m_cards);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(12);
    m_cardsLayout->addStretch(1);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName(QStringLiteral("quickItemsScroll"));
    m_scroll->setWidget(m_cards);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFixedHeight(kStripHeight);

    // No "no quick items" label. It used to sit in a strip layout beside the
    // scroll area and the two took turns being visible, which cost no height
    // but left a row that said nothing useful: a till with no quick items is the
    // normal case for a shop that scans everything, and the strip reads as
    // empty on its own.

    m_addCard = new QuickAddCard(this);
    connect(m_addCard, &QuickAddCard::clicked, this, &QuickItemsBar::addNewRequested);

    root->addWidget(m_scroll, 1);
    root->addWidget(m_addCard);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) { refresh(text); });

    refresh();
}

void QuickItemsBar::refresh(const QString& query)
{
    clearCards();

    data::ProductRepository products(m_db);
    const std::vector<core::Product> items = query.isEmpty() ? products.findQuickItems()
                                                               : products.findQuickItemsByName(query);
    for (const auto& item : items) {
        auto* card = new QuickItemCard(item.id, item.name, formatMoney(item.salePriceCents), m_cards);
        connect(card, &QuickItemCard::clicked, this, &QuickItemsBar::productClicked);
        // The trailing stretch keeps the cards against the leading edge.
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
    }

    // No widget to hide now that the empty message is gone: the scroll area is
    // always shown and simply holds no cards when there is nothing to list.
}

void QuickItemsBar::clearCards()
{
    // The cards are deleted rather than scheduled: a test counts them right
    // after a refresh, and a second refresh would otherwise see the leftovers.
    while (QLayoutItem* item = m_cardsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
    m_cardsLayout->addStretch(1);
}

int QuickItemsBar::cardCount() const
{
    return static_cast<int>(m_cards->findChildren<QuickItemCard*>().size());
}

bool QuickItemsBar::isEmptyMessageVisible() const
{
    // The name is kept because the header declares it and a test asserts on it,
    // but there is no longer a label to report: what it answered is whether the
    // strip is showing nothing, which is the same question. After clearCards()
    // the row holds the trailing stretch and nothing else.
    return m_cardsLayout->count() <= 1;
}

} // namespace app::ui
