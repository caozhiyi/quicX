#include "session/session.h"

#include <quicx/http3/if_client.h>

#include "obs/tracer.h"
#include "session/transport_map.h"

ClientSession::~ClientSession() { Close(); }

bool ClientSession::Init(const TransportOptions& tp, double connect_timeout_s, bool verbose) {
    Tracer trace(verbose, /*show_error=*/true);
    client_ = quicx::IClient::Create(BuildHttp3Settings(tp));
    if (!client_) {
        trace.Error("failed to create HTTP/3 client");
        return false;
    }
    if (!client_->Init(BuildClientConfig(tp, connect_timeout_s, verbose))) {
        trace.Error("failed to initialize HTTP/3 client");
        client_.reset();
        return false;
    }
    inited_ = true;
    trace.Info("HTTP/3 client initialized (qlog=%s keylog=%s 0rtt=%s version=%s)",
        tp.qlog_dir.empty() ? "off" : tp.qlog_dir.c_str(),
        tp.keylog_file.empty() ? "off" : tp.keylog_file.c_str(),
        tp.zero_rtt ? "on" : "off", tp.quic_version.c_str());
    return true;
}

void ClientSession::Close() {
    if (inited_ && client_) {
        client_->Close();
        inited_ = false;
    }
}
