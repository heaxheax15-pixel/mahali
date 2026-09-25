#include "stat_card.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

namespace app::ui {

StatCard::StatCard(const QString& caption, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("statCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_icon = new QLabel;
    m_icon->setObjectName(QStringLiteral("statIcon"));
    m_icon->setFixedSize(40, 40);
    m_icon->setAlignment(Qt::AlignCenter);

    m_caption = new QLabel(caption);
    m_caption->setObjectName(QStringLiteral("statLabel"));

    m_value = new QLabel(QStringLiteral("—"));
    m_value->setObjectName(QStringLiteral("statValue"));
    m_value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(m_caption);
    texts->addWidget(m_value);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(12);
    row->addWidget(m_icon);
    row->addLayout(texts, 1);
}

void StatCard::setIcon(Icon kind, const QString& accentColor)
{
    Q_UNUSED(accentColor);
    m_icon->setPixmap(appIcon(kind, QColor(QStringLiteral("#4f46e5")), 22).pixmap(22, 22));
}

void StatCard::setValue(const QString& text)
{
    m_value->setText(text);
}

void StatCard::setCents(long long cents)
{
    m_value->setText(formatMoney(cents));
}

void StatCard::setDelta(long long deltaCents)
{
    const QString text = formatMoney(deltaCents);
    if (deltaCents > 0) {
        m_value->setProperty("deltaState", QStringLiteral("positive"));
        m_value->setText(QStringLiteral("+%1").arg(text));
    } else if (deltaCents < 0) {
        m_value->setProperty("deltaState", QStringLiteral("negative"));
        m_value->setText(text);
    } else {
        m_value->setProperty("deltaState", QStringLiteral("neutral"));
        m_value->setText(text);
    }
    m_value->style()->unpolish(m_value);
    m_value->style()->polish(m_value);
    m_value->update();
}

} // namespace app::ui