#ifndef QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER
#define QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER

#include <vector>
#include <openssl/ssl.h>
#include "quic/crypto/cryptographer_interface.h"

namespace quicx {

class AeadBaseCryptographer:
    public CryptographerIntreface {
public:
    AeadBaseCryptographer();
    virtual ~AeadBaseCryptographer();

    virtual bool InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write);

    virtual bool InstallInitSecret(uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server);

    virtual bool DecryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> ciphertext,
                             std::shared_ptr<IBufferReadOnly> out_plaintext);

    virtual bool EncryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> plaintext,
                             std::shared_ptr<IBufferReadOnly> out_ciphertext);

    virtual bool DecryptHeader(std::shared_ptr<IBufferReadOnly> ciphertext, uint8_t pn_offset, bool is_short);

    virtual bool EncryptHeader(std::shared_ptr<IBufferReadOnly> plaintext, uint8_t pn_offset, bool is_short);
private:
    bool InitKey(bool is_write);

protected:
    struct Secret {
        std::vector<uint8_t> _key;
        std::vector<uint8_t> _iv;
        std::vector<uint8_t> _hp;
    };

    Secret _read_secret;
    Secret _write_secret;

    size_t _aead_key_length;
    size_t _aead_iv_length;
    size_t _aead_tag_length;

    size_t _cipher_key_length;
    size_t _cipher_iv_length;

    const EVP_MD *_digest;
    const EVP_AEAD *_aead;
    const EVP_CIPHER *_cipher;

    EVP_AEAD_CTX *_write_aead_ctx;
    EVP_AEAD_CTX *_read_aead_ctx;
};

}

#endif