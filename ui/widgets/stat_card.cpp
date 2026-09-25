#include "stat_card.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace app::ui {

StatCard::StatCard(const QString& caption, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_icon = new QLabel;
    m_icon->setFixedSize(38, 38);
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
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(12);
    row->addWidget(m_icon);
    row->addLayout(texts, 1);
}

void StatCard::setIcon(Icon kind, const QString& accentColor)
{
    const QColor base(accentColor);
    QColor bg = base;
    bg.setAlpha(30);
    m_icon->setPixmap(appIcon(kind, base, 22).pixmap(22, 22));
    m_icon->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,%4); border-radius: 10px;")
                              .arg(bg.red())
                              .arg(bg.green())
                              .arg(bg.blue())
                              .arg(bg.alpha()));
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
        m_value->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; color: #1c8b5f;"));
        m_value->setText(QStringLiteral("+%1").arg(text));
    } else if (deltaCents < 0) {
        m_value->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; color: #c84444;"));
        m_value->setText(text);
    } else {
        m_value->setStyleSheet(QString());
        m_value->setText(text);
    }
}

} // namespace app::ui