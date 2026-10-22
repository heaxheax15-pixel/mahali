# SYNC_PROTOCOL.md — Wire contract between desktop server and clients

This document defines the exact wire contract between the Mahali desktop sync server and any client (phone, second till). The phone app must be built from this document; every field, endpoint, and error class must match the code exactly — nothing invented. It is versioned by its own content; both ends ship together.

---

## 1. Purpose

The sync protocol allows remote clients to submit batches of operations (sales, customer debts, customer payments) to the desktop server over the LAN. The server verifies authenticity, applies operations transactionally (each in its own transaction), enforces exactly-once semantics via `(deviceId, opId)`, and returns per-operation acknowledgements. The contract is designed to remain backward compatible: older payloads that omit newly added optional fields must be accepted.

---

## 2. Transport

- HTTP/1.1 over LAN (no TLS).
- JSON, UTF-8. Request/response bodies are JSON.
- Interface: `QHostAddress::Any` — all interfaces (the shop LAN plus loopback) (`ui/server_controller.cpp:35`; header comment `ui/server_controller.h:31`).
- Port: there is **no fixed numeric port in the code**. `ServerController::start()` calls `startAt(0)`, and `startAt(quint16 port)` calls `m_server.server().listen(QHostAddress::Any, port)` (ui/server_controller.cpp:24-35). Port `0` means the OS assigns an available ephemeral port; `listen()` returns that port, stored in `m_port` and exposed via `ServerController::port()` (ui/server_controller.cpp:28-44; ui/server_controller.h:31,37,59). The desktop starts it automatically after login via `controller.start()` (apps/desktop/main.cpp:109-110), so the client must be configured with the **actual** assigned port (shown to the operator in the status message, server_controller.cpp:42).
- Clients reach the desktop by its LAN address on that port.

---

## 3. Authentication (HMAC-SHA256)

- Algorithm: HMAC-SHA256 over the raw request body bytes.
- Implementation (server): `SyncProtocol::hmacSha256(const QByteArray& payload, const QByteArray& key)` uses `QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256)` (network/sync_protocol.cpp:10-13). The server computes this over the exact raw body bytes and compares to the hex-decoded header value (network/sync_processor.cpp:55-60).
- Key: taken from settings key `sync_hmac_key` (UTF-8 bytes). On first
  launch `data::SyncKey::ensure` mints a random 32-byte key (hex, 64
  chars) and persists it; `apps/desktop/main.cpp`, `ui/settings_page.cpp`,
  and `apps/screenshot/main.cpp` call `ensure()` so the server signs with
  the stored/generated key (never a hardcoded default). Both devices must
  share the same key (manual copy).
- Header name (exact): `X-Mahali-Signature` (network/sync_server.cpp:23; network/sync_client.cpp:40).
- Encoding: hex-encoded bytes. Client sends `signature.toHex()` (lowercase hex from QByteArray::toHex()); server decodes with `QByteArray::fromHex(request.value("X-Mahali-Signature"))` (network/sync_server.cpp:23; network/sync_client.cpp:40).
- Verification result: if signature does not match, server responds with status 403 Forbidden and body contains `"ok": false` and `"error": "invalid HMAC signature"` (no applied/errors lists returned in that case) (network/sync_server.cpp:64-68; network/sync_processor.cpp:55-60).
- What it protects: authenticity/integrity of the raw request body (prevents tampering/replay from untrusted peers on LAN). What it does not: does not protect against replay of a valid signed body if the server has already applied ops (that is handled by idempotency via `(deviceId, opId)`; see §6). No TLS.

---

## 4. Endpoints

