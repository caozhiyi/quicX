cmake_minimum_required(VERSION 3.10)

project(quicx CXX)

collect_sources(${PROJECT_SOURCE_DIR}/common common_files)
collect_sources(${PROJECT_SOURCE_DIR}/quic quic_files)
collect_sources(${PROJECT_SOURCE_DIR}/http3 http3_files)

SET(common_source ${common_files} PARENT_SCOPE)
SET(quic_source ${quic_files} PARENT_SCOPE)
SET(http3_source ${http3_files} PARENT_SCOPE)