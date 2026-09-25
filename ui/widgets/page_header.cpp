#include "page_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace app::ui {

PageHeader::PageHeader(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    titleLabel->setStyleSheet(QStringLiteral("font-size: 20px;"));

    auto* subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setObjectName(QStringLiteral("hint"));
    subtitleLabel->setWordWrap(true);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(titleLabel);
    texts->addWidget(subtitleLabel);

    m_actionLayout = new QHBoxLayout;
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(8);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
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