### 4.1 /api/sync
- Method: the route is registered with `m_server.route("/api/sync", ...)` with no method filter, so it matches any HTTP method; it is intended and used as `POST`.
- Path: `/api/sync`
- Request headers: `X-Mahali-Signature: <lowercase hex HMAC-SHA256 of raw body>` is the only header the server reads. `Content-Type: application/json` is sent by the reference client; the server does not read or validate it.
- Request body: JSON array of operation objects (batch). See §5.
- Processing: server passes raw body + hex-decoded signature + hmac key to `SyncProcessor::process(...)`. Processor validates HMAC, validates batch shape/size, validates each op identity, checks idempotency, enforces open-session constraints for Sale/CustomerPayment, and applies each op in its own transaction. Note: `process()` reads the raw `QJsonObject` fields directly; it does not call `SyncOpCodec::deserialize`. (network/sync_server.cpp:20-76; network/sync_processor.cpp:51-155)
- Response body (JSON object): `ok` and the two arrays `applied` and `errors` are **always** present (empty when there is nothing to report); `error` is present only when non-empty.
  - `ok` (bool): `result.hmacValid && result.batchValid` (network/sync_server.cpp:28)
  - `error` (string, only when non-empty): batch-level error (network/sync_server.cpp:29-31)
  - `applied` (array): applied/replayed acks. Each element: `opId` (int), `type` (string, one of `"sale"`, `"customer_debt"`, `"customer_payment"`), `entityId` (int, server-generated id of the created sale/payment/customer-transaction as applicable), `totalCents` (number; the server serializes the `long long` via `static_cast<double>` here), `cogsCents` (number; same; `0` for a payment, which has no cost of goods), `alreadyApplied` (bool, present only when true) (network/sync_server.cpp:33-48)
  - `errors` (array, always present): per-operation errors. Each element: `opId` (int), `errorClass` (int; `SyncErrorClass` value), `message` (string) (network/sync_server.cpp:50-62)
- Status codes returned:
  - 403 Forbidden if `!result.hmacValid` (e.g. invalid signature). Body includes `ok=false`, may include `error="invalid HMAC signature"`. (network/sync_server.cpp:64-68)
  - 400 Bad Request if `result.hmacValid` but `!result.batchValid`. `batchValid` is false only for the batch-shape cases: body not a JSON array -> `error="batch must be a JSON array of operations"`; empty array -> `error="empty batch"`; size>50 -> `error="batch exceeds 50 operations"` (network/sync_processor.cpp:61-73). In these cases the response also carries `ok=false` and `applied`/`errors` are empty.
  - Quirk (200 OK): if an element is not a JSON object, `process()` sets `error="operation is not a JSON object"` and returns **after** `batchValid` was already set true for a valid-sized batch, so the server answers **200 OK with `ok=true`** plus the `error` field (with empty `applied[]`/`errors[]`) (network/sync_processor.cpp:75-79; network/sync_server.cpp:28, 74-76). Clients should still surface `error` even when `ok` is true.
  - Per-op identity/validation errors are returned with 200 OK and placed in `errors[]`, not as batch-level 400.
  - 200 OK otherwise: `ok = true` (and batch valid). Response may contain `applied[]` and/or `errors[]` (network/sync_server.cpp:74-76).

Complete example (request with headers + response): see §11.

### 4.2 /api/health
- Method: the route is registered with `m_server.route("/api/health", ...)` with no method filter, so it matches any HTTP method; it is intended and used as `GET`.
- Path: `/api/health`
- Request: no body required
- Response (200 OK): `{"status":"ok", "time":"<ISODateWithMs UTC>"}` where time is `QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)` (network/sync_server.cpp:79-86). No authentication header is required by this route.

---

## 5. Payload — Batch and Operation Schema

### 5.1 Batch
- Top-level JSON value must be a JSON array `[...]` of operation objects. (network/sync_processor.cpp:61-66)
- Batch size: `count >= 1 && count <= 50`. Empty -> batch invalid (400). >50 -> batch invalid (400). (sync_protocol.h:30; sync_protocol.cpp:25-28; sync_processor.cpp:68-73; sync_client.cpp:18-28)
- Each element must be a JSON object. (sync_processor.cpp:75-79)

### 5.2 Operation (top-level fields)
All fields below are as defined in code (`core/sync_operation.h`, `core/sync_operation_codec.cpp`). Types reflect wire form.

