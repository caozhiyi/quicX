# The QPACK Dynamic Table

This document walks through quicX's implementation of RFC 9204 *QPACK: Field Compression for HTTP/3*: the full chain of an HTTP header from being accepted by `Encode()`, decided into the dynamic table, pushed to the peer over the encoder stream, up to the peer's decoder stream feeding back Section Ack / Insert Count Increment; and, in the reverse direction, how a received HEADERS frame gets "parked" when the dynamic-table inserts haven't caught up, then re-decoded once the IIC arrives. After reading it you should be able to:

- Know which two files quicX puts QPACK's "compression state" and "transport state" in;
- Understand how **Required Insert Count (RIC)**, an RFC concept, flows through the repo — who produces it, who validates it, who blocks on it, who releases it;
- Locate the exact code fragment when debugging "the peer reports `QPACK_DECOMPRESSION_FAILED`" or "HEADERS never reach the application layer";
- Distinguish the two kinds of QPACK index in quicX — deque position vs absolute index — and which problem each solves.

This document covers only the **dynamic table + blocking coordination** chain. The static table (QPACK Appendix A, 99 entries) is a read-only singleton outside our scope; Huffman encoding/decoding is a separate parallel chain, see `huffman_encoder.cpp`.

---

## 1. The Overall Path

The diagram below draws the two dual chains of a header, starting from "the user calls `SendHeaders` / a `HEADERS` frame arrives", in one picture, with every node annotated with its **actual function name** for source cross-referencing:

```text
═══════════════════════════════════════ ① Encoding side (sending HEADERS) ═══════════════════════════════════════

  ReqRespBaseStream::SendHeaders(headers)
        │
        ▼
  QpackEncoder::Encode(headers, buffer)                                【§3 two-pass encoding】
        │
        ├── Pass 1: decide the EncodeAction for every field
        │     ├── StaticTable::FindHeaderItemIndex   → kStaticIndexed / kStaticNameRef
        │     ├── DynamicTable::FindAbsoluteIndex    → kDynamicIndexed / kDynamicPostBaseIndexed
        │     ├── otherwise try inserting into dynamic_table_  ──► instruction_sender_  (encoder stream)
        │     │     │
        │     │     └── triggers synchronously: DynamicTable::AddHeaderItem
        │     │                                       deque.push_front
        │     │                                       total_insert_count_++
        │     │                                       index_map +1 offset
        │     └── all failed: kLiteralNoNameRef
        │
        ├── track max_required_insert_count                            【§4 RIC computation】
        │
        └── Pass 2: write the Header Block Prefix + each field
              ├── WriteHeaderPrefix(RIC, Base)        encode RIC = (ric % 2·MaxEntries) + 1
              └── each field encoded per its EncodeAction into the HEADERS frame payload


═══════════════════════════════════════ ② Decoding side (HEADERS received) ═══════════════════════════════════════

  ReqRespBaseStream::OnData ──► HandleHeaders(headers_frame)
        │
        ▼
  QpackEncoder::Decode(buffer, headers)              ◄── the same class serves as both encoder and decoder
        │
        ├── ReadHeaderPrefix(RIC, Base)
        │
        ├── if (RIC > dynamic_table_.GetInsertCount()) → return false   【§5 blocking】
        │     │
        │     ▼
        │   HandleHeaders detects the false
        │     ├── is_currently_blocked_ = true
        │     ├── snapshot encoded_fields (CloneReadable, !move_write_pt)
        │     └── blocked_registry_->Add(key, retry)
        │
        └── decode the fields → headers, record last_decoded_ric_        【§6 field decoding】
              │
              ▼
        if (last_decoded_ric_ > 0) EmitDecoderFeedback(SectionAck, key)


═══════════════════════════════════════ ③ encoder stream receive path ═══════════════════════════════════════

  the peer sends encoder stream bytes
        │
        ▼
  QpackEncoderReceiverStream::OnData
        │
        ▼
  QpackEncoder::DecodeEncoderInstructions(buffer)                      【§7 encoder instructions】
        │
        ├── Set Dynamic Table Capacity      → DynamicTable::UpdateMaxTableSize
        ├── Insert With Name Reference      → DynamicTable::AddHeaderItem
        ├── Insert Without Name Reference   → DynamicTable::AddHeaderItem
        └── Duplicate                       → DynamicTable::DuplicateEntry
        │
        ▼
  delta = insert_count_after − insert_count_before
        │
        ├── if (delta > 0)  EmitDecoderFeedback(InsertCountInc, delta)  【§8 feedback】
        │
        └── blocked_registry_->NotifyAll()                              【§5 release】
              │
              ▼
        iterate pending_, calling retry() one by one
              ├── if Decode succeeds: clear is_currently_blocked_
              │                   HandleHeaders() → DrainPendingFrames
              └── if still blocked : blocked_registry_->Add(self, retry)


═══════════════════════════════════════ ④ decoder stream receive path ═══════════════════════════════════════

  the peer sends decoder stream bytes
        │
        ▼
  QpackDecoderReceiverStream::OnData ──► parse the three feedback kinds
        │
        ├── Section Ack(stream_id)     → blocked_registry_->AckByStreamId
        ├── Stream Cancellation        → blocked_registry_->RemoveByStreamId
        └── Insert Count Increment(d)  → tells the peer: we have inserted up to entry X
                                         (advances known-received-count only; doesn't touch
                                          the local dynamic table)
```

