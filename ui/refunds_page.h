#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

#include "data/database.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace app::ui {

// Refunds (الاستردادات): undo a recorded sale (restores stock and the till)
// or an over/erroneous customer payment, on the open cash session, with every
// reversal written to the audit log.
//
// Both lists live in tabs with the reverse action on each row, so a till clerk
// reverses the row they can see rather than selecting it and then finding a
// button that acts on the selection: with one button per page the two steps
// have to agree, and they drift apart the moment the list is re-sorted.
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

protected:
    // A theme switch replaces the application stylesheet, which arrives at every
    // widget in the tree as a style change and nothing else. This is the only
    // signal the page gets, and without it the row icons keep the colour the
    // previous theme painted them in until the page is closed and reopened.
    void changeEvent(QEvent* event) override;

private:
    // The confirmation the two page-level handlers used to raise, moved onto the
    // row button so the id comes from the row that was pressed instead of from
    // whatever the table happened to have selected.
    void confirmSaleRefund(int saleId);
    void confirmPaymentRefund(int paymentId);

    // The icon colour for the current theme, and the buttons already built with
    // it, so a repaint after a theme switch can reach them without rebuilding
    // the rows (which would drop the row a dialog was opened against).
    QColor rowIconColor() const;
    void repaintRowButtons();

    app::data::Database& m_db;
    QTableWidget* m_salesTable;
    QTableWidget* m_paymentsTable;
    QLabel* m_salesEmpty;
    QLabel* m_paymentsEmpty;
    QLabel* m_notice;
    QList<QPushButton*> m_rowButtons;
};

} // namespace app::ui