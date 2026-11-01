#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace san9 {
// Companion VA, injected-game VA and dialog-object offsets are distinct domains.
struct Relocation { bool objectField; std::uint32_t location; };
struct RelocationChange { Relocation slot; std::uint32_t before, after; };

class PatchImage final {
public:
    static constexpr std::uint32_t DataVA = 0x41F000;
    static constexpr std::uint32_t BaseSlot = 0x420050;
    static constexpr std::uint32_t AllocationSize = 0xB000;
    PatchImage();
    std::vector<RelocationChange> rebase(std::uint32_t newBase);
    std::uint32_t read32(std::uint32_t companionVA) const;
    std::uint32_t object32(std::size_t offset) const;
    std::uint32_t base() const { return read32(BaseSlot); }
    const std::vector<std::uint8_t>& data() const { return data_; }
private:
    std::vector<std::uint8_t> data_;
    std::array<std::uint8_t, 7844> object_{};
};
} // namespace san9
