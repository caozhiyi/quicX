#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

#include "common/util/singleton.h"

namespace quicx {

class Buffer;
class AlloterWrap;
class Frame;

class FrameDecode: public Singleton<FrameDecode> {
public:
    FrameDecode();
    ~FrameDecode();

    bool DecodeFrame(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, std::vector<std::shared_ptr<Frame>>& frames);
private:
    typedef std::function<std::shared_ptr<Frame>(uint16_t)> FrameCreater;
    static std::unordered_map<uint16_t, FrameCreater> __frame_creater_map;
};

bool DecodeFrame(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, std::vector<std::shared_ptr<Frame>>& frames);

}

#endif