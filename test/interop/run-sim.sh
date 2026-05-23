#!/bin/bash
# quicX wrapper around martenseemann/quic-network-simulator's run.sh
#
# Differences from the upstream run.sh:
#   1. No "set -e" at the top: a transient DNS error in wait-for-it-quic
#      (caused by docker compose starting sim before the server hostname
#      is registered in its internal DNS) used to abort sim with exit 1.
#      We tolerate that race because the QUIC handshake itself retransmits
#      a few early lost packets.
#   2. Verbatim copy of the rest of upstream run.sh's logic, so the ns-3
#      simulator behaves identically once it is running.

ifconfig eth0 promisc
ifconfig eth1 promisc

# Force traffic through ns-3 by dropping any direct L3 forward between
# eth0 and eth1.  ns-3 captures via raw L2 sockets so it is unaffected.
iptables  -A FORWARD -i eth0 -o eth1 -j DROP
iptables  -A FORWARD -i eth1 -o eth0 -j DROP
ip6tables -A FORWARD -i eth0 -o eth1 -j DROP
ip6tables -A FORWARD -i eth1 -o eth0 -j DROP

if [[ -n "$WAITFORSERVER" ]]; then
  # Tolerate transient DNS / connectivity errors during compose startup
  # race.  See header for justification.
  wait-for-it-quic -t 10s "$WAITFORSERVER" || \
    echo "wait-for-it-quic failed for $WAITFORSERVER (continuing anyway)"
fi

echo "Using scenario: $SCENARIO"

dumpcap -i eth0 -s 0 -w "/logs/trace_node_left.pcap"  &
dumpcap -i eth1 -s 0 -w "/logs/trace_node_right.pcap" &
eval ./scratch/"$SCENARIO &"

PID=$(jobs -p | tr '\n' ' ')
trap "kill -SIGINT  $PID" INT
trap "kill -SIGTERM $PID" TERM
trap "kill -SIGKILL $PID" KILL
wait
