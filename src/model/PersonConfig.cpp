#include "PersonConfig.h"
namespace san9 {
namespace {
bool validFriendship(const std::array<std::uint16_t,3>& row) {
    return row[0]<PersonConfig::Count && row[1]<PersonConfig::Count && row[2]<=60;
}
}
bool PersonConfig::valid() const {
    for(auto value:levels) if(value>0x3FFFF) return false;
    for(const auto& row:friendships) if(!validFriendship(row)) return false;
    return true;
}
PersonConfig::LevelBytes PersonConfig::encodeLevels() const {
    LevelBytes bytes{};
    for(size_t i=0;i<Count;++i) for(size_t k=0;k<4;++k) bytes[i*4+k]=static_cast<std::uint8_t>(levels.at(i)>>(k*8));
    return bytes;
}
PersonConfig::FriendshipBytes PersonConfig::encodeFriendships() const {
    FriendshipBytes bytes{};
    for(size_t i=0;i<FriendshipCount;++i) for(size_t k=0;k<3;++k) {
        bytes[i*6+k*2]=static_cast<std::uint8_t>(friendships.at(i)[k]);
        bytes[i*6+k*2+1]=static_cast<std::uint8_t>(friendships.at(i)[k]>>8);
    }
    return bytes;
}
void PersonConfig::decode(const LevelBytes& l,const FriendshipBytes& f) {
    for(size_t i=0;i<Count;++i) {
        levels.at(i)=0;
        for(size_t k=0;k<4;++k) levels.at(i)|=std::uint32_t(l[i*4+k])<<(k*8);
    }
    for(size_t i=0;i<FriendshipCount;++i) for(size_t k=0;k<3;++k)
        friendships.at(i)[k]=f[i*6+k*2]|(std::uint16_t(f[i*6+k*2+1])<<8);
}
PackedReadResult PersonConfig::read(std::istream& input) {
    PersonConfig candidate;
    for(size_t i=0;i<Count+FriendshipCount;++i) {
        std::uint32_t value=0;const auto error=packed::number(input,value);
        if(error!=PackedReadError::none) return {error,i+1};
        if(i<Count) {
            if(value>0x3FFFF) return {PackedReadError::outOfRange,i+1};
            candidate.levels.at(i)=value;
        } else {
            auto& row=candidate.friendships.at(i-Count);
            row={static_cast<std::uint16_t>(value&0x3FF),static_cast<std::uint16_t>((value>>10)&0x3FF),static_cast<std::uint16_t>((value>>20)&0x3F)};
            if(value>0x3FFFFFF || !validFriendship(row)) return {PackedReadError::outOfRange,i+1};
        }
    }
    const auto tail = packed::finish(input, Count+FriendshipCount);
    if (tail.error != PackedReadError::none) return tail;
    *this=candidate;return {};
}
bool PersonConfig::write(std::ostream& output) const {
    if(!valid()) return false;
    for(auto value:levels) output<<value<<'\n';
    for(const auto& row:friendships) output<<(std::uint32_t(row[0])|(std::uint32_t(row[1])<<10)|(std::uint32_t(row[2])<<20))<<'\n';
    output.flush();return bool(output);
}
}
