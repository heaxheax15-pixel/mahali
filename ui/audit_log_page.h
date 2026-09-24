#pragma once

#include <QWidget>

#include "data/database.h"

class QCheckBox;
class QLabel;
class QTableWidget;

namespace app::ui {

// Audit log (سجل المراجعة): every sensitive action (price overrides, sale and
// payment refunds, expense/drawing reversals) in a read-only, time-filtered
// list so the shop owner can trace what happened.
class AuditLogPage : public QWidget {
    Q_OBJECT

public:
    explicit AuditLogPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;

private slots:
    void onTodayToggled();

private:
    app::data::Database& m_db;
    QCheckBox* m_todayOnly;
    QLabel* m_summary;
    QTableWidget* m_table;
};

} // namespace app::ui