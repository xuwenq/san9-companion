#pragma once
#include "../model/PackedConfigIO.h"
#include <array>
#include <cstdint>
#include <istream>
#include <ostream>

// Per-officer personalization (SanIXPKCheat 第一批): one value set per officer.
// Values are stored as compact byte arrays to keep the companion state small.
struct OfficerPersonalization2026 {
    static constexpr size_t Count = 800;
    // 威力系数 (percent): 100 = 1.0x, 0..250. 0 = 不修改.
    std::array<std::uint8_t, Count> powerPercent{};
    // 发动率 (percent): 0 = 不修改(原版), 1..100 = 强制该概率.
    std::array<std::uint8_t, Count> activationPercent{};
    // 发动间隔 (旬): 0 = 不修改, 1..30 = 强制最小间隔.
    std::array<std::uint8_t, Count> interval{};

    san9::PackedReadResult read(std::istream &input);
    bool write(std::ostream &output) const;
};
