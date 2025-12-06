// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_SERIALIZER_JSON_SEQ_SERIALIZER
#define COMMON_QLOG_SERIALIZER_JSON_SEQ_SERIALIZER

#include "common/qlog/serializer/qlog_serializer.h"

namespace quicx {
namespace common {

/**
 * @brief JSON Text Sequences serializer (RFC 7464)
 *
 * Format: One JSON object per line, ending with \n
 * (Note: Standard RFC 7464 requires \x1E prefix, but qlog spec simplifies to just \n)
 */
class JsonSeqSerializer: public IQlogSerializer {
public:
    JsonSeqSerializer() = default;
    ~JsonSeqSerializer() override = default;

    std::string SerializeTraceHeader(const std::string& connection_id, VantagePoint vantage_point,
        const CommonFields& common_fields, const QlogConfiguration& config) override;

    std::string SerializeEvent(const QlogEvent& event) override;

    QlogFileFormat GetFormat() const override { return QlogFileFormat::kSequential; }
};

}  // namespace common
}  // namespace quicx

#endif
