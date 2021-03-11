#ifndef QUIC_FRAME_NEW_CONNECTION_ID_FRAME
#define QUIC_FRAME_NEW_CONNECTION_ID_FRAME

#include <vector>
#include <cstdint>
#include "frame_interface.h"

namespace quicx {

static const uint16_t __stateless_reset_token_length = 128;

class NewConnectionIDFrame: public Frame {
public:
    NewConnectionIDFrame();
    ~NewConnectionIDFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetSequenceNumber(uint64_t sequence_number) { _sequence_number = sequence_number; }
    uint64_t GetSequenceNumber() { return _sequence_number; }

    void SetRetirePriorTo(uint64_t retire_prior_to) { _retire_prior_to = retire_prior_to; }
    uint64_t GetRetirePriorTo() { return _retire_prior_to; }

    void AddConnectionID(uint64_t id) { _connection_id.push_back(id); }
    const std::vector<uint64_t>& GetConnectionID() { return _connection_id; }

    void SetStatelessResetToken(char* token);
    const char* GetStatelessResetToken() { return _stateless_reset_token; }

private:
    uint64_t _sequence_number;  // the connection ID by the sender.
    uint64_t _retire_prior_to;  // which connection IDs should be retired.

    std::vector<uint64_t> _connection_id; // a connection ID of the specified length.

    char _stateless_reset_token[__stateless_reset_token_length];  // a 128-bit value that will be used for a stateless reset when the associated connection ID is used.
    /*
    uint8_t  _length;           // the length of the connection ID.
    */
};

}

#endif