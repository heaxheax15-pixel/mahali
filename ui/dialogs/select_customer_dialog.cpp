#include "select_customer_dialog.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

#include "core/customer.h"
#include "data/customer_repository.h"
#include "format_utils.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("SelectCustomerDialog", text);
}

} // namespace

std::optional<int> showSelectCustomerDialog(QWidget* parent, app::data::Database& db)
{
    data::CustomerRepository customers(db);

    // The balance is read once per customer rather than once per row of the table.
    // The sort needs it before the first row is drawn, and a column that asked the
    // ledger again on every repaint would query it on every keystroke in the search
    // field.
    struct Row {
        core::Customer customer;
        long long balanceCents = 0;
    };
    std::vector<Row> rows;
    for (const core::Customer& customer : customers.findAll()) {
        rows.push_back({customer, customers.balanceCentsFor(customer.id)});
    }

    // Deepest debt first. The name settles ties, so two accounts owing the same
    // figure do not swap places between openings.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.balanceCents != b.balanceCents) {
            return a.balanceCents > b.balanceCents;
        }
        return a.customer.name.compare(b.customer.name, Qt::CaseInsensitive) < 0;
    });

    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Choisir un client"));
    dialog.setModal(true);
    // The one dialog in the application that is pinned the other way round: its
    // columns are French labels meant to be read Nom, Téléphone, Solde in that
    // order, and a name typed into the search field has to come back out the way it
    // went in. Everything else in the app reads right to left.
    dialog.setLayoutDirection(Qt::LeftToRight);
    dialog.resize(500, 600);

    auto* layout = new QVBoxLayout(&dialog);

    auto* search = new QLineEdit;
    search->setObjectName(QStringLiteral("searchField"));
    search->setPlaceholderText(tr("Nom ou téléphone"));
    layout->addWidget(search);

    auto* table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({tr("Nom"), tr("Téléphone"), tr("Solde")});
    // Nothing here is edited: a row is picked, and a cell that opened for typing
    // would take a keystroke meant for the search field above.
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    // The name is what identifies the row, so it takes the slack and the money
    // column is only as wide as the figure in it.
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    auto* cancelButton = new QPushButton(tr("Annuler"));
    buttonRow->addWidget(cancelButton);
    layout->addLayout(buttonRow);

    std::optional<int> chosen;

    // Filtering is in memory over the list already read. The alternative is a
    // query per keystroke, which for a name typed one letter at a time is a
    // ledger round trip per letter.
    const auto refill = [&]() {
        const QString needle = search->text().trimmed();
        table->setRowCount(0);
        for (const Row& row : rows) {
            // Both fields, because a cashier holding the phone in one hand has no
            // use for the name, and the other way round.
            if (!needle.isEmpty() && !row.customer.name.contains(needle, Qt::CaseInsensitive)
                && !row.customer.phone.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
            const int line = table->rowCount();
            table->insertRow(line);

            auto* nameItem = new QTableWidgetItem(row.customer.name);
            // The id travels on the cell rather than being looked up again on
            // accept: the row index is a position in a filtered list, and the list
            // is rebuilt on every keystroke.
            nameItem->setData(Qt::UserRole, row.customer.id);
            table->setItem(line, 0, nameItem);

            auto* phoneItem = new QTableWidgetItem(row.customer.phone);
            phoneItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(line, 1, phoneItem);

            auto* balanceItem = new QTableWidgetItem(formatMoney(row.balanceCents));
            balanceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(line, 2, balanceItem);
        }
        // A search that leaves one account standing has already answered the
        // question, and the operator pressing Return means that account.
        if (table->rowCount() == 1) {
            table->selectRow(0);
        }
    };

    const auto take = [&]() {
        const int line = table->currentRow();
        if (line < 0) {
            return;
        }
        const QTableWidgetItem* nameItem = table->item(line, 0);
        if (!nameItem) {
            return;
        }
        chosen = nameItem->data(Qt::UserRole).toInt();
        dialog.accept();
    };

    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dialog,
                     [take](int, int) { take(); });
    QObject::connect(search, &QLineEdit::textChanged, &dialog,
                     [refill](const QString&) { refill(); });
    // A scanner presses Return wherever it is standing, and on this screen that is
    // worth having: a phone number typed at the till picks that account rather than
    // doing nothing, since refill() has already left it selected.
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, [take]() { take(); });

    refill();
    search->setFocus();
    dialog.exec();

    return chosen;
}

} // namespace app::ui
