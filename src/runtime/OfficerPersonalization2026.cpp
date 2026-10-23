#include "OfficerPersonalization2026.h"

san9::PackedReadResult OfficerPersonalization2026::read(std::istream &input) {
    OfficerPersonalization2026 candidate;
    for (size_t i = 0; i < Count; ++i) {
        std::uint32_t power = 0, activation = 0, intervalValue = 0;
        san9::PackedReadError error = san9::packed::number(input, power);
        if (error != san9::PackedReadError::none) return {error, i * 3 + 1};
        error = san9::packed::number(input, activation);
        if (error != san9::PackedReadError::none) return {error, i * 3 + 2};
        error = san9::packed::number(input, intervalValue);
        if (error != san9::PackedReadError::none) return {error, i * 3 + 3};
        if (power > 250 || activation > 100 || intervalValue > 30)
            return {san9::PackedReadError::outOfRange, i * 3 + 1};
        candidate.powerPercent[i] = static_cast<std::uint8_t>(power);
        candidate.activationPercent[i] = static_cast<std::uint8_t>(activation);
        candidate.interval[i] = static_cast<std::uint8_t>(intervalValue);
    }
    const auto tail = san9::packed::finish(input, Count * 3);
    if (tail.error != san9::PackedReadError::none) return tail;
    *this = candidate;
    return {};
}

bool OfficerPersonalization2026::write(std::ostream &output) const {
    for (size_t i = 0; i < Count; ++i) {
        output << static_cast<unsigned>(powerPercent[i]) << '\n'
               << static_cast<unsigned>(activationPercent[i]) << '\n'
               << static_cast<unsigned>(interval[i]) << '\n';
        if (!output) return false;
    }
    return true;
}