---

## 2. Data-Structure Layout

The QPACK implementation splits into 5 files, each owning one thing:

| File | Responsibility |
|---|---|
| `static_table.{h,cpp}` | the 99-entry static table of RFC 9204 Appendix A, a read-only singleton. |
| `dynamic_table.{h,cpp}` | the single-end dynamic table, pure compression state — `deque<HeaderItem>` + `unordered_map<(name,value), deque_position>` + a monotonically increasing `total_insert_count_`. |
| `blocked_registry.{h,cpp}` | decode-side only — holds the "retry when the IIC arrives" header blocks, indexed by the composite key `(stream_id, section_no)`. |
| `qpack_encoder.{h,cpp}` | **serves both the encoder and decoder roles**. The Encoder name is historical baggage; Decode/DecodeEncoderInstructions live in the same class. |
| `qpack_constants.h` | all wire-format constants of RFC 9204 §4.3/§4.5 (patterns, masks, prefixes), hand-expanded into `constexpr uint8_t`. |

Why is `QpackEncoder` dual-role? Because in an H3 connection **each end holds a pair** of `qpack_encoder_` / `qpack_decoder_`: its own `qpack_encoder_` calls `Encode()`, its own `qpack_decoder_` calls `Decode()`, yet both instantiate **the same class**. This leads to an easily-missed subtlety — `SetMaxTableCapacity` actually sets "how big a table my send side may use as encoder", while **how big a table I tolerate from the peer as decoder** comes from `SETTINGS_QPACK_MAX_TABLE_CAPACITY`; the two values need not match. See §9.

---

## 3. Encoding: The Two-Pass Strategy

`QpackEncoder::Encode` is one of the few "explicitly two-pass algorithms" in quicX. It exists not for performance but for **correctness**: RFC 9204 §4.5 requires the header block prefix at the very front of the payload, but the RIC it contains depends on the maximum dynamic-table reference across all fields in the payload — uncomputable without a full scan first.

The implementation is at `qpack_encoder.cpp:13-228`, key skeleton:

```text
Pass 1 (decisions + side effects):
  for each header in ordered_headers:
      try static exact      → kStaticIndexed
      try static name-only  → kStaticNameRef
      try dynamic exact     → kDynamicIndexed / kDynamicPostBaseIndexed
      otherwise:
          if can_insert_to_dynamic_table:
              dynamic_table_.AddHeaderItem(name, value)         ← side effect 1
              instruction_sender_({...})                        ← side effect 2: encoder stream
              kDynamicIndexed (refer to the just-inserted entry)
          else:
              kLiteralNoNameRef
      max_required_insert_count = max(...)

Pass 2 (write the wire):
  WriteHeaderPrefix(max_required_insert_count, base_insert_count_at_start)
  for each encoding:
      write the wire representation per its EncodeAction
```

### 3.1 Header Order: Pseudo-Headers First

The `headers` entering `Encode()` is an `unordered_map` with nondeterministic iteration order. RFC 9114 §4.3 requires the pseudo-headers (`:method`, `:scheme`, `:authority`, `:path`, `:status`) up front in a fixed order — so the first thing `Encode()` does is pick out the pseudo-headers in whitelist order, then sort the rest lexicographically. The ordering looks purely "aesthetic", but it solves a real problem: **map iteration order differs across machines, making fuzz tests non-reproducible** (P2-3 in the commit notes).

