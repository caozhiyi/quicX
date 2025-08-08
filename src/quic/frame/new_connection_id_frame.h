#ifndef QUIC_FRAME_NEW_CONNECTION_ID_FRAME
#define QUIC_FRAME_NEW_CONNECTION_ID_FRAME

#include <vector>
#include <cstdint>
#include "quic/frame/if_frame.h"
#include "quic/connection/type.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

static const uint16_t kStatelessResetTokenLength = 128;

class NewConnectionIDFrame:
    public IFrame {
public:
    NewConnectionIDFrame();
    ~NewConnectionIDFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetSequenceNumber(uint64_t sequence_number) { sequence_number_ = sequence_number; }
    uint64_t GetSequenceNumber() { return sequence_number_; }

    void SetRetirePriorTo(uint64_t retire_prior_to) { retire_prior_to_ = retire_prior_to; }
    uint64_t GetRetirePriorTo() { return retire_prior_to_; }

    void SetConnectionID(uint8_t* id, uint8_t len);
    void GetConnectionID(ConnectionID& id);

    void SetStatelessResetToken(uint8_t* token);
    const uint8_t* GetStatelessResetToken() { return stateless_reset_token_; }

private:
    uint64_t sequence_number_;  // the connection ID by the sender.
    uint64_t retire_prior_to_;  // which connection IDs should be retired.
    uint8_t  length_;           // the length of the connection ID.
    uint8_t  connection_id_[kMaxCidLength];    // a connection ID of the specified length.

    uint8_t stateless_reset_token_[kStatelessResetTokenLength];  // a 128-bit value that will be used for a stateless reset when the associated connection ID is used.
};

}
}

#endif