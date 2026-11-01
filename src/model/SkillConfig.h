#pragma once
#include "PackedConfigIO.h"
#include <array>
#include <ostream>

namespace san9 {
struct SkillValues {
    std::uint8_t power=10, interval=9;
    std::uint16_t proficiency=100;
    bool valid() const { return power<=100 && interval<=30 && proficiency<=2500; }
};
class SkillConfig {
public:
    static constexpr size_t Count=41, GameRecordSize=0x8C;
    using Bytes=std::array<std::uint8_t,Count*4>;
    std::array<SkillValues,Count> entries{};
    bool valid() const;
    Bytes encode() const;
    void decode(const Bytes& bytes);
    PackedReadResult read(std::istream& input);
    bool write(std::ostream& output) const;
    bool importGame(size_t index,const std::uint8_t* bytes,size_t size);
};
}
