#include <cstring>

#include <openssl/hkdf.h>

#include "quic/crypto/hkdf.h"

namespace quicx {
namespace quic {

bool Hkdf::HkdfExtract(uint8_t* dest, size_t destlen, const uint8_t* secret, size_t secretlen, const uint8_t* salt,
    size_t saltlen, const EVP_MD* md) {
    if (dest == nullptr || md == nullptr) {
        return false;
    }
    return 1 == HKDF_extract(dest, &destlen, md, secret, secretlen, salt, saltlen);
}

bool Hkdf::HkdfExpand(uint8_t* dest, size_t destlen, const uint8_t* secret, size_t secretlen, const uint8_t* label,
    size_t labellen, const EVP_MD* md) {
    // RFC 8446 Section 7.1: HKDF-Expand-Label
    // struct {
    //     uint16 length = Length;
    //     opaque label<7..255> = "tls13 " + Label;
    //     opaque context<0..255> = Context;
    // } HkdfLabel;

    if (dest == nullptr || (label == nullptr && labellen > 0) || md == nullptr) {
        return false;
    }

    uint8_t hkdf_label[256];
    size_t hkdf_label_len = 0;

    // Layout is 2 (length) + 1 (label length) + labellen + 1 (context length).
    // `hkdf_label` is a fixed 256-byte stack buffer, so a label that does not
    // fit must be rejected rather than written past the end. Today's QUIC/TLS
    // labels are <= 16 bytes, but the signature accepts any length and the
    // memcpy below is unbounded — check it so adding a longer label later
    // cannot silently turn into a stack buffer overflow.
    if (labellen > sizeof(hkdf_label) - 3 - 1) {
        return false;
    }

    // Length (2 bytes, big-endian)
    hkdf_label[hkdf_label_len++] = (destlen >> 8) & 0xFF;
    hkdf_label[hkdf_label_len++] = destlen & 0xFF;

    // Label length (1 byte)
    hkdf_label[hkdf_label_len++] = static_cast<uint8_t>(labellen);

    // Label (already includes "tls13 " prefix)
    memcpy(hkdf_label + hkdf_label_len, label, labellen);
    hkdf_label_len += labellen;

    // Context length (1 byte) - empty for QUIC Initial secrets
    hkdf_label[hkdf_label_len++] = 0;

    return 1 == HKDF_expand(dest, destlen, md, secret, secretlen, hkdf_label, hkdf_label_len);
}

}  // namespace quic
}  // namespace quicx
