# Qlog Output Refactor — draft-02 / qvis Compliance

**Date**: 2026-05-23  
**Scope**: `src/common/qlog/**`  
**Goal**: Make `quicX` produce qlog files that are upload-and-parse compliant with [qvis](https://qvis.quictools.info), conforming to `draft-ietf-quic-qlog-main-schema-02` (a.k.a. *draft-02*) over **JSON-Text-Sequences** (RFC 7464).

---

## 1. Motivation

The previous qlog pipeline emitted a hybrid format that did **not** load in qvis:

| Aspect | Old output | qvis result |
| --- | --- | --- |
| File extension | `.qlog` | qvis took the `JSON.parse` path and only saw the first line |
| Format header | `qlog_format: NDJSON`, `qlog_version: 0.4` | non-compliant declaration |
| Record framing | bare lines (no separator) | trace=0, event=0 |
| `time` / `reference_time` | JSON `number` (e.g. `0.000`, `1779507599658`) | "non-compliant qlog draft-02 trace" — schema mandates *string* |
| Header layout | two top-level objects on lines 1 and 2 | qvis cannot reconstruct one trace |
| `protocol_types` | `["QUIC_HTTP3"]` | invalid token |

After staring at qvis loader code + `draft-ietf-quic-qlog-main-schema-02` long enough, every issue above had to be fixed at the source.

## 2. Wire format we now emit

```
<RS=0x1E> <single trace JSON>           "\n"
<RS=0x1E> <event JSON #0>               "\n"
<RS=0x1E> <event JSON #1>               "\n"
...
```

- File extension: **`.sqlog`** (qvis dispatches to the JSON-SEQ parser based on suffix).
- Each record is `0x1E` … JSON … `0x0A`, per RFC 7464 §2.
- The very first record carries the *whole* trace metadata as a **single, valid draft-02 trace object**:

```jsonc
{
  "qlog_format":  "JSON-SEQ",
  "qlog_version": "draft-02",
  "title":        "QuicX client",
  "description":  "QUIC connection trace",
  "trace": {
    "vantage_point":  { "name": "<scid>",     "type": "client" },
    "common_fields":  {
      "protocol_types":  ["QUIC", "HTTP3"],
      "time_format":     "relative",
      "reference_time":  "1779507599657"        // <-- string, ms since unix epoch
    },
    "configuration":  { "time_offset": 0, "time_units": "ms" }
  }
}
```

- Subsequent records are draft-02 event objects:

```jsonc
{ "time": "0", "name": "connectivity:connection_started", "data": { ... } }
```

- `time` is the integer milliseconds since `reference_time`, serialized as a JSON **string** to avoid IEEE-754 precision loss (mandated by draft-02).

## 3. Source-level changes

### 3.1 `src/common/qlog/util/qlog_constants.h`

```cpp
constexpr const char* kQlogVersion       = "draft-02";
constexpr const char* kQlogFormat        = "JSON-SEQ";
constexpr const char* kQlogSchemaQuic    = "urn:ietf:params:qlog:schema:quic";
constexpr char        kJsonSeqRecordSeparator = '\x1E';
```

### 3.2 `src/common/qlog/qlog_config.h`

`CommonFields::protocol_types` default: `{"QUIC_HTTP3"}` → **`{"QUIC", "HTTP3"}`**.

### 3.3 `src/common/qlog/serializer/json_seq_serializer.cpp`

- `SerializeTraceHeader` now writes **one** record (`RS` + JSON + `LF`) containing the merged
  `qlog_format` / `qlog_version` / `title` / `description` plus the nested
  `trace { vantage_point, common_fields, configuration }` sub-object.
- `reference_time` is emitted as a JSON string (`"<ms>"`).
- `SerializeEvent` writes `RS` + `{"time":"<ms>","name":"…","data":{…}}` + `LF`.
  Event time is always derived as `event.time_us / 1000` and stringified.

### 3.4 `src/common/qlog/writer/async_writer.cpp`

Per-connection trace files are now written with the **`.sqlog`** suffix. This is the single line change that flips qvis onto the correct parser path; without it, even byte-perfect JSON-SEQ content is rejected by the loader.

### 3.5 Tests updated to match new format

| File | Update |
| --- | --- |
| `test/unit_test/common/qlog/qlog_async_writer_test.cpp` | Scan for `.sqlog` instead of `.qlog`. |
| `test/unit_test/common/qlog/qlog_e2e_output_test.cpp`   | Header is one record, not two; events start at `lines[1]`; expectations on `time` no longer assume a numeric form. |
| `test/unit_test/common/qlog/qlog_serializer_test.cpp`   | `HeaderLines*` cases collapsed into `HeaderIsIndependentJson` / `HeaderRequiredTopLevelFields` / `HeaderTraceSubObjectRequiredFields`; `time` is asserted as `"\"time\":\"<ms>\""`; `FullQlogOutputIsValidJsonSeq` expects 1 + N records. |
| `test/unit_test/common/qlog/qlog_config_test.cpp`       | `CommonFieldsDefaults` expects `{"QUIC", "HTTP3"}`. |

## 4. Verification

### 4.1 Build

```text
[100%] Linking CXX executable ../../bin/hello_world_server
[100%] Linking CXX executable ../../bin/hello_world_client
```

### 4.2 Runtime

`hello_world_server` + `hello_world_client` produced three traces:

```text
qlog_output_server/2026-05-23-11-43-11_server.sqlog          (server endpoint trace)
qlog_output_server/2026-05-23-11-43-13_11177254.sqlog        (server-side connection)
qlog_output_client/2026-05-23-11-43-13_86923700.sqlog        (client-side connection)
```

### 4.3 Byte-level checks (`xxd` over the first record of each file)

| Check | server endpoint | server connection | client connection |
| --- | --- | --- | --- |
| First byte = `0x1E` | ✓ | ✓ | ✓ |
| `qlog_format: "JSON-SEQ"` | ✓ | ✓ | ✓ |
| `qlog_version: "draft-02"` | ✓ | ✓ | ✓ |
| `protocol_types: ["QUIC","HTTP3"]` | ✓ | ✓ | ✓ |
| `reference_time` is a string | ✓ | ✓ | ✓ |
| `time` is a string (`"0"`, …) | ✓ | ✓ | ✓ |
| Header is a single record | ✓ | ✓ | ✓ |
| File extension `.sqlog` | ✓ | ✓ | ✓ |

### 4.4 qvis

User-confirmed: the regenerated `.sqlog` files load successfully in qvis (no
*"non-compliant qlog draft-02 trace"* warning, traces and events count > 0).

## 5. Build-system gotcha (worth remembering)

During the iteration we hit a confusing case where `hello_world_client` produced
the new format but `hello_world_server` still produced the old NDJSON. Root
cause: qlog sources are compiled into **two** CMake targets — `quicx` and
`http3` — and only the `http3.dir` object files were rebuilt by the incremental
build. The stale `quicx.dir/.../json_seq_serializer.cpp.o` (timestamp older
than the source) was the one being linked.

If you ever modify `src/common/qlog/**` and see inconsistent runtime behaviour
across binaries, force a rebuild:

```bash
rm -f build/CMakeFiles/quicx.dir/src/common/qlog/**/*.o \
      build/CMakeFiles/http3.dir/src/common/qlog/**/*.o
cmake --build build --target hello_world_server hello_world_client -j
```

## 6. Non-goals / Future work

- We still emit *only* `protocol_types: ["QUIC", "HTTP3"]`. If a deployment
  uses pure QUIC without HTTP/3, this should be made configurable via
  `QlogConfig::common_fields`.
- We currently store time as integer milliseconds. draft-02 also allows
  `time_format = absolute` and finer units; switching is a config-only change.
- Per-event `group_id` (multi-stream multiplexing inside one trace) is not
  emitted yet — quicX uses one file per connection so it is unnecessary today.
