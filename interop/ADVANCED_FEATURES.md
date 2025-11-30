# Advanced QUIC Features Implementation Guide

This document provides detailed implementation guidance for advanced QUIC features required by the interop test suite.

## Table of Contents

1. [QLOG Support](#qlog-support)
2. [SSLKEYLOG Support](#sslkeylog-support)
3. [Retry Mechanism](#retry-mechanism)
4. [Session Resumption](#session-resumption)
5. [0-RTT Support](#0-rtt-support)
6. [Connection Migration](#connection-migration)
7. [Key Update](#key-update)
8. [ECN Support](#ecn-support)

---

## QLOG Support

QLOG provides structured logging of QUIC events for debugging and analysis.

### Specification
- [QLOG Schema](https://datatracker.ietf.org/doc/draft-ietf-quic-qlog-main-schema/)
- [QUIC Event Definitions](https://datatracker.ietf.org/doc/draft-ietf-quic-qlog-quic-events/)

### Implementation Required

#### 1. QLOG Writer Class

```cpp
// File: src/quic/qlog/qlog_writer.h
#ifndef QUIC_QLOG_WRITER_H
#define QUIC_QLOG_WRITER_H

#include <string>
#include <fstream>
#include <vector>
#include <chrono>
#include "nlohmann/json.hpp"

namespace quicx {

class QlogWriter {
public:
    QlogWriter(const std::string& connection_id);
    ~QlogWriter();

    void SetOutputPath(const std::string& path);
    void Enable(bool enabled);

    // Connection events
    void LogConnectionStarted(const std::string& local_addr, const std::string& remote_addr);
    void LogConnectionClosed(uint64_t error_code, const std::string& reason);

    // Packet events
    void LogPacketSent(const PacketHeader& header, size_t size);
    void LogPacketReceived(const PacketHeader& header, size_t size);
    void LogPacketLost(uint64_t packet_number);

    // Frame events
    void LogFrameCreated(const QuicFrame& frame);
    void LogFrameProcessed(const QuicFrame& frame);

    // Congestion control
    void LogCongestionStateUpdate(const std::string& state, uint64_t cwnd, uint64_t in_flight);
    void LogMetricsUpdate(uint64_t min_rtt, uint64_t smoothed_rtt, uint64_t latest_rtt);

    // Stream events
    void LogStreamCreated(uint64_t stream_id, const std::string& stream_type);
    void LogStreamClosed(uint64_t stream_id);
    void LogStreamDataReceived(uint64_t stream_id, size_t length, bool fin);

    void Flush();

private:
    struct QlogEvent {
        uint64_t relative_time_us;
        std::string category;
        std::string event_type;
        nlohmann::json data;
    };

    std::string connection_id_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<QlogEvent> events_;
    std::string output_path_;
    bool enabled_;

    void AddEvent(const std::string& category, const std::string& event_type,
                  const nlohmann::json& data);
    uint64_t GetRelativeTimeUs() const;
};

} // namespace quicx

#endif
```

#### 2. QLOG Writer Implementation

```cpp
// File: src/quic/qlog/qlog_writer.cpp
#include "qlog_writer.h"
#include <iomanip>

namespace quicx {

QlogWriter::QlogWriter(const std::string& connection_id)
    : connection_id_(connection_id)
    , start_time_(std::chrono::steady_clock::now())
    , enabled_(false) {
}

QlogWriter::~QlogWriter() {
    if (enabled_ && !output_path_.empty()) {
        Flush();
    }
}

void QlogWriter::SetOutputPath(const std::string& path) {
    output_path_ = path;
}

void QlogWriter::Enable(bool enabled) {
    enabled_ = enabled;
}

void QlogWriter::LogConnectionStarted(const std::string& local_addr,
                                     const std::string& remote_addr) {
    if (!enabled_) return;

    nlohmann::json data = {
        {"local", local_addr},
        {"remote", remote_addr},
        {"connection_id", connection_id_}
    };
    AddEvent("connectivity", "connection_started", data);
}

void QlogWriter::LogPacketSent(const PacketHeader& header, size_t size) {
    if (!enabled_) return;

    nlohmann::json data = {
        {"packet_type", header.type},
        {"packet_number", header.packet_number},
        {"packet_size", size}
    };
    AddEvent("transport", "packet_sent", data);
}

void QlogWriter::LogFrameProcessed(const QuicFrame& frame) {
    if (!enabled_) return;

    nlohmann::json data = {
        {"frame_type", frame.type}
    };
    // Add frame-specific fields based on type
    AddEvent("transport", "frame_processed", data);
}

void QlogWriter::Flush() {
    if (output_path_.empty() || events_.empty()) return;

    // Build QLOG JSON structure
    nlohmann::json qlog = {
        {"qlog_version", "0.3"},
        {"title", "quicX qlog"},
        {"description", "QUIC connection log"},
        {"trace", {
            {"vantage_point", {{"type", "server"}}},  // or "client"
            {"configuration", {
                {"time_units", "us"},
                {"time_offset", 0}
            }},
            {"common_fields", {
                {"ODCID", connection_id_}
            }},
            {"events", nlohmann::json::array()}
        }}
    };

    // Add all events
    for (const auto& event : events_) {
        qlog["trace"]["events"].push_back({
            event.relative_time_us,
            event.category,
            event.event_type,
            event.data
        });
    }

    // Write to file
    std::ofstream file(output_path_);
    file << std::setw(2) << qlog << std::endl;
    file.close();

    events_.clear();
}

void QlogWriter::AddEvent(const std::string& category,
                         const std::string& event_type,
                         const nlohmann::json& data) {
    QlogEvent event;
    event.relative_time_us = GetRelativeTimeUs();
    event.category = category;
    event.event_type = event_type;
    event.data = data;
    events_.push_back(event);
}

uint64_t QlogWriter::GetRelativeTimeUs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count();
}

} // namespace quicx
```

#### 3. Integration with Connection

```cpp
// In BaseConnection class
class BaseConnection {
private:
    std::unique_ptr<QlogWriter> qlog_writer_;

public:
    void EnableQlog(const std::string& output_dir) {
        std::string conn_id = GetConnectionId();
        qlog_writer_ = std::make_unique<QlogWriter>(conn_id);
        qlog_writer_->SetOutputPath(output_dir + "/" + conn_id + ".qlog");
        qlog_writer_->Enable(true);
    }

    void OnPacketSent(const Packet& packet) {
        // ... existing code ...
        if (qlog_writer_) {
            qlog_writer_->LogPacketSent(packet.header, packet.size);
        }
    }
};
```

#### 4. Environment Variable Support

```cpp
// In interop_server.cpp and interop_client.cpp
const char* qlog_env = std::getenv("QLOGDIR");
if (qlog_env) {
    connection->EnableQlog(qlog_env);
}
```

---

## SSLKEYLOG Support

### Current Status
✅ **Implemented** - Basic file writing support added

### Remaining Work: BoringSSL Integration

```cpp
// File: src/quic/crypto/tls_context.h
class TlsContext {
public:
    void SetKeylogCallback(FILE* keylog_file) {
        if (keylog_file && ssl_ctx_) {
            SSL_CTX_set_keylog_callback(ssl_ctx_, KeylogCallback);
            keylog_file_ = keylog_file;
        }
    }

private:
    static void KeylogCallback(const SSL* ssl, const char* line) {
        // Get TlsContext from SSL object
        TlsContext* ctx = static_cast<TlsContext*>(
            SSL_get_ex_data(ssl, GetExDataIndex()));

        if (ctx && ctx->keylog_file_) {
            fprintf(ctx->keylog_file_, "%s\n", line);
            fflush(ctx->keylog_file_);
        }
    }

    FILE* keylog_file_ = nullptr;
    SSL_CTX* ssl_ctx_ = nullptr;
};
```

### Usage

```bash
# Server
SSLKEYLOGFILE=/logs/keys.log ./bin/interop_server

# Client
SSLKEYLOGFILE=/logs/keys.log ./bin/interop_client

# Decrypt with Wireshark
wireshark capture.pcap -o tls.keylog_file:/logs/keys.log
```

---

## Retry Mechanism

### Purpose
Validates client address before committing server resources (anti-amplification).

### Implementation Required

#### 1. Retry Token Generation

```cpp
// File: src/quic/crypto/retry_token.h
class RetryToken {
public:
    static std::vector<uint8_t> Generate(
        const Address& client_addr,
        const ConnectionId& original_dcid,
        const uint8_t* secret, size_t secret_len);

    static bool Validate(
        const std::vector<uint8_t>& token,
        const Address& client_addr,
        const ConnectionId& original_dcid,
        const uint8_t* secret, size_t secret_len);

private:
    // AEAD encryption of: timestamp || client_ip || original_dcid
    // Secret rotated periodically for security
};
```

#### 2. Server-side Logic

```cpp
class BaseConnection {
public:
    void OnInitialPacket(const Packet& packet) {
        if (force_retry_ && !packet.has_retry_token) {
            // Generate and send Retry packet
            auto token = RetryToken::Generate(
                client_addr_, packet.source_cid, retry_secret_, 32);
            SendRetryPacket(packet.source_cid, token);
            return;
        }

        if (packet.has_retry_token) {
            // Validate token
            if (!RetryToken::Validate(packet.retry_token, client_addr_,
                                     packet.original_dcid, retry_secret_, 32)) {
                // Invalid token - close connection
                Close(INVALID_TOKEN);
                return;
            }
        }

        // Proceed with handshake
        ProcessInitialPacket(packet);
    }

private:
    bool force_retry_ = false;
    uint8_t retry_secret_[32];
};
```

#### 3. Client-side Logic

```cpp
class ClientConnection {
public:
    void OnRetryPacket(const Packet& packet) {
        // Save retry token
        retry_token_ = packet.retry_token;
        original_dcid_ = packet.original_dcid;

        // Reset connection state
        ResetConnection();

        // Reconnect with retry token
        SendInitialPacket(retry_token_, original_dcid_);
    }

private:
    std::vector<uint8_t> retry_token_;
    ConnectionId original_dcid_;
};
```

#### 4. Configuration

```cpp
// Server config
Http3ServerConfig config;
config.config_.force_retry_ = true;  // Force retry for all connections
```

---

## Session Resumption

### Purpose
Resume previous TLS session to avoid full handshake (1-RTT instead of 2-RTT).

### Implementation Required

#### 1. Session Cache (Client)

```cpp
// File: src/quic/crypto/session_cache.h
class SessionCache {
public:
    void SaveSession(const std::string& server_name,
                    const uint8_t* session_data, size_t len);

    bool LoadSession(const std::string& server_name,
                    std::vector<uint8_t>& session_data);

    void ClearSession(const std::string& server_name);

private:
    std::unordered_map<std::string, std::vector<uint8_t>> sessions_;
    std::string cache_file_;

    void LoadFromFile();
    void SaveToFile();
};
```

#### 2. BoringSSL Integration

```cpp
// Client
class TlsClientContext {
public:
    void EnableSessionResumption(SessionCache* cache) {
        session_cache_ = cache;

        // Try to load previous session
        std::vector<uint8_t> session_data;
        if (cache->LoadSession(server_name_, session_data)) {
            SSL_SESSION* session = d2i_SSL_SESSION(
                nullptr, &session_data[0], session_data.size());
            if (session) {
                SSL_set_session(ssl_, session);
                SSL_SESSION_free(session);
            }
        }

        // Set callback to save new session
        SSL_CTX_sess_set_new_cb(ssl_ctx_, NewSessionCallback);
    }

private:
    static int NewSessionCallback(SSL* ssl, SSL_SESSION* session) {
        TlsClientContext* ctx = GetContext(ssl);

        // Serialize session
        uint8_t* data = nullptr;
        size_t len = i2d_SSL_SESSION(session, &data);

        // Save to cache
        ctx->session_cache_->SaveSession(ctx->server_name_, data, len);
        OPENSSL_free(data);

        return 1;
    }

    SessionCache* session_cache_ = nullptr;
};

// Server
class TlsServerContext {
public:
    void EnableSessionResumption() {
        // Generate session ticket key (rotate periodically)
        RAND_bytes(ticket_key_, sizeof(ticket_key_));

        SSL_CTX_set_session_cache_mode(ssl_ctx_,
            SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL);

        SSL_CTX_set_tlsext_ticket_key_cb(ssl_ctx_, TicketKeyCallback);
    }

private:
    uint8_t ticket_key_[48];  // 16 name + 32 key
};
```

#### 3. Test Verification

```cpp
bool IsSessionResumed(SSL* ssl) {
    return SSL_session_reused(ssl) == 1;
}
```

---

## 0-RTT Support

### Purpose
Send application data in first flight (0-RTT) using resumed session.

### Implementation Required

#### 1. Server Configuration

```cpp
Http3ServerConfig config;
config.config_.enable_0rtt_ = true;
config.config_.max_early_data_size_ = 16384;  // Max 0-RTT data
```

#### 2. Client API

```cpp
class IRequest {
public:
    void SetEarlyData(bool enabled);
    bool IsEarlyData() const;
};

// Usage
auto request = IRequest::Create();
request->SetEarlyData(true);
client->DoRequest(url, HttpMethod::kGet, request, handler);
```

#### 3. Server Early Data Handling

```cpp
class BaseConnection {
public:
    void OnEarlyData(const uint8_t* data, size_t len) {
        if (!enable_0rtt_) {
            // Reject early data
            RejectEarlyData();
            return;
        }

        // Check replay protection
        if (IsReplay(data, len)) {
            RejectEarlyData();
            return;
        }

        // Process early data
        ProcessApplicationData(data, len, true);
    }

private:
    bool IsReplay(const uint8_t* data, size_t len) {
        // Anti-replay using timestamp + unique token
        // See RFC 8446 Section 8
        return false;
    }
};
```

#### 4. BoringSSL Integration

```cpp
// Client
SSL_set_early_data_enabled(ssl_, 1);

// Server
SSL_CTX_set_early_data_enabled(ssl_ctx_, 1);
SSL_CTX_set_max_early_data(ssl_ctx_, max_early_data_size_);
```

---

## Connection Migration

### Purpose
Continue connection when client's IP address or port changes.

### Implementation Required

#### 1. Path Validation

```cpp
class PathValidator {
public:
    uint64_t StartValidation(const Address& new_path);
    bool ValidateChallengeResponse(uint64_t challenge_id,
                                  const uint8_t* response);

private:
    struct PendingPath {
        Address address;
        uint64_t challenge;
        std::chrono::steady_clock::time_point sent_time;
    };

    std::unordered_map<uint64_t, PendingPath> pending_validations_;
};
```

#### 2. Migration API

```cpp
class IQuicConnection {
public:
    // Client initiates migration
    virtual bool MigrateTo(const std::string& new_local_addr) = 0;

    // Both sides handle path change
    virtual void OnPathChange(const Address& new_remote) = 0;
};
```

#### 3. Implementation

```cpp
class BaseConnection {
public:
    bool MigrateTo(const std::string& new_local_addr) {
        // Bind to new local address
        Address new_addr = ResolveAddress(new_local_addr);

        // Send PATH_CHALLENGE on new path
        uint64_t challenge = GeneratePathChallenge();
        SendPathChallenge(new_addr, challenge);

        path_validator_.StartValidation(new_addr);
        return true;
    }

    void OnPathChallengeFrame(const PathChallengeFrame& frame) {
        // Respond on same path
        SendPathResponse(frame.data);
    }

    void OnPathResponseFrame(const PathResponseFrame& frame) {
        // Validate response
        if (path_validator_.ValidateChallengeResponse(
                frame.challenge_id, frame.data)) {
            // Switch to new path
            remote_address_ = validated_path_;
            OnPathChange(remote_address_);
        }
    }
};
```

---

## Key Update

### Purpose
Refresh encryption keys during long-lived connections for security.

### Implementation Required

#### 1. API

```cpp
class IQuicConnection {
public:
    virtual bool UpdateKeys() = 0;
};
```

#### 2. Implementation

```cpp
class BaseConnection {
public:
    bool UpdateKeys() {
        // Generate new keys using TLS key update
        if (!crypto_->UpdateKeys()) {
            return false;
        }

        // Update packet protection keys
        packet_protection_->UpdateKeys(crypto_->GetNewKeys());

        // Track key phase
        current_key_phase_ = !current_key_phase_;

        return true;
    }

    void OnKeyPhaseChange(bool new_phase) {
        if (new_phase != current_key_phase_) {
            // Peer initiated key update
            // Generate new receiving keys
            crypto_->UpdateReceivingKeys();
            current_key_phase_ = new_phase;
        }
    }

private:
    bool current_key_phase_ = false;
};
```

#### 3. Automatic Trigger

```cpp
class AutoKeyUpdate {
public:
    void OnDataSent(size_t bytes) {
        total_sent_ += bytes;
        if (total_sent_ >= key_update_threshold_) {
            connection_->UpdateKeys();
            total_sent_ = 0;
        }
    }

private:
    uint64_t total_sent_ = 0;
    uint64_t key_update_threshold_ = 100 * 1024 * 1024;  // 100MB
};
```

---

## ECN Support

### Current Status
⚠️ Partial - Config flag exists, needs socket implementation

### Remaining Work

#### 1. Socket ECN Configuration

```cpp
// File: src/common/network/udp_socket.cpp
void UdpSocket::EnableECN() {
    // IPv4
    int tos = INET_ECN_ECT_0;  // ECT(0)
    if (setsockopt(fd_, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
        LOG_ERROR("Failed to set ECN: %s", strerror(errno));
    }

    // Enable ECN reception
    int recvtos = 1;
    setsockopt(fd_, IPPROTO_IP, IP_RECVTOS, &recvtos, sizeof(recvtos));

    // IPv6
    int tclass = INET_ECN_ECT_0;
    setsockopt(fd_, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));

    int recvtclass = 1;
    setsockopt(fd_, IPPROTO_IPV6, IPV6_RECVTCLASS, &recvtclass, sizeof(recvtclass));
}
```

#### 2. ECN Feedback in ACK

```cpp
struct EcnCounts {
    uint64_t ect0_count = 0;
    uint64_t ect1_count = 0;
    uint64_t ce_count = 0;
};

class BaseConnection {
public:
    void OnPacketReceived(const Packet& packet, uint8_t ecn) {
        // Track ECN markings
        switch (ecn & INET_ECN_MASK) {
            case INET_ECN_ECT_0:
                ecn_counts_.ect0_count++;
                break;
            case INET_ECN_ECT_1:
                ecn_counts_.ect1_count++;
                break;
            case INET_ECN_CE:
                ecn_counts_.ce_count++;
                // Congestion experienced - reduce cwnd
                congestion_control_->OnCongestionEvent();
                break;
        }
    }

    void SendAckFrame() {
        AckFrame ack;
        ack.ecn_counts = ecn_counts_;
        SendFrame(ack);
    }

private:
    EcnCounts ecn_counts_;
};
```

---

## Testing Checklist

- [ ] QLOG output generated correctly
- [ ] SSLKEYLOG file contains keys
- [ ] Retry mechanism works
- [ ] Session resumption verified (1-RTT)
- [ ] 0-RTT data accepted
- [ ] Connection migration succeeds
- [ ] Key update works mid-transfer
- [ ] ECN markings sent/received
- [ ] All tests pass against quic-go
- [ ] All tests pass against ngtcp2

---

## References

- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html)
- [QUIC-TLS RFC 9001](https://www.rfc-editor.org/rfc/rfc9001.html)
- [HTTP/3 RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html)
- [QLOG Schema](https://datatracker.ietf.org/doc/draft-ietf-quic-qlog-main-schema/)
- [quic-interop-runner](https://github.com/marten-seemann/quic-interop-runner)
