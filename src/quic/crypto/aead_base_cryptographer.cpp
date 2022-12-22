#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

AeadBaseCryptographer::AeadBaseCryptographer() {

}
AeadBaseCryptographer::~AeadBaseCryptographer() {}

bool AeadBaseCryptographer::DecryptPacket(std::shared_ptr<IBufferReadOnly> ciphertext,
    std::shared_ptr<IBufferReadOnly> out_plaintext) {
    return true;
}

bool AeadBaseCryptographer::EncryptPacket(std::shared_ptr<IBufferReadOnly> plaintext,
    std::shared_ptr<IBufferReadOnly> out_ciphertext) {
    return true;
}


}
