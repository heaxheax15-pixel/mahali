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

constexpr int kBarHeight = 130;
constexpr int kCardWidth = 140;
constexpr int kCardHeight = 100;
constexpr int kSearchWidth = 220;
// The themed horizontal scrollbar is 12px plus a 2px margin. The strip leaves
// room for it so the 100px card is never clipped once the row overflows.
constexpr int kBarMarginY = 7;
constexpr int kStripHeight = kBarHeight - 2 * kBarMarginY;

} // namespace

QuickItemCard::QuickItemCard(int productId, const QString& name, const QString& price, QWidget* parent)
    : QFrame(parent)
    , m_productId(productId)
{
    setObjectName(QStringLiteral("quickCard"));
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(4);

    auto* nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("quickName"));
    nameLabel->setWordWrap(true);
    nameLabel->setAlignment(Qt::AlignCenter);

    auto* priceLabel = new QLabel(price, this);
    priceLabel->setObjectName(QStringLiteral("quickPrice"));
    priceLabel->setAlignment(Qt::AlignCenter);

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

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    m_glyph = new QLabel(this);
    m_glyph->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_glyph, 1);
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

    m_search = makeSearchField(tr("بحث سريع"), this);
    m_search->setFixedWidth(kSearchWidth);

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

    m_empty = new QLabel(tr("لا منتجات سريعة"));
    m_empty->setObjectName(QStringLiteral("quickEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setFixedHeight(kStripHeight);

    // The message takes the strip's place when there is nothing to list, so the
    // two never compete for the same row.
    auto* strip = new QVBoxLayout;
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(0);
    strip->addWidget(m_scroll);
    strip->addWidget(m_empty);

    m_addCard = new QuickAddCard(this);
    connect(m_addCard, &QuickAddCard::clicked, this, &QuickItemsBar::addNewRequested);

    root->addWidget(m_search);
    root->addLayout(strip, 1);
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

    const bool empty = items.empty();
    m_scroll->setVisible(!empty);
    m_empty->setVisible(empty);
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
    return !m_empty->isHidden();
}

} // namespace app::ui
