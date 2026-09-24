#pragma once

#include <QWidget>

#include "data/database.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace app::ui {

// Refunds (الاستردادات): undo a recorded sale (restores stock and the till)
// or an over/erroneous customer payment, on the open cash session, with every
// reversal written to the audit log.
class RefundsPage : public QWidget {
    Q_OBJECT

public:
    explicit RefundsPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();

    int salesRowCount() const;
    int paymentsRowCount() const;
    QString noticeText() const;
    QTableWidget* salesTable() const { return m_salesTable; }
    QTableWidget* paymentsTable() const { return m_paymentsTable; }

public slots:
    void refundSale(int saleId);
    void refundPayment(int paymentId, const QString& note = QString());

private slots:
    void onRefundSaleClicked();
    void onRefundPaymentClicked();

private:
    app::data::Database& m_db;
    QTableWidget* m_salesTable;
    QTableWidget* m_paymentsTable;
    QPushButton* m_refundSale;
    QPushButton* m_refundPayment;
    QLabel* m_notice;
};

} // namespace app::ui