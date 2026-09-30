#include "session/transport_map.h"

#include <quicx/quic/type.h>

namespace {

uint32_t VersionFromString(const std::string& v) {
    return (v == "v2") ? quicx::kQuicVersion2 : quicx::kQuicVersion1;
}

}  // namespace

quicx::Http3ClientConfig BuildClientConfig(const TransportOptions& tp, double connect_timeout_s,
    bool verbose) {
    quicx::Http3ClientConfig config;

    // --- TLS ---
    config.quic_config_.verify_peer_ = !tp.insecure;  // curl semantics: verify by default
    config.quic_config_.ca_file_ = tp.cacert;

    // --- QUIC version negotiation (RFC 9000 v1 / RFC 9369 v2) ---
    uint32_t version = VersionFromString(tp.quic_version);
    config.quic_config_.preferred_version_ = version;
    config.quic_config_.supported_versions_ = {version};

    // --- 0-RTT / session resumption ---
    if (!tp.session_cache.empty()) {
        config.quic_config_.enable_session_cache_ = true;
        config.quic_config_.session_cache_path_ = tp.session_cache;
    }
    config.quic_config_.config_.enable_0rtt_ = tp.zero_rtt;

    // --- observability: qlog + keylog ---
    if (!tp.qlog_dir.empty()) {
        config.quic_config_.config_.qlog_config_.enabled_ = true;
        config.quic_config_.config_.qlog_config_.output_dir_ = tp.qlog_dir;
    }
    config.quic_config_.config_.keylog_file_ = tp.keylog_file;

    // --- transport switches ---
    config.quic_config_.config_.enable_ecn_ = tp.ecn;
    config.quic_config_.config_.enable_key_update_ = tp.key_update;

    // --- push ---
    config.enable_push_ = tp.push;

    // --- runtime ---
    config.quic_config_.config_.worker_thread_num_ = 2;
    config.quic_config_.config_.log_level_ = verbose ? quicx::LogLevel::kDebug : quicx::LogLevel::kError;
    if (connect_timeout_s > 0)
        config.connection_timeout_ms_ = static_cast<uint32_t>(connect_timeout_s * 1000);

    return config;
}

quicx::Http3Settings BuildHttp3Settings(const TransportOptions& tp) {
    quicx::Http3Settings settings;  // start from kDefaultHttp3Settings values
    // NOTE: push is NOT set here — Http3Settings has no push field. It is
    // expressed via Http3ClientConfig::enable_push_ / max_push_id_ (MAX_PUSH_ID
    // frame, RFC 9114 §7.2.7), already set in BuildClientConfig above.

    // keep-alive PING lives in transport params (RFC 9000 10.1.2), carried
    // through IClient::Create(settings).
    if (tp.keep_alive_ms > 0) {
        settings.quic_transport_params_.enable_keep_alive_ = true;
        settings.quic_transport_params_.keep_alive_interval_ms_ = tp.keep_alive_ms;
    }
    return settings;
}
