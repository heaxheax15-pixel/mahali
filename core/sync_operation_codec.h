#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

#include "core/sync_operation.h"

namespace app::core {

// Serializes a SyncOperation to/from its compact JSON wire form. Single source
// of truth shared by both the device outbox (data layer) and the HTTP protocol
// (network layer) so neither needs to depend on the other.
class SyncOpCodec {
public:
    // The largest magnitude an old build could have written as a bare JSON
    // number and still meant exactly. Above 2^53 a double cannot hold every
    // integer, so a number that large is refused rather than guessed at.
    static constexpr long long kMaxExactJsonNumber = 9007199254740992LL; // 2^53

    static QByteArray serialize(const SyncOperation& op);

    // Reads an operation back. Nullopt means the payload was not an operation
    // this build can trust; use the overload below to learn which field was
    // refused and why.
    static std::optional<SyncOperation> deserialize(const QJsonObject& json);

    // Same, reporting the reason. `error` is left empty on success and is safe
    // to pass a nullptr for. The messages name the field and the value, because
    // a batch that fails to apply is worth nothing without knowing which field
    // of which operation was the problem.
    static std::optional<SyncOperation> deserialize(const QJsonObject& json, QString* error);

    // Reads one money-or-quantity field. Accepts the string form written by
    // current builds (full 64-bit range) and, for compatibility with older
    // builds, a bare JSON number that is a whole number within +/- 2^53. A
    // fractional or out-of-range number is refused: rounding it silently would
    // turn someone else's arithmetic into a different amount of money.
    static bool readExactInteger(const QJsonValue& value, const QString& fieldName, long long* out,
                                 QString* error);
};

} // namespace app::core