### 3.2 Static vs Dynamic Table Priority

The matching order is hard-coded: static exact → static name-only → dynamic exact → dynamic insert → fallback literal. The reasoning:

- Static-table indexes never go stale — **always the safest reference** — RIC never rises because of a static reference;
- Every dynamic reference pushes RIC up, creating head-of-line risk on the decode side (§5);
- Hence "if static works, don't use dynamic" — even with a dynamic exact match, it yields to static name-only.

### 3.3 The Dynamic Table's Side Effects Are Double

In Pass 1, a dynamic insert doesn't just mutate the local `dynamic_table_` state — it **simultaneously writes one wire instruction onto the encoder stream** — that's the `instruction_sender_` callback. The two sides' state must stay atomically consistent: the local `total_insert_count_` increments only after push_front, and the peer also does `AddHeaderItem` before computing delta inside `DecodeEncoderInstructions`. Either side skipping a write or writing early misaligns the two ends' RIC views, causing decode failures.

---

## 4. RIC and Base: The Dynamic Table's "Two Timestamps"

The most easily confused QPACK concepts are **Required Insert Count (RIC)** and **Base**. Both are absolute indexes, but with different semantics:

| Field | Meaning | Who Produces It | How It's Used |
|---|---|---|---|
| RIC | the minimum insert count **required** to decode this header block — below it, referenced dynamic entries may not be inserted yet. | encoder: the max dynamic reference's absolute index + 1 within the block. | decoder: must wait for `dynamic_table_.GetInsertCount() >= RIC` before decoding. Otherwise §5 blocks. |
| Base | the dynamic-table origin this block "anchors to" — toward newer is **post-base**, toward older is **pre-base**. | encoder: the `total_insert_count_` **at encoding start**. | decoder: used to restore relative indexes to absolute ones. |

Both use absolute indexes, but RIC ≥ Base is the common case — meaning the block references "the batch just inserted by itself" (post-base references). In the code:

- RIC: `max_required_insert_count` in `qpack_encoder.cpp` (the variable name maps directly).
- Base: `base_insert_count` in `qpack_encoder.cpp`, snapshotted from `dynamic_table_.GetInsertCount()` at the Encode entry (`qpack_encoder.cpp:51`).

### 4.1 RIC's Wire Encoding Is Not Written Directly

RFC 9204 §4.5.1.1 designs a wraparound encoding:

```text
MaxEntries = floor(MaxTableCapacity / 32)
EncodedRIC = (RIC % (2 * MaxEntries)) + 1     when RIC > 0
EncodedRIC = 0                                when RIC == 0
```

Why the detour? RIC grows monotonically and gets large over a long-lived connection; transmitting the modulo both compresses the length and lets the decoder recover the true value without knowing the encoder's latest insert count (provided the decoder's own insert count is within MaxEntries of the RIC — otherwise no unique truth exists). That's why SETTINGS pre-negotiates `QPACK_MAX_TABLE_CAPACITY` — MaxEntries is jointly negotiated.

The implementation is at `qpack_encoder.cpp:783-796`. Note `max_entries == 0` degrades to 1, avoiding a `RIC % 0` crash.

---

## 5. Blocking Coordination: the Whole Job of blocked_registry

The decode side's thorniest problem: **the HEADERS frame has arrived, but the dynamic entries it references haven't been inserted yet**. This is detected by `RIC > dynamic_table_.GetInsertCount()`, and QpackEncoder::Decode returns false.

The naive approach would be an error and connection close — but QPACK deliberately permits this "reordering", because the encoder stream and the request stream are two independent QUIC streams whose bytes aren't guaranteed to reach the decoder in encoder-write order. So the decoder must "park" the header block and retry once the dynamic table catches up.

### 5.1 The Three Things That Must Be Done

The retry path of `req_resp_base_stream.cpp::HandleHeaders` solves three independent correctness problems (numbered 1/2/3 in the comments):

**Problem 1: the retry must fill `self->headers_`, not a local map.**
The first version's retry wrote its decode result into a lambda-local `tmp`, leaving `headers_` still empty after a successful retry — the application received a request **with no headers at all**. The fix: pass the `self->headers_` captured via weak_ptr as the output parameter.

