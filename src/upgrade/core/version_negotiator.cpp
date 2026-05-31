#include <algorithm>
#include "upgrade/core/protocol_detector.h"
#include "upgrade/core/version_negotiator.h"

namespace quicx {
namespace upgrade {

NegotiationResult VersionNegotiator::Negotiate(
    ConnectionContext& context, const UpgradeSettings& settings) {
    
    // Step 1: Protocol detection (skip if already known)
    if (context.detected_protocol == Protocol::UNKNOWN) {
        if (!DetectProtocol(context)) {
            return {false, Protocol::UNKNOWN, "", {}, "Protocol detection failed"};
        }
    }
    
    // Step 2: Select optimal protocol
    Protocol target = SelectBestProtocol(context, settings);
    context.target_protocol = target;
    
    // Step 3: Generate upgrade strategy
    return GenerateUpgradeStrategy(context, settings);
}

bool VersionNegotiator::DetectProtocol(ConnectionContext& context) {
    if (context.initial_data.empty()) {
        return false;
    }
    
    context.detected_protocol = ProtocolDetector::Detect(context.initial_data);
    return context.detected_protocol != Protocol::UNKNOWN;
}

Protocol VersionNegotiator::SelectBestProtocol(const ConnectionContext& context, const UpgradeSettings& settings) {
    // Always prefer HTTP/3 if client already using it
    if (context.detected_protocol == Protocol::HTTP3) {
        return Protocol::HTTP3;
    }

    // Prefer HTTP/3 if advertised via ALPN
    if (SupportsProtocol(context.alpn_protocols, "h3")) {
        return Protocol::HTTP3;
    }

    // Prefer upgrading HTTP/1.1 or HTTP/2 to HTTP/3 if enabled
    if (settings.enable_http3 &&
        (context.detected_protocol == Protocol::HTTP1_1 || context.detected_protocol == Protocol::HTTP2)) {
        return Protocol::HTTP3;
    }

    // Otherwise, keep detected protocol
    return context.detected_protocol;
}

NegotiationResult VersionNegotiator::GenerateUpgradeStrategy(
    ConnectionContext& context, const UpgradeSettings& settings) {
    
    NegotiationResult result;
    result.target_protocol = context.target_protocol;
    
    // If target protocol is the same as detected protocol, no upgrade needed
    if (context.target_protocol == context.detected_protocol) {
        result.success = true;
        return result;
    }
    
    // Only support upgrade to HTTP/3
    if (context.target_protocol == Protocol::HTTP3) {
        result.success = true;
        result.upgrade_token = "h3";
        
        // Generate upgrade data based on detected protocol
        switch (context.detected_protocol) {
            case Protocol::HTTP1_1:
                result.upgrade_data = GenerateHTTP1UpgradeData(settings);
                break;
            case Protocol::HTTP2:
                result.upgrade_data = GenerateHTTP2UpgradeData(settings);
                break;
            case Protocol::HTTP3:
                // Direct HTTP/3: no upgrade payload required
                result.upgrade_data.clear();
                break;
            default:
                result.success = false;
                result.error_message = "Unsupported upgrade path to HTTP/3";
                break;
        }
    } else {
        result.success = false;
        result.error_message = "Only HTTP/3 upgrades are supported";
    }
    
    return result;
}

bool VersionNegotiator::SupportsProtocol(const std::vector<std::string>& client_protocols, const std::string& protocol) {
    return std::find(client_protocols.begin(), client_protocols.end(), protocol) != client_protocols.end();
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP1UpgradeData(const UpgradeSettings& settings) {
    // For browsers over plain HTTP, many do not switch on 101 with Upgrade: h3.
    // Prefer responding 200 OK with Alt-Svc to advertise h3 endpoint.
    // Example minimal response with Alt-Svc: h3=":<port>"; ma=86400

    std::string body = "h3 available on :" + std::to_string(settings.h3_port) + "\n";
    std::string alt_svc = "h3=\":" + std::to_string(settings.h3_port) + "\"; ma=86400";

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Alt-Svc: " + alt_svc + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    return std::vector<uint8_t>(response.begin(), response.end());
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP2UpgradeData(const UpgradeSettings& settings) {
    // Build a minimal but spec-compliant HTTP/2 response sequence whose
    // sole purpose is to deliver `Alt-Svc: h3=...` to "h2-only" clients
    // (e.g. nghttp / h2load / some gRPC libraries that don't offer
    // `http/1.1` in ALPN). For browsers and curl we never reach this
    // path because the ALPN selector prefers http/1.1.
    //
    // What we emit (in order, on a single TLS write):
    //   1) Server SETTINGS frame                       (type=0x04, empty)
    //   2) SETTINGS ACK for the client SETTINGS        (type=0x04, flags=0x01)
    //      We send the ACK preemptively. RFC 7540 §6.5.3 only requires we
    //      ACK "as soon as possible"; doing it before we've seen the client
    //      SETTINGS is technically out of order but is widely tolerated and
    //      lets us answer in one shot without parsing client frames.
    //   3) HEADERS on stream 1 (END_HEADERS)
    //        :status: 200
    //        content-type: text/plain
    //        content-length: <body.len>
    //        alt-svc: h3=":<h3_port>"; ma=86400
    //   4) DATA on stream 1 (END_STREAM) with the body
    //   5) GOAWAY (last_stream_id=1, error=NO_ERROR)
    //
    // HPACK encoding strategy: "literal header field without indexing"
    // (0x00 prefix), with the name either taken from the static table by
    // index (for `:status` / `content-type` / `content-length`) or sent
    // as a literal string (for `alt-svc`, which is not in the static
    // table). Strings use Huffman=off (high bit clear) and a 7-bit length
    // prefix -- all our values are well under 127 bytes so a single byte
    // length is enough. This avoids pulling in a real HPACK encoder while
    // staying spec-compliant.

    auto append_frame_header = [](std::vector<uint8_t>& buf,
                                  uint32_t payload_len,
                                  uint8_t type,
                                  uint8_t flags,
                                  uint32_t stream_id) {
        buf.push_back(static_cast<uint8_t>((payload_len >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(payload_len & 0xFF));
        buf.push_back(type);
        buf.push_back(flags);
        buf.push_back(static_cast<uint8_t>((stream_id >> 24) & 0x7F)); // R bit clear
        buf.push_back(static_cast<uint8_t>((stream_id >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((stream_id >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(stream_id & 0xFF));
    };

    // HPACK: literal header field without indexing, name from static table
    //   first byte: 0000 NNNN where NNNN is the static index (must be < 15);
    //   followed by value-len (7-bit, H=0) and value bytes.
    auto append_lit_indexed_name = [](std::vector<uint8_t>& buf,
                                      uint8_t static_index,
                                      const std::string& value) {
        // 0x0F = 15, max representable in the 4-bit prefix without overflow
        // marker; all indexes we use are <= 8 so a single byte is fine.
        buf.push_back(static_cast<uint8_t>(0x00 | (static_index & 0x0F)));
        buf.push_back(static_cast<uint8_t>(value.size() & 0x7F));
        buf.insert(buf.end(), value.begin(), value.end());
    };

    // HPACK: literal header field without indexing, name as literal string
    //   first byte: 0000 0000
    //   then name-len(7bit) name-bytes, value-len(7bit) value-bytes
    auto append_lit_literal_name = [](std::vector<uint8_t>& buf,
                                      const std::string& name,
                                      const std::string& value) {
        buf.push_back(0x00);
        buf.push_back(static_cast<uint8_t>(name.size() & 0x7F));
        buf.insert(buf.end(), name.begin(), name.end());
        buf.push_back(static_cast<uint8_t>(value.size() & 0x7F));
        buf.insert(buf.end(), value.begin(), value.end());
    };

    std::vector<uint8_t> out;
    out.reserve(256);

    // 1) Server SETTINGS (empty)
    append_frame_header(out, /*len*/0, /*type SETTINGS*/0x04, /*flags*/0x00, /*stream*/0);

    // 2) SETTINGS ACK
    append_frame_header(out, /*len*/0, /*type SETTINGS*/0x04, /*flags ACK*/0x01, /*stream*/0);

    // Build response body and headers
    const std::string body = "h3 available on :" + std::to_string(settings.h3_port) + "\n";
    const std::string alt_svc_value =
        "h3=\":" + std::to_string(settings.h3_port) + "\"; ma=86400";
    const std::string content_length_value = std::to_string(body.size());

    // 3) HEADERS frame (END_HEADERS=0x04 | END_STREAM=0x00 -- body follows in DATA)
    //    Static table indexes (RFC 7541 Appendix A):
    //      8  -> :status: 200
    //      31 -> content-type
    //      28 -> content-length
    //    But our `append_lit_indexed_name` only encodes 4-bit indexes
    //    (<=14) in a single byte. For :status:200 we use index 8 directly
    //    (this becomes "indexed header field representation" 0x88 -- even
    //    simpler than literal).  For content-type and content-length we
    //    fall back to literal-name form to keep the encoder trivial.

    std::vector<uint8_t> hdr_block;
    hdr_block.reserve(96);

    // :status: 200 -- indexed (static table index 8)
    hdr_block.push_back(0x88); // 1xxxxxxx with index=8

    // content-type: text/plain -- literal name
    append_lit_literal_name(hdr_block, "content-type", "text/plain");

    // content-length: <n> -- literal name
    append_lit_literal_name(hdr_block, "content-length", content_length_value);

    // alt-svc: h3=":<port>"; ma=86400 -- literal name (not in static table)
    append_lit_literal_name(hdr_block, "alt-svc", alt_svc_value);

    append_frame_header(out, static_cast<uint32_t>(hdr_block.size()),
                        /*type HEADERS*/0x01,
                        /*flags END_HEADERS*/0x04,
                        /*stream*/1);
    out.insert(out.end(), hdr_block.begin(), hdr_block.end());

    // 4) DATA frame on stream 1 with END_STREAM
    append_frame_header(out, static_cast<uint32_t>(body.size()),
                        /*type DATA*/0x00,
                        /*flags END_STREAM*/0x01,
                        /*stream*/1);
    out.insert(out.end(), body.begin(), body.end());

    // 5) GOAWAY: last_stream_id=1, error_code=0 (NO_ERROR), no debug data
    //    Payload layout: R(1) | LastStreamID(31) | ErrorCode(32)
    {
        std::vector<uint8_t> goaway_payload;
        goaway_payload.reserve(8);
        // Last-Stream-ID = 1
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x01);
        // Error Code = 0
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x00);
        goaway_payload.push_back(0x00);

        append_frame_header(out, static_cast<uint32_t>(goaway_payload.size()),
                            /*type GOAWAY*/0x07,
                            /*flags*/0x00,
                            /*stream*/0);
        out.insert(out.end(), goaway_payload.begin(), goaway_payload.end());
    }

    return out;
}

} // namespace upgrade
} // namespace quicx 