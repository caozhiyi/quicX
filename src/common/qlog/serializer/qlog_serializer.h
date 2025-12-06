// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_SERIALIZER_QLOG_SERIALIZER
#define COMMON_QLOG_SERIALIZER_QLOG_SERIALIZER

#include <string>

#include "common/qlog/event/qlog_event.h"
#include "common/qlog/qlog_config.h"

namespace quicx {
namespace common {

/**
 * @brief qlog serializer interface
 */
class IQlogSerializer {
public:
    virtual ~IQlogSerializer() = default;

    /**
     * @brief serialize trace header
     */
    virtual std::string SerializeTraceHeader(const std::string& connection_id, VantagePoint vantage_point,
        const CommonFields& common_fields, const QlogConfiguration& config) = 0;

    /**
     * @brief serialize single event
     */
    virtual std::string SerializeEvent(const QlogEvent& event) = 0;

    /**
     * @brief get fuke format
     */
    virtual QlogFileFormat GetFormat() const = 0;
};

}  // namespace common
}  // namespace quicx

#endif