- `opId` (int, required): positive integer > 0. Identity part of exactly-once key `(deviceId, opId)`. Rejected with per-op Permanent error `"opId must be a positive number"` if <= 0 (sync_processor.cpp:29-39, 82-89).
- `type` (int, required): enum value `0=Sale, 1=CustomerDebt, 2=CustomerPayment` (core/sync_operation.h:9). Unknown type -> per-op Permanent error `"unknown operation type <n>"` (sync_processor.cpp:141-144).
- `entityId` (int): meaning depends on `type`. For `CustomerDebt` it is the target customer id (rejected Permanent if `<= 0`; sync_processor.cpp:330-335). For `CustomerPayment` it is the target customer id (rejected Permanent if `<= 0` by the payment service; data/payment_service.cpp:29-32). For `Sale` it is unused/`0` in the reference payloads (a sale is recorded against the server’s open cash session, not a customer).
- `amountCents` (int/string, required): total amount in cents. **Required by the codec** (`deserialize` requires `opId`, `type`, `amountCents` present; codec.cpp:117-121) and it is the amount the server actually uses for `CustomerPayment` (read via `readExactInteger`; sync_processor.cpp:422-428). For `Sale` and `CustomerDebt` the server recomputes the total from the resolved items + `adjustmentCents` (`total = Σ(unitPriceCents×quantity) + adjustmentCents`, with the `/1000` weight division); the wire `amountCents` is not used by the sale/debt apply path (sync_processor.cpp:241-322, 324-407; sale_rules.h:166-188). **Integer-as-string rule applies** (see §5.4).
- `adjustmentCents` (int/string, **OPTIONAL**): adjustment applied on top of sum of item lines (same semantics as local sale). If omitted, defaults to `0`. If present, must be a valid integer (read via `readExactInteger`). (core/sync_operation.h:30; core/sync_operation_codec.cpp:36, 138-143; sync_processor.cpp:256-265, 342-352)
- `occurredAt` (string, required in serialized form): UTC timestamp in ISO 8601 format with milliseconds, e.g. `2026-10-17T12:34:56.789Z` (written as `toUTC().toString(Qt::ISODateWithMs)`). Deserialized via `QDateTime::fromString(..., Qt::ISODateWithMs)`. (codec.cpp:37, 144-146)
- `note` (string, optional): free text; defaults to empty string on read if missing. (codec.cpp:38, 147)
- `deviceId` (string, required): non-empty. Identity part of `(deviceId,opId)`. Rejected with per-op Permanent `"deviceId is required"` if empty (sync_processor.cpp:30-39).
- `items` (array, required): list of item objects. For `Sale` and `CustomerDebt` must be non-empty (sync_processor.cpp:248-251, 336-340). For `CustomerPayment` items array may be empty (see payment op shape) (sync_processor.cpp:247 in payment path reads amount from top-level; items not used).

### 5.3 Item object
- `productId` (int, required)
- `quantity` (int/string, required): quantity in the line’s unit (piece count for piece lines, number of packages for package lines, grams for weight lines). Read via `readExactInteger`. (codec.cpp:45,46,160-164; sync_processor.cpp:281-286, 365-370)
- `unitPriceCents` (int/string, required): unit price in cents for that line/unit. Read via `readExactInteger`. (codec.cpp:46, 164-168; sync_processor.cpp:287-292, 371-377)
- `unitKind` (string, **OPTIONAL**): one of `"piece"`, `"package"`, `"weight"`, or empty string `""`. Empty string means “bare piece line” (backward compatible with payloads that predates this field). If omitted, defaults to `""` on read. (core/sync_operation.h:15-17; codec.cpp:48, 170-171; sync_processor.cpp:296-304, 380-389)
- `piecesConsumed` (int/string, **OPTIONAL**): the positive count the stock arithmetic consumes for this line (how many base pieces/grams leave the shelf). `0` means “not sent”. If omitted, defaults to `0`. If present it must be an exact integer (read via `readExactInteger`) or the op is rejected Permanent. It is the device’s claim only: on apply, `resolveSaleItems()` recomputes the effective value from the central product (see §9), so the wire value does not by itself set the stock movement. (core/sync_operation.h:18-20; codec.cpp:49, 171-177; sync_processor.cpp:297-304, 380-389; data/sale_rules.h:87-126)

### 5.4 Integer-as-string rule (precision)
- Money/quantities are `long long` (64-bit). JSON numbers are IEEE-754 doubles which lose integer precision above 2^53 (~9e15); amounts can be larger than that. 
- **Serialize**: `amountCents`, `adjustmentCents`, `quantity`, `unitPriceCents`, `piecesConsumed` are written as JSON strings via `QString::number(...)` (codec.cpp:31,36,45,46,49). `opId`, `type`, `entityId`, `productId` are written as JSON numbers (ints). (codec.cpp:24-27,44)
- **Deserialize**: integers are read via `readExactInteger` which accepts:
  - string -> parsed with `QString::toLongLong()` (exact)
  - double -> only if finite, `floor(n)==n`, and `|n| <= 2^53` (kMaxExactJsonNumber = 2^53); otherwise refused (prevents silent rounding). Fractional parts are refused. (codec.cpp:56-104)
