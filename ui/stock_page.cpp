#include "stock_page.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/product.h"
#include "data/product_repository.h"
#include "format_utils.h"
#include "theme.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// Column order. Qté is second because it is the reason this page exists, and the
// eye should land on it without scrolling sideways.
constexpr int kColQuantity = 0;
constexpr int kColName = 1;
constexpr int kColBarcode = 2;
constexpr int kColUnit = 3;
constexpr int kColValue = 4;

// The reorder band: on the shelf but nearly gone. Deliberately excludes zero and
// anything below it, which the sold-out and negative chips own between them.
constexpr long long kLowStockCeiling = 10;
constexpr int kSearchDebounceMs = 200;

// A count worth noticing, painted rather than left as another number in the row:
// a negative count is wrong and a zero one is gone. The colours come from the
// theme in use, read the same way suppliers_page reads its balance colour, so a
// row does not keep a light-theme colour after the shop switches.
QColor alarmingQuantity(long long quantity)
{
    const bool dark = activeTheme() == QLatin1String("dark");
    if (quantity < 0) {
        return QColor(dark ? QStringLiteral("#ef4444") : QStringLiteral("#dc2626"));
    }
    // Zero is not an error, so it is muted rather than red: sold out is a fact
    // about the shelf, not a mistake in the data.
    return QColor(dark ? QStringLiteral("#a3a3a3") : QStringLiteral("#94a3b8"));
}

} // namespace

StockPage::StockPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    root->addWidget(new PageHeader(tr("Stock"), tr("Suivez les quantités en stock")));

    // ---- card 1: search and filters ----
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setMinimumHeight(48);
    m_search->setPlaceholderText(tr("Rechercher un nom, un code-barres..."));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                        QLineEdit::LeadingPosition);

    // The grid follows 200ms after the last key rather than on each one: the
    // catalogue is read from the database on every refresh, and a pass per
    // keystroke is a pass per character against every row.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);

    const QList<QPair<QString, QString>> filters = {
        {QStringLiteral("all"), tr("Tous")},
        {QStringLiteral("low"), tr("Stock bas")},
        {QStringLiteral("negative"), tr("Stock négatif")},
        {QStringLiteral("empty"), tr("Épuisé")},
    };
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(8);
    for (const auto& [key, label] : filters) {
        auto* chip = new QPushButton(label);
        chip->setObjectName(QStringLiteral("filterChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setProperty("filterKey", key);
        m_chips.insert(key, chip);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);

    // The search box takes the slack the chips do not need, so a long chip label
    // never pushes the field off the card.
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    toolbar->addWidget(m_search, 1);
    toolbar->addLayout(chipRow);

    auto* toolbarCard = makeCard();
    auto* toolbarLayout = new QVBoxLayout(toolbarCard);
    toolbarLayout->setContentsMargins(18, 16, 18, 16);
    toolbarLayout->setSpacing(12);
    toolbarLayout->addLayout(toolbar);

    // ---- card 2: the grid ----
    m_table = new QTableWidget;
    // No object name on purpose: the theme styles QTableWidget generically, and
    // the two named tables in the app carry rules for the inline editors their
    // columns hold. This one edits nothing, so it asks for neither.
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Qté"), tr("Nom"), tr("Code-barres"),
                                        tr("Unité"), tr("Valeur")});
    // The order is the order an operator reads this page in: how many, what, then
    // how it is identified and what it is worth. Unchanged from before, but the
    // widths are now pinned rather than shared out evenly -- setSectionResizeMode
    // (Stretch) on all five gave a three-character count the same 250px as a
    // fifteen-character name, which is the same complaint the products grid had.
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Nothing on this page is editable: a count is changed by selling, receiving
    // or a stock movement, never by typing over a cell here.
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(kColQuantity, 90);
    m_table->setColumnWidth(kColBarcode, 180);
    m_table->setColumnWidth(kColUnit, 80);
    m_table->setColumnWidth(kColValue, 130);
    // The name takes what is left, as on the products grid: a stock list is read
    // by name, and the barcode beside it is the same fixed 180px it is there.
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    gridLayout->setContentsMargins(18, 16, 18, 16);
    gridLayout->setSpacing(12);
    gridLayout->addWidget(m_table, 1);

    // ---- footer ----
    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("faintText"));

    root->addWidget(toolbarCard);
    root->addWidget(gridCard, 1);
    root->addWidget(m_footer);

    connect(m_search, &QLineEdit::textChanged, this, &StockPage::onSearchChanged);
    connect(m_searchDebounce, &QTimer::timeout, this, &StockPage::refresh);
    for (auto* chip : m_chips) {
        connect(chip, &QPushButton::clicked, this, &StockPage::onFilterChipClicked);
    }

    setFilterActive(m_filterKey);
    refresh();
}