**Problem 2: a failed retry must re-register into the registry.**
One `NotifyAll` invokes *all* pending callbacks, but **this IIC delta may not cover your RIC** — other blocks might unblock at +1 while you need +5. If the retry doesn't re-`Add`, the next IIC can't find you and the header block is lost forever. The fix: `if (still_blocked) blocked_registry_->Add(self, retry)` at the retry's end.

**Problem 3: subsequent frames arriving during the block must queue in order.**
DATA frames and later HEADERS frames (trailers) can all arrive after the blocked HEADERS. Dispatching them straight to the application means **the app sees the body before the header** — broken HTTP semantics. So once `is_currently_blocked_` is set, `OnData/ProcessFrames` stuffs subsequent frames into the `pending_blocked_frames_` queue, replayed in order by `DrainPendingFrames` after the retry succeeds.

### 5.2 The Buffer Snapshot: Preventing the Retry From Reading the Wrong Position

QPACK Decode is a streaming parse that advances the buffer's read pointer. If the first failed decode already consumed a few bytes (say the RIC prefix), the second retry starting from the raw buffer would decode from mid-stream — garbage.

The fix: snapshot **before** the first attempt:

```cpp
auto encoded_fields_template = encoded_fields->CloneReadable(
    encoded_fields->GetDataLength(), /*move_write_pt=*/false);
```

`move_write_pt=false` is the key — it lets the clone share the underlying chunk but **not consume** the source buffer's read pointer. Each retry then clones a fresh readable view off the template, yielding a clean buffer whose read pointer sits at the prefix start. See `req_resp_base_stream.cpp:289-303`.

### 5.3 The Semantic Trap of max_blocked_streams

The semantics of `SetMaxBlockedStreams(0)` are defined by RFC 9204 §5: "peer's encoder MUST NOT cause any stream to become blocked" — i.e. **equivalent to "blocking entirely forbidden"**, every `Add` returns false. For "unlimited" you must pass `UINT64_MAX` explicitly. This counter-intuitive convention is spelled out in the comment at `blocked_registry.h:26-29`.

Subtler still is the `max_blocked_explicit_` bool: false by construction, meaning **a registry that never had SetMaxBlockedStreams called is unlimited**. This fallback exists for backward compatibility with old code paths (registries constructed before SETTINGS were known). New code should always call `SetMaxBlockedStreams` explicitly, treating 0 as "blocking forbidden".

---

## 6. The Dynamic Table Itself: deque + map + Absolute Indexes

`DynamicTable` is a ~170-line small class, yet it steps on every spot where QPACK most easily breeds bugs.

### 6.1 The Three Index Perspectives

```text
deque_position    : front=0=newest entry, size()-1=oldest entry (position after eviction shrinks the deque)
absolute_index    : the RFC 9204 concept, monotonically increasing, never reused after evict
total_insert_count: entries inserted so far (never decreases; equals the historical absolute_index upper bound + 1)
evicted_count     : total_insert_count - deque.size()
```

Their relationships:

```text
absolute_index_of_front = total_insert_count − 1
absolute_index_of_back  = evicted_count
deque_position(abs)     = (total_insert_count − 1) − abs_idx
```

### 6.2 The Fix History of `FindHeaderItemByAbsoluteIndex`

The original conversion was `deque_pos = total_insert_count - 1 - absolute_index`, correct **without eviction**; but once eviction happens, the deque shrinks from the back and old entries' absolute indexes get "buried" — the formula then reads out of bounds or fetches the wrong entry.

The fix at `dynamic_table.cpp:60-80`: first compute `evicted_count = total_insert_count - deque.size()`; an absolute index outside `[evicted_count, total_insert_count)` returns nullptr directly (entry evicted). That's the P1-1 bugfix in the comments.

### 6.3 push_front + Whole-Map Index Shifting

Every `AddHeaderItem` pushes the new entry onto the deque front, shifting every old entry's deque position +1. The `headeritem_index_map_` (`(name,value) → deque_pos`) must shift wholesale:

```cpp
for (auto& kv : headeritem_index_map_) {
    kv.second += 1;
}
```

