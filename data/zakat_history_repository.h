#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "database.h"

namespace app::data {

// One zakat year as it was assessed: what the threshold was, what the shop's
// trading goods came to, what that worked out to, and whether it was paid.
struct ZakatHistory {
    int id = 0;
    int year = 0;
    long long nisabCents = 0;
    long long baseCents = 0;
    long long dueCents = 0;
    // The per-gram gold price the nisab was derived from, kept so a past year
    // can be explained later: the threshold alone does not say what it came from.
    long long goldPriceCents = 0;
    bool paid = false;
    long long paidCents = 0;
    QString paidAt;
};

class ZakatHistoryRepository {
public:
    explicit ZakatHistoryRepository(Database& db);

    // Records the year's assessment, or leaves an existing row alone. Deliberately
    // not an upsert: a year that is already on file was assessed once and the
    // figures of record are the ones it was first assessed at, so re-running the
    // calculation months later with today's stock cannot rewrite history. Returns
    // false when the statement fails.
    bool insertOrIgnore(int year, long long nisabCents, long long baseCents, long long dueCents,
                        long long goldPriceCents);

    // Settles a year. Updates nothing and returns false when there is no such
    // year on file — marking a payment for a year nobody assessed would leave a
    // "paid" with no "what for" behind it.
    bool markPaid(int year, long long paidCents, const QString& paidAt);

    std::optional<ZakatHistory> findByYear(int year) const;

    // Newest year first, which is the order the reports page reads them in.
    std::vector<ZakatHistory> findAll() const;

private:
    Database& m_db;
};

} // namespace app::data