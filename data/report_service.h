#pragma once

#include <QDateTime>
#include <QString>
#include <vector>

#include "database.h"

namespace app::data {

struct CashLine {
    QString type;
    int count = 0;
    long long sumCents = 0;
};

struct StoreReport {
    long long revenueCents = 0;
    long long salesCount = 0;
    long long cogsCents = 0;
    long long grossProfitCents = 0;
    long long expensesCents = 0;
    long long netProfitCents = 0;
    long long drawingsCents = 0;
    long long zakatBaseCents = 0;
    long long zakatCents = 0;
    long long openingFloatCents = 0;
    long long sessionsOpened = 0;
    long long outstandingDebtCents = 0;
    std::vector<CashLine> cashLines;
};

// Aggregates the profit/loss statement, the zakat base and the cash breakdown
// for a date range (both endpoints inclusive, mirroring the repositories).
class ReportService {
public:
    explicit ReportService(Database& db);

    StoreReport build(const QDateTime& from, const QDateTime& to) const;

private:
    Database& m_db;
};

} // namespace app::data