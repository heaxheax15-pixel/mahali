#pragma once

#include "database.h"

namespace app::data {

// Single-row, durable, monotonic generator of per-device sync opIds. It must be
// called while the caller already owns an open transaction so that minting an
// opId is atomic with the financial write + outbox enqueue that carries it.
class SyncSequenceRepository {
public:
    explicit SyncSequenceRepository(Database& db);

    // Returns the next value (current + 1). 0 signals the read/update failed.
    int nextOpId();

private:
    Database& m_db;
};

} // namespace app::data