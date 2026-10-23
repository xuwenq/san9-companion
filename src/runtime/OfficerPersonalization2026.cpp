#include "OfficerPersonalization2026.h"

san9::PackedReadResult OfficerPersonalization2026::read(std::istream &input) {
    OfficerPersonalization2026 candidate;
    for (size_t i = 0; i < Count; ++i) {
        std::uint32_t power = 0, activation = 0, intervalValue = 0, critical = 0;
        san9::PackedReadError error = san9::packed::number(input, power);
        if (error != san9::PackedReadError::none) return {error, i * 4 + 1};
        error = san9::packed::number(input, activation);
        if (error != san9::PackedReadError::none) return {error, i * 4 + 2};
        error = san9::packed::number(input, intervalValue);
        if (error != san9::PackedReadError::none) return {error, i * 4 + 3};
        error = san9::packed::number(input, critical);
        if (error != san9::PackedReadError::none) return {error, i * 4 + 4};
        if (power > 250 || activation > 100 || intervalValue > 30 || critical > 1)
            return {san9::PackedReadError::outOfRange, i * 4 + 1};
        candidate.powerPercent[i] = static_cast<std::uint8_t>(power);
        candidate.activationPercent[i] = static_cast<std::uint8_t>(activation);
        candidate.interval[i] = static_cast<std::uint8_t>(intervalValue);
        candidate.criticalHit[i] = static_cast<std::uint8_t>(critical);
    }
    const auto tail = san9::packed::finish(input, Count * 4);
    if (tail.error != san9::PackedReadError::none) return tail;
    *this = candidate;
    return {};
}

bool OfficerPersonalization2026::write(std::ostream &output) const {
    for (size_t i = 0; i < Count; ++i) {
        output << static_cast<unsigned>(powerPercent[i]) << '\n'
               << static_cast<unsigned>(activationPercent[i]) << '\n'
               << static_cast<unsigned>(interval[i]) << '\n'
               << static_cast<unsigned>(criticalHit[i]) << '\n';
        if (!output) return false;
    }
    return true;
}
