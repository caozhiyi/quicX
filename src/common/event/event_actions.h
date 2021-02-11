#ifndef QUIC_COMMON_EVENT_EVENT_ACTIONS
#define QUIC_COMMON_EVENT_EVENT_ACTIONS

namespace quicx {

class EventActions {
public:
    EventActions() {}
    virtual ~EventActions() {}

    virtual void Init() = 0;
    
};

}

#endif