- This means clients MUST send 64-bit integer fields as strings on the wire (to be safe and to match serialized form). The codec accepts both forms for backward compatibility with older payloads that used doubles, but refuses values outside exact double range or with fractional parts.
- **Explicit note**: `adjustmentCents`, `unitKind`, `piecesConsumed` are OPTIONAL on the wire; a payload that omits them MUST be accepted (defaults `0`, `""`, `0`). This keeps an older client working. (codec.cpp:32-36, 133-143, 169-177; sync_processor.cpp:256-265, 296-304, 342-352, 380-389)

### 5.5 Operation types (semantics)
- `Sale` (type 0): records a cash sale against an open cash session. Requires open session on server; if none, per-op error `errorClass=Retry, message="no open cash session"`. Items non-empty. Applies via `SaleService::recordSale` with `allowOversold=false`, passes `adjustmentCents` and `SyncApplyToken{opId, deviceId}`. Returns created `saleId`, `totalCents`, `cogsCents`. (sync_processor.cpp:116-127, 241-322)
- `CustomerDebt` (type 1): records customer debt (no cash movement to cash session in that op). No open session required. Requires valid `customerId>0` and non-empty items. Applies via `SaleService::recordCustomerDebt`. (sync_processor.cpp:127-130, 324-407)
- `CustomerPayment` (type 2): records a customer payment into an open cash session. Requires open session; if none -> `errorClass=Retry, "no open cash session"`. `entityId` is the customer id (must be `> 0`, else Permanent `"customer id is invalid"`). Top-level `amountCents` is the payment amount and must be **strictly positive** (`> 0`, else Permanent `"payment amount must be positive"`; data/payment_service.cpp:25-32). `items` may be empty; `note` is the payment note. Applies via `PaymentService::recordCustomerPayment`. (sync_processor.cpp:131-141, 409-445)

---

## 6. Idempotency — exactly-once model

- Key: `(deviceId, opId)` stored in `applied_ops` (AppliedOpRepository). Each applied op is recorded with `device_id`, `op_id`, `op_type`, `entity_id`, `total_cents`, `cogs_cents`, timestamps etc. (referenced in sync_processor.h/cpp; see replay logic).
- Check: before applying, processor looks up the existing entry by `(opDeviceId, opId)`. If found, it builds a `SyncAppliedOp` from the stored record with `alreadyApplied = true` and appends it to `applied[]` (does not reapply). This happens for both `process()` and `processJson()` paths and runs **before** the open-cash-session gate, so a retry still succeeds even if the session has since closed (sync_processor.cpp:91-107, 183-195).
- Duplicate inside same batch: if the same `(deviceId,opId)` appears twice in the same request, the second is detected against the first application within the same batch context (processor tracks applied state via applied_ops table; after first is applied, second lookup finds it) and reported as `alreadyApplied=true` with same `entityId`. (tests/tst_sync_server.cpp:675-703)
- Cross-batch replay: if ACK lost, client resends same batch/op -> server returns `alreadyApplied=true` with original `entityId`, `totalCents`, `cogsCents`. (tests/tst_sync_server.cpp:603-640, 642-673)
- Client requirements: must never reuse an `opId` for a different operation; must keep the op in the outbox until it receives an acknowledgement for that `opId` (coordinator reconciles ACKs by matching `appliedOpId == entry.op.opId`).

---

## 7. Error classes and taxonomy

- Enum `SyncErrorClass` (network/sync_protocol.h:12): `None = 0`, `Permanent = 1`, `Retry = 2`, `Network = 3`.
- Mapping from HTTP status (client-side classification): `SyncProtocol::errorClassForStatus(int httpStatus)` (sync_protocol.cpp:30-54)

  | HTTP status | Class | Rule |
  |---|---|---|
  | 100-199 | `Permanent` | informational only; no final answer means resending gets the same |
  | 200-299 | `None` | success |
  | 300-399 | `Permanent` | redirect the client refused to follow |
  | 400-499 | `Permanent` | client error; do not retry |
  | 500-599 | `Retry` | server error; retry later |
  | anything else | `Network` | not an HTTP status (e.g. status 0) |

