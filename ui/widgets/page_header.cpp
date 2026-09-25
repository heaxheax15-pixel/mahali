#include "page_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace app::ui {

PageHeader::PageHeader(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pageHeader"));

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("pageHeaderTitle"));

    auto* subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setObjectName(QStringLiteral("pageHeaderSubtitle"));
    subtitleLabel->setWordWrap(true);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(3);
    texts->addWidget(titleLabel);
    texts->addWidget(subtitleLabel);

    m_actionLayout = new QHBoxLayout;
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(10);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(18, 14, 18, 14);
    row->setSpacing(14);
    row->addLayout(texts, 1);
    row->addLayout(m_actionLayout);
}

void PageHeader::setTitle(const QString& title)
{
    for (QObject* child : children()) {
        if (auto* label = qobject_cast<QLabel*>(child);
            label && label->objectName() == QStringLiteral("cardTitle")) {
            label->setText(title);
            return;
        }
    }
}

void PageHeader::setSubtitle(const QString& subtitle)
{
    for (QObject* child : children()) {
        if (auto* label = qobject_cast<QLabel*>(child);
            label && label->objectName() == QStringLiteral("hint")) {
            label->setText(subtitle);
            return;
        }
    }
}

void PageHeader::addAction(QWidget* widget)
{
    m_actionLayout->addWidget(widget);
}

QHBoxLayout* PageHeader::actionLayout() const
{
    return m_actionLayout;
}

} // namespace app::ui