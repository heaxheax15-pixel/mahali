#pragma once

#include <QVector>
#include <QWidget>

#include "core/sale_item.h"
#include "data/database.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace app::core {
struct SaleItem;
} // namespace app::core

namespace app::ui {

// Customers (الحسابات): the debt ledger. Balance owed = what they bought on
// credit minus what they paid (reversals are already negative entries). From
// here the operator records a credit sale (البيع الآجل) or a payment (سداد).
class CustomersPage : public QWidget {
    Q_OBJECT

public:
    explicit CustomersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }
    QString balanceAt(int row) const;
    QString noticeText() const;

public slots:
    void recordDebt(int customerId, const QVector<core::SaleItem>& items);
    void recordPayment(int customerId, long long amountCents, const QString& note);

private slots:
    void onAddClicked();
    void onEditClicked();
    void onDebtClicked();
    void onPaymentClicked();
    void onSelectionChanged();

private:
    int selectedCustomerId() const;

    app::data::Database& m_db;
    QTableWidget* m_table;
    QLabel* m_notice;
    QPushButton* m_edit = nullptr;
    QPushButton* m_debt = nullptr;
    QPushButton* m_pay = nullptr;
};

} // namespace app::ui