- Server returns per-op `errorClass` (int) in `errors[]` elements (network/sync_server.cpp:57-61). Values correspond to enum above.
- Common per-op cases:
  - Identity: missing/invalid `deviceId` or `opId<=0` -> `Permanent`, message like `"deviceId is required"` or `"opId must be a positive number"`. (sync_processor.cpp:30-39, 82-89)
  - Unknown operation type -> `Permanent`, `"unknown operation type <n>"`. (141-144)
  - Missing open cash session for Sale/CustomerPayment -> `Retry`, `"no open cash session"`. (119-123, 133-137, 207-210, 220-223) — note: this is a transient condition (session may open/close) so classified Retry.
  - Structural/validation errors from apply paths (empty items, bad customer id, non-positive payment amount, integer parse errors including fractional/invalid legacy number) -> `Permanent` with a descriptive message: e.g. `"sale has no items"`, `"customer_debt has no items"`, `"customer_debt requires a valid customer id"`, `"customer id is invalid"`, `"payment amount must be positive"`, `"insufficient stock for product <name>"`, or `readExactInteger` messages such as `"<field> is not a whole number: \"...\""`, `"<field> has a fractional part: ..."`, `"<field> is outside the exactly representable range of +/-2^53: ..."`. (sync_processor.cpp:146-153, 249-251, 262-264, 284-286, 290-292, 302-304, 330-340, 349-351, 368-370, 375-377, 386-388, 425-428; codec.cpp:56-104; data/sale_rules.h:139-144; data/payment_service.cpp:25-32)
- Batch-level errors: HMAC invalid -> 403 (empty `applied[]`/`errors[]`); body not an array, empty array, or size>50 -> 400 with a batch `error` and empty `applied[]`/`errors[]`; a non-object element -> 200 with `ok=true` and a batch `error` (the quirk in §4.1). (network/sync_server.cpp:28, 64-76; sync_processor.cpp:55-79)

---

## 8. Client behaviour (what the phone must do)

Based on `network/sync_client.cpp`, `network/sync_coordinator.cpp`, `network/sync_scheduler.cpp` (referenced) and outbox model:

- Queue inside sale transaction: the device mints the `opId` (`SyncSequenceRepository::nextOpId()` monotonic `UPDATE ... value+1`), builds the `SyncOperation`, and enqueues it to the outbox **inside the same DB transaction** that records the local sale (`DeviceLedgerService::recordSale` begins the transaction, then calls `mintAndEnqueue(...)` → `m_outbox.enqueue(op)`). `opId` is never reused because it is drawn from a monotonically increasing sequence. (data/device_ledger_service.cpp:60-103, 119-, data/sync_sequence_repository.cpp:13-31)
- Batch limit: maximum `kMaxBatchSize = 50` operations per batch. Client must not send more than 50; coordinator queries `outbox.findPending(SyncClient::kMaxBatchSize)`. (sync_client.h:25; sync_coordinator.cpp:25)
- Serialize: for each op produce wire form via codec rules (string form for 64-bit ints: `amountCents`, `adjustmentCents`, `quantity`, `unitPriceCents`, `piecesConsumed`). The reference codec always writes `unitKind` and `piecesConsumed` (empty string / `"0"` when unset), but the receiver accepts their omission (older clients). (codec.cpp:21-54, 133-177)
- Sign: compute `HMAC-SHA256` over compact JSON bytes of the batch array with shared key; set header `X-Mahali-Signature` to hex of signature (lowercase). Set `Content-Type: application/json`. (sync_client.cpp:30-41)
- Send: `POST <endpoint>/api/sync` (blocking call in reference client). (sync_client.cpp:42-46). Endpoint is configured per device (URL to desktop server).
- After a delivered response (the request reached the server, any HTTP status), reconcile the per-op ACK list. The response is `delivered` when `QNetworkReply::error() == NoError || status != 0`; `errorClass` is then `errorClassForStatus(status)` (sync_client.cpp:53-60).
  - Per-op `applied[]`/`errors[]`: reconcile each against the outbox by matching `appliedOpId == op.opId`. `applied` ack -> `outbox.markApplied`. `errors` ack with `errorClass == Permanent` -> `outbox.markPermanentFailed` with the message (never retry). `errors` ack with `Retry`/`Network` -> keep pending via `outbox.recordAttempt`. (sync_coordinator.cpp:56-87)
  - No per-op acks at all (a batch-level failure such as invalid HMAC/400, or the server did not mention an op) -> every pending op in that batch stays pending; `recordAttempt` for each (never delete). (sync_coordinator.cpp:45-54, 66-71)
