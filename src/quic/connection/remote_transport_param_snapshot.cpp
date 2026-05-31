#include "quic/connection/remote_transport_param_snapshot.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

RemoteTransportParamSnapshot RemoteTransportParamSnapshot::From(const TransportParam& tp) {
    RemoteTransportParamSnapshot snapshot;
    snapshot.has_value = true;
    snapshot.initial_max_data = tp.GetInitialMaxData();
    snapshot.initial_max_streams_bidi = tp.GetInitialMaxStreamsBidi();
    snapshot.initial_max_streams_uni = tp.GetInitialMaxStreamsUni();
    snapshot.initial_max_stream_data_bidi_local = tp.GetInitialMaxStreamDataBidiLocal();
    snapshot.initial_max_stream_data_bidi_remote = tp.GetInitialMaxStreamDataBidiRemote();
    snapshot.initial_max_stream_data_uni = tp.GetInitialMaxStreamDataUni();
    snapshot.active_connection_id_limit = tp.GetActiveConnectionIdLimit();
    return snapshot;
}

}  // namespace quic
}  // namespace quicx