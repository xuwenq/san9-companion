#pragma once
#include "PackedConfigIO.h"
#include <array>
#include <ostream>
namespace san9 {
class PersonConfig {
public:
    static constexpr size_t Count=800, FriendshipCount=116;
    // 兵法系数量。前 5 系(步兵/骑兵/弓骑/弩兵/水军)的最高可习得级别为 2；
    // 后 4 系(攻城/智略/谋略/策略)的第 4 档是独立兵法阶位(象兵/石兵/破陷/教论等)，可取 3。
    static constexpr size_t Systems=9, RefinedSystems=5, MaxLevel=3, MaxLevelBeforeRefined=2;
    using LevelBytes=std::array<std::uint8_t,Count*4>;
    using FriendshipBytes=std::array<std::uint8_t,FriendshipCount*6>;
    std::array<std::uint32_t,Count> levels{};
    std::array<std::array<std::uint16_t,3>,FriendshipCount> friendships{};
    PersonConfig() { levels.fill(0x3FEAA); }
    // 单个兵法系的可选级别上限。
    static constexpr std::uint32_t maxLevel(size_t system) {
        return system<RefinedSystems?MaxLevelBeforeRefined:MaxLevel;
    }
    bool valid() const;
    LevelBytes encodeLevels() const;
    FriendshipBytes encodeFriendships() const;
    void decode(const LevelBytes& levels,const FriendshipBytes& friendships);
    PackedReadResult read(std::istream& input);
    bool write(std::ostream& output) const;
};
}