- 4xx: `errorClassForStatus` maps 400-499 to `Permanent` (sync_protocol.cpp:43-45). On the ACK path a per-op `Permanent` marks that op failed permanently (sync_coordinator.cpp:74-76); on the batch-level path (no per-op ACKs) the ops are still only `recordAttempt`-ed and stay pending. Never reuse an opId; never silently drop an op.
- On 5xx/Retry/Network: 500-599 -> Retry; network errors (status 0 with network error) -> `Network` class; client must keep those ops pending and retry later. Per-op Retry errors (e.g. `"no open cash session"`) are also kept pending.
- Backoff: the device-side loop lives in `SyncScheduler` (network/sync_scheduler.cpp:72-89). The drain interval starts at the base interval `m_baseInterval = 3000` ms (sync_scheduler.h:55), doubles on every stalled round (`m_currentInterval = qMin(m_maxInterval, qMax(m_baseInterval, m_currentInterval * 2))`, sync_scheduler.cpp:82), is capped at `m_maxInterval = 300000` ms (5 minutes; sync_scheduler.h:56), and resets to the base the moment a round makes full progress (sync_scheduler.cpp:83-85). A round is "stalled" when it was attempted and `!delivered`, or `errorClass` is `Network` or `Retry`, or `keptPending > 0` (sync_scheduler.cpp:78-80). `SyncScheduler::syncNow()` triggers an immediate out-of-band drain (e.g. right after a sale) (sync_scheduler.cpp:42-50, 35-37).
- Timeout: the reference client performs a blocking `QNetworkReply` + `QEventLoop` and sets no explicit timeout/transfer-timeout of its own (sync_client.cpp:42-46); it relies on Qt/QNetworkAccessManager defaults. A client is free to impose its own request timeout, but must treat a timed-out request as `Network`/retry (never as applied).
- On shutdown: outbox persists (must be on durable storage). Coordinator operates on DB/outbox repository; operations stay in outbox until acknowledged.

---

## 9. Failure modes and what they mean for the shop

- Server not reachable: a network error (no HTTP response, status 0, `QNetworkReply::error() != NoError`) is classified `Network` (sync_client.cpp:53-57). The operations stay pending in the outbox and are retried later with backoff; nothing is dropped or applied server-side without an ACK.
- Signature rejected (both devices must share `sync_hmac_key`): the server returns HTTP 403 with `"ok": false` and `error="invalid HMAC signature"`; the batch is not applied. The client’s status mapping turns 403 into `Permanent` (sync_protocol.cpp:43-45), and the coordinator keeps or discards accordingly (a batch-level failure with no per-op ACKs keeps every op pending — sync_coordinator.cpp:45-54). Both devices must be configured with the same `sync_hmac_key`; mismatched keys block sync but also prevent unauthorized writes.
- Piece count drift (server re-derives `piecesConsumed` from its own product row; the server wins silently — documented constraint for this release): the wire may carry `piecesConsumed` and `unitKind` as the device’s claim, and the server passes them into `SaleService::recordSale` / `recordCustomerDebt`. Those services call `resolveSaleItems(...)`, which **overwrites** `unitKind` (empty → `"piece"`) and recomputes `piecesConsumed` from the central product (`quantity × piecesPerPackage` for a package line, otherwise `quantity`) — the device’s value is not the final stock figure (`data/sale_rules.h:62-147`; `data/sale_service.cpp:128, 189-196`). So if a phone’s local product data differs from the desktop’s, the desktop’s definition determines how many pieces leave the shelf; there is no wire-level rejection for that difference.

---

## 10. Versioning

- No version field in the protocol/payload. Codec accepts missing keys (optional fields). Older payloads are always valid.
- Backward compatibility: adding an optional field never breaks an old peer. Deserialization treats missing optional keys as defaults (`adjustmentCents` missing -> 0, `unitKind` missing -> `""`, `piecesConsumed` missing -> 0). (codec.cpp:133-143, 169-177; processor applies same)
- If anything that breaks compatibility must appear, it must be documented here and in a migration note. The integer-as-string rule preserves precision across versions.

---

## 11. Complete worked example

This example shows a phone syncing one cash sale of two packages and one weight line, adjustment −500 DA. Money is integer cents throughout; `formatMoney` divides by 100 for display, so −500 DA is `-50000` cents (core/format_utils.h:28-45).

