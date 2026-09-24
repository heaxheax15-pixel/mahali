#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <optional>

#include "core/sync_operation.h"

namespace app::core {

// Serializes a SyncOperation to/from its compact JSON wire form. Single source
// of truth shared by both the device outbox (data layer) and the HTTP protocol
// (network layer) so neither needs to depend on the other.
class SyncOpCodec {
public:
    static QByteArray serialize(const SyncOperation& op);
    static std::optional<SyncOperation> deserialize(const QJsonObject& json);
};

} // namespace app::core