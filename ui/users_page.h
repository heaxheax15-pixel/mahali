#pragma once

#include <QWidget>

#include "data/database.h"

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace app::ui {

class UsersPage : public QWidget {
    Q_OBJECT

public:
    explicit UsersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }

private slots:
    void onAddClicked();
    void onEditClicked(int row);
    void onRemoveClicked(int row);
    void onActiveChanged(int row, int state);

private:
    void rebuildTable();

    app::data::Database& m_db;
    QPushButton* m_add;
    QTableWidget* m_table;
};

} // namespace app::ui