### 11.1 Scenario details
- Operation: `Sale` (type 0); device `"phone-01"`; `opId = 1001`; shared key example `"test-shared-key"`.
- Item 1 (packages): `productId=5`, `quantity="2"` (two package units), `unitPriceCents="1440"` (1440 cents per package), `unitKind="package"`, `piecesConsumed="48"` (2 × 24 pieces per package).
- Item 2 (weight): `productId=6`, `quantity="250"` (250 grams), `unitPriceCents="120000"` (120000 cents per kilogram), `unitKind="weight"`, `piecesConsumed="250"`.
- Server line total: item1 `1440×2 = 2880`; item2 `120000×250/1000 = 30000` (the `/1000` weight division lives in `totalCentsFor`; data/sale_rules.h:166-188). Lines sum `= 32880`.
- `adjustmentCents = "-50000"` (−500 DA). So `amountCents = 32880 + (-50000) = -17120` (the server would recompute the same total from items + adjustment; the wire value must still be present).

So operation:
```json
{
  "opId": 1001,
  "type": 0,
  "entityId": 0,
  "amountCents": "-17120",
  "adjustmentCents": "-50000",
  "occurredAt": "2026-10-17T10:00:00.000Z",
  "note": "sale",
  "deviceId": "phone-01",
  "items": [
    {
      "productId": 5,
      "quantity": "2",
      "unitPriceCents": "1440",
      "unitKind": "package",
      "piecesConsumed": "48"
    },
    {
      "productId": 6,
      "quantity": "250",
      "unitPriceCents": "120000",
      "unitKind": "weight",
      "piecesConsumed": "250"
    }
  ]
}
```
### 11.2 Raw body bytes (compact JSON)
The reference client serialises with `QJsonDocument(...).toJson(QJsonDocument::Compact)`. Qt emits `QJsonObject` keys in sorted order, producing these exact bytes:

`[{"adjustmentCents":"-50000","amountCents":"-17120","deviceId":"phone-01","entityId":0,"items":[{"piecesConsumed":"48","productId":5,"quantity":"2","unitKind":"package","unitPriceCents":"1440"},{"piecesConsumed":"250","productId":6,"quantity":"250","unitKind":"weight","unitPriceCents":"120000"}],"note":"sale","occurredAt":"2026-10-17T10:00:00.000Z","opId":1001,"type":0}]`

Key order is the sender's choice: the server parses by key and never requires a particular order. The HMAC, however, must be computed over the **exact bytes the sender transmits**, whatever their order (the signature below corresponds to the bytes shown here).

### 11.3 HMAC computation
- Algorithm: HMAC-SHA256
- Key (UTF-8): `"test-shared-key"` (example)
- Data: the raw body bytes in §11.2
- Signature bytes -> hex (lowercase): `99d6d8dd0ab3449b1c5c6a3ed298fb779ecd6f104b1b4568dbdfcd5d9fd7d237`

### 11.4 HTTP request
```http
POST /api/sync HTTP/1.1
Host: <desktop-lan-ip>:<port>
Content-Type: application/json
X-Mahali-Signature: 99d6d8dd0ab3449b1c5c6a3ed298fb779ecd6f104b1b4568dbdfcd5d9fd7d237

[{"adjustmentCents":"-50000","amountCents":"-17120","deviceId":"phone-01","entityId":0,"items":[{"piecesConsumed":"48","productId":5,"quantity":"2","unitKind":"package","unitPriceCents":"1440"},{"piecesConsumed":"250","productId":6,"quantity":"250","unitKind":"weight","unitPriceCents":"120000"}],"note":"sale","occurredAt":"2026-10-17T10:00:00.000Z","opId":1001,"type":0}]
```

### 11.5 Success response (200 OK)
```json
{
  "ok": true,
  "applied": [
    {
      "opId": 1001,
      "type": "sale",
      "entityId": <server-generated sale id>,
      "totalCents": <total computed by server>,
      "cogsCents": <cogs computed by server>
    }
  ],
  "errors": []
}
```
Notes: `alreadyApplied` is added only when true (sync_server.cpp:43-45), so it is absent from a fresh-apply ack like this one and present as `true` on a replay. `totalCents` and `cogsCents` are returned as JSON numbers (server serializes `long long` via `static_cast<double>` in the response object; see §4.1). The example values depend on server-side product definitions and derivation; the shape is exact.