void StockPage::refresh()
{
    const QString query = m_search->text().trimmed();
    // Visibility::Visible, the same read the products list makes: a row with no
    // letter in its name is not browsed for here either.
    const std::vector<core::Product> all = data::ProductRepository(m_db).findAll(
        data::ProductRepository::Visibility::Visible);

    int shown = 0;
    int low = 0;
    long long totalValueCents = 0;

    m_table->setRowCount(0);

    for (const core::Product& product : all) {
        if (!query.isEmpty() && !product.name.contains(query, Qt::CaseInsensitive)
            && !product.barcode.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        if (m_filterKey == QLatin1String("low")
            && (product.quantity <= 0 || product.quantity > kLowStockCeiling)) {
            continue;
        }
        if (m_filterKey == QLatin1String("negative") && product.quantity >= 0) {
            continue;
        }
        if (m_filterKey == QLatin1String("empty") && product.quantity != 0) {
            continue;
        }

        ++shown;
        if (product.quantity > 0 && product.quantity <= kLowStockCeiling) {
            ++low;
        }
        // Quantity counts units and costPriceCents is the cost of one unit, so
        // the two multiply straight. packageSize is left out on purpose: nothing
        // in the till or in a stock movement applies it to a count, so a value
        // that included it would be larger than the stock it describes.
        totalValueCents += product.quantity * product.costPriceCents;

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, kColQuantity, new QTableWidgetItem(QString::number(product.quantity)));
        m_table->setItem(row, kColName, new QTableWidgetItem(product.name));
        m_table->setItem(row, kColBarcode, new QTableWidgetItem(product.barcode));
        m_table->setItem(row, kColUnit, new QTableWidgetItem(product.unit));
        m_table->setItem(row, kColValue,
                         new QTableWidgetItem(formatMoney(product.quantity * product.costPriceCents)));
        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->item(row, column)->setData(Qt::UserRole, product.id);
        }

        // Only the counts worth acting on are painted. A positive row keeps the
        // theme's own text colour rather than being given one here, so this
        // cannot fight the stylesheet.
        if (product.quantity <= 0) {
            m_table->item(row, kColQuantity)
                ->setForeground(QBrush(alarmingQuantity(product.quantity)));
        }
    }

    // The three numbers all describe the rows on screen, not the whole table: a
    // footer that counted everything while the grid showed one chip's worth would
    // be a way to be misinformed about what is in front of you.
    m_footer->setText(tr("Total : %1 produits · Valeur du stock : %2 · %3 en stock bas")
                          .arg(shown)
                          .arg(formatMoney(totalValueCents))
                          .arg(low));
}

int StockPage::rowCount() const
{
    return m_table->rowCount();
}

void StockPage::setFilterActive(const QString& key)
{
    m_filterKey = key;
    for (auto it = m_chips.cbegin(); it != m_chips.cend(); ++it) {
        QPushButton* chip = it.value();
        const bool on = it.key() == key;
        // A stylesheet keyed on the object name needs a repolish to notice the
        // change; setObjectName alone repaints with the old rule still cached.
        chip->setObjectName(on ? QStringLiteral("filterChipActive") : QStringLiteral("filterChip"));
        chip->setChecked(on);
        chip->style()->unpolish(chip);
        chip->style()->polish(chip);
        chip->update();
    }
}

void StockPage::onSearchChanged()
{
    m_searchDebounce->start();
}

void StockPage::onFilterChipClicked()
{
    auto* chip = qobject_cast<QPushButton*>(sender());
    if (!chip) {
        return;
    }
    const QString key = chip->property("filterKey").toString();
    if (key.isEmpty()) {
        return;
    }
    setFilterActive(key);
    refresh();
}

} // namespace app::ui