This O(N) shift looks ugly, but QPACK dynamic tables are naturally small (typically 4-8KB, tens to hundreds of entries), measured far below hash-table rebuild cost.

Subtler is the eviction handling: `headeritem_index_map_` can contain **a newer entry with the same name and value** (say cookie=abc inserted twice; the second push_front overwrites the first's map slot to the new position 0, while the old position lives on mid-deque). So when evicting an old entry, **only delete the map slot when the slot points at exactly the evicted position** — otherwise you'd wrongly delete the newer entry's index:

```cpp
if (it != headeritem_index_map_.end() && it->second == evicted_index) {
    headeritem_index_map_.erase(it);
}
```

### 6.4 Duplicate's Dangling-Reference Trap

`DuplicateEntry(absolute_index)` implements RFC 9204 §4.3.4's Duplicate instruction — essentially "copy the entry at absolute_index and insert the copy as a new entry at the table head". The intuitive implementation:

```cpp
HeaderItem* item = FindHeaderItemByAbsoluteIndex(absolute_index);
return AddHeaderItem(item->name_, item->value_);   // BUG
```

But `AddHeaderItem` internally calls `EvictEntries()` to make room — and the copied `item` **may itself be the one about to be evicted**! Eviction then frees/reuses the memory behind item, and name/value become dangling references.

The fix is plain: copy the strings out first, then Add:

```cpp
std::string name = item->name_;
std::string value = item->value_;
return AddHeaderItem(name, value);
```

That's the entirety of `dynamic_table.cpp:143-160`. One comment line longer than the code.

---

## 7. The encoder stream Wire Format and Capacity Negotiation

`DecodeEncoderInstructions` is the entry point for receiving encoder-stream bytes and landing them in the local dynamic table. RFC 9204 §4.3 defines 4 instruction kinds:

| Instruction | High-Bit Pattern | Prefix | Implementation Entry |
|---|---|---|---|
| Insert With Name Reference | `1Sxxxxxx` | 6-bit | `DecodeEncoderInstructions` branch 1 |
| Insert Without Name Reference | `01xxxxxx` | 6-bit | branch 2 |
| Set Dynamic Table Capacity | `001xxxxx` | 5-bit | `UpdateMaxTableSize` |
| Duplicate | `0001xxxx` | 4-bit | `DuplicateEntry` |

The S bit (Static bit, only on Insert With Name Reference): 1 = the name comes from the static table; 0 = a relative index into the dynamic table (`relative = ric - 1 - abs_idx`).

Here lies an easily-missed subtlety: **in the dynamic branch of Insert With Name Reference, the relative index is relative to *the encoder's current insert count*** — not the decoder's. Upon receipt, the decoder must restore the absolute index using its own just-advanced `total_insert_count_`. That's why `qpack_encoder.cpp:529` takes `dynamic_table_.GetInsertCount()` as the base when writing, and nothing else.

### 7.1 Capacity Negotiation: local vs peer

`max_table_capacity_` is **min(local, peer)** — RFC 9204 §3.2.3 explicitly forbids either side exceeding its own limit. The implementation splits into three fields (`qpack_encoder.h:30-45`):

```text
local_max_table_capacity_   : my configured cap (usually from Http3Settings)
peer_max_table_capacity_    : filled once the peer's SETTINGS arrive
peer_cap_known_             : whether the peer's SETTINGS have been received
max_table_capacity_         : the effective value = min(local, peer) (when peer known) or local (when unknown)
```

Both `SetLocalMaxTableCapacity` / `SetPeerMaxTableCapacity` call `RecomputeMaxTableCapacity`, which contains one crucial implicit side effect:

```cpp
dynamic_table_.UpdateMaxTableSize(max_table_capacity_);
```

Without this line there's a bug — construction with `dynamic_table_(1024)` already pinned max at 1024; even if SETTINGS negotiates 16KB, forgetting to sync it down to the underlying dynamic_table means AddHeaderItem rejects inserts "over 1024" while the upper Encode has already decided to Insert and emitted the wire instruction — ending with the entry in the peer's dynamic table but not the local one, a **permanent two-end desync**. The bugfix comment at `qpack_encoder.h:93-101` is quite detailed and worth reading.

---

## 8. The Three Feedback Kinds on the decoder stream and Their Three Triggers

`QpackDecoderReceiverStream::OnData` parses decoder-stream bytes into three feedback kinds:

| Kind | Trigger | Handling |
|---|---|---|
| Section Acknowledgment | the decoder finishes a header block with RIC > 0 | encoder side: `blocked_registry_->AckByStreamId` — but note, this repo's encoder itself **doesn't maintain a blocked_registry** (that's a decoder-side concept); here the peer's SectionAck is really consumed as a known-received-count advancement signal. |
| Stream Cancellation | a decoder-side stream is reset by the peer | encoder side: deduct from known-received-count, reset that stream's outstanding sections. |
| Insert Count Increment | the decoder side has new entries inserted up to a position *the encoder wrote* | encoder side: advance known-received-count, letting the encoder know which dynamic entries have been "confirmed received" by the peer and may be referenced freely without blocking risk. |

The unified entry for emitting feedback is `EmitDecoderFeedback(type, value)` (`qpack_encoder.h:65`), which hands the bytes to the `decoder_feedback_sender_` callback — bound at the `H3Connection::Init` stage to the decoder stream's send method.

### 8.1 The RIC=0 Exception for Section Ack

RFC 9204 §4.4.1 is explicit: "The decoder MUST NOT emit Section Acknowledgment for header blocks with a Required Insert Count of 0". The reason: a RIC=0 header block references only the static table, and the encoder needs no tracking of it at all — sending a Section Ack anyway would corrupt the encoder's known-received-count state machine.

The implementation is at `req_resp_base_stream.cpp:344-349, 385-392`: after each decoded header block, check `last_decoded_ric_` first; only >0 emits.

### 8.2 The Exact IIC delta Computation

`QpackEncoderReceiverStream::ParseEncoderInstructions` (note the name is counter-intuitive — it's the *Encoder* Receiver Stream: it receives the peer's encoder stream, but the local end hands the bytes to its own `qpack_encoder_` instance) must compute "how many new entries this batch added":

```cpp
uint64_t insert_count_before = qpack_encoder_->GetInsertCount();
qpack_encoder_->DecodeEncoderInstructions(buffer);
uint64_t insert_count_after = qpack_encoder_->GetInsertCount();
uint64_t delta = insert_count_after - insert_count_before;

if (delta > 0) {
    qpack_encoder_->EmitDecoderFeedback(kInsertCountInc, delta);
}
```

Why not emit one IIC per insert inside `DecodeEncoderInstructions`? Because peers commonly send several Insert instructions in one batch (even mixed with Set Capacity + Duplicate) — emitting an IIC per insert would explode the decoder-stream byte count, and IIC's semantics are "cumulative delta", suited to batching. That's why this count-diff step belongs in the stream layer, not inside qpack_encoder.

---

## 9. Boundaries with Upstream/Downstream

QPACK in quicX cares only about wire bytes and dynamic-table state, explicitly **not** about:

| What It Doesn't Care About | Where It's Handled |
|---|---|
| HEADERS frame encapsulation/decapsulation | `headers_frame.{h,cpp}` (the HTTP/3 frame layer) |
| whether HEADERS are well-formed (required pseudo-headers, value legality) | `req_resp_base_stream` + the upper router |
| QUIC stream congestion, retransmission | `quic/stream/`, `quic/connection/controller/send_control` |
| H3 SETTINGS negotiation | `HandleSettings` in `connection_client/server.cpp` |
| case normalization | `qpack_encoder.cpp:91-93` does an explicit `std::tolower`, since the static table is all-lowercase |

Conversely, QPACK never calls any upper-layer API on its own — every "I want to send an encoder instruction" goes up via the `instruction_sender_` callback, every "I want to send decoder feedback" via `decoder_feedback_sender_`. These two std::functions are bound by the H3 connection at init to the corresponding unidirectional streams. This callback-injected decoupling lets the QPACK module run in tests without real QUIC streams by mocking the two senders — exactly what `qpack_dynamic_table_e2e_test.cpp` does, running the whole flow in memory.

---

## 10. Verification Entry Points

| Test File | Coverage |
|---|---|
| `test/unit_test/http3/qpack/qpack_dynamic_table_e2e_test.cpp` | end-to-end: dual encoder + decoder instances, covering insert / lookup / evict / RIC blocking / Section Ack / IIC across the whole chain (the largest single test, 98 KB). |
| `test/unit_test/http3/qpack/qpack_dynamic_update_test.cpp` | evict behavior after Set Dynamic Table Capacity. |
| `test/unit_test/http3/stream/qpack_blocked_inline_data_test.cpp` | blocking + DATA frame queuing and replay (§5.1 problem 3). |
| `test/unit_test/http3/stream/qpack_encoder_stream_test.cpp` | encoder stream wire-format encode/decode. |
| `test/unit_test/http3/stream/qpack_decoder_stream_test.cpp` | parsing of the three decoder-stream feedback kinds. |
| `test/unit_test/http3/frame/qpack_encoder_frames_test.cpp` | frame-level codec for the 4 encoder instruction kinds. |
| `test/unit_test/http3/frame/qpack_decoder_frames_test.cpp` | frame-level codec for the 3 decoder feedback kinds. |
| `test/perf/qpack_perf_test.cpp` | Encode/Decode throughput and dynamic-table hit rate. |

Common entry points for debugging QPACK problems:

- The peer reports `QPACK_DECOMPRESSION_FAILED` → check §6.2's absolute-index conversion, or §7.1's capacity negotiation;
- HEADERS never reach the application → check §5's blocking coordination, especially problems 1 (headers_ not filled) and 2 (retry not re-Added) in §5.1;
- Unbounded memory growth → check §6's whether evict is correctly triggered by capacity negotiation (especially the `dynamic_table_.UpdateMaxTableSize` line in §7.1);
- Sporadic garbage decodes → check §5.2's buffer snapshot.

---

## 11. Key Invariants

When hunting QPACK bugs, these invariants are the first things to verify:

1. **`total_insert_count_` increases monotonically and never regresses** — even if `UpdateMaxTableSize(0)` evicts the entire dynamic table, `total_insert_count_` keeps its value. RIC encoding depends on this property.
2. **The wire instructions written by the encoder stay strictly in sync with the local `dynamic_table_` state** — call `instruction_sender_` immediately after `AddHeaderItem` succeeds; nothing that can throw may sit between the two.
3. **`max_table_capacity_` is always = min(local, peer)** — assigned only in `RecomputeMaxTableCapacity`, and must be flushed down to `dynamic_table_` at the same time.
4. **`is_currently_blocked_` and `pending_blocked_frames_` live and die together** — when setting the flag, the retry must already be successfully Added to the registry; when clearing it, DrainPendingFrames must run immediately.
5. **`last_decoded_ric_` is updated at the end of every `Decode()`** — the upper layer's "send a Section Ack or not" depends entirely on this value; a missed update makes Section Acks never sent or sent randomly.
6. **`base_insert_count` is snapshotted from `GetInsertCount()` at the Encode entry** — Pass 1's dynamic inserts push `total_insert_count_` up, but Base must stay at the encoding origin.

---

## 12. Related Documents

- [`packet_lifecycle.md`](packet_lifecycle.md) §6 how QUIC stream bytes reach the H3 layer.
- [`handshake_state_machine.md`](handshake_state_machine.md) at which handshake stage H3 SETTINGS arrive and how QPACK hooks in.
- [`ownership_and_memory.md`](ownership_and_memory.md) the shared-chunk semantics of `CloneReadable` (§5.2's buffer snapshot depends on it).

---

## 13. Related RFCs

| RFC | Section | Content |
|---|---|---|
| RFC 9204 | §3.2 | dynamic table semantics, capacity negotiation. |
| RFC 9204 | §3.2.4 | absolute indexing (§4-§6). |
| RFC 9204 | §4.3 | the 4 encoder-stream instruction kinds (§7). |
| RFC 9204 | §4.4 | the 3 decoder-stream feedback kinds (§8). |
| RFC 9204 | §4.5 | header block prefix + RIC/Base encoding (§4). |
| RFC 9204 | §2.1.4 | blocking and the RIC comparison (§5). |
| RFC 9204 | §5 | SETTINGS parameters (QPACK_MAX_TABLE_CAPACITY, QPACK_BLOCKED_STREAMS). |
| RFC 9204 | Appendix A | the static table (99 entries). |
| RFC 7541 | §4.1 | the entry-size formula `name + value + 32` — reused directly by quicX's `CalculateEntrySize`. |
| RFC 9114 | §4.3 | pseudo-header ordering (§3.1). |
