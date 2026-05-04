#!/bin/bash
# No-op setup.sh for direct mode (no ns-3 network simulator)
# This replaces the real /setup.sh in third-party containers to prevent
# route table corruption when running without the simulator topology.
echo "Direct mode: skipping network simulator setup"
mkdir -p /logs/qlog
