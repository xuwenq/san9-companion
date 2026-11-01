#include "OfficerPersonalization2026.h"

namespace {
constexpr size_t FieldsPerOfficer = OfficerPersonalization2026::Systems + OfficerPersonalization2026::Skills + 3;
}

san9::PackedReadResult OfficerPersonalization2026::read(std::istream &input) {
    OfficerPersonalization2026 candidate;
    for (size_t i = 0; i < Count; ++i) {
        for (size_t k = 0; k < Systems; ++k) {
            std::uint32_t value = 0;
            if (const auto error = san9::packed::number(input, value); error != san9::PackedReadError::none)
                return {error, i * FieldsPerOfficer + k + 1};
            if (value > 65535)
                return {san9::PackedReadError::outOfRange, i * FieldsPerOfficer + k + 1};
            candidate.proficiencyCap[i][k] = static_cast<std::uint16_t>(value);
        }
        for (size_t k = 0; k < Skills; ++k) {
            std::uint32_t value = 0;
            if (const auto error = san9::packed::number(input, value); error != san9::PackedReadError::none)
                return {error, i * FieldsPerOfficer + Systems + k + 1};
            if (value > MaxInterval)
                return {san9::PackedReadError::outOfRange, i * FieldsPerOfficer + Systems + k + 1};
            candidate.interval[i][k] = static_cast<std::uint8_t>(value);
        }
        std::uint32_t boost = 0, power = 0, critical = 0;
        if (const auto error = san9::packed::number(input, boost); error != san9::PackedReadError::none)
            return {error, i * FieldsPerOfficer + Systems + Skills + 1};
        if (const auto error = san9::packed::number(input, power); error != san9::PackedReadError::none)
            return {error, i * FieldsPerOfficer + Systems + Skills + 2};
        if (const auto error = san9::packed::number(input, critical); error != san9::PackedReadError::none)
            return {error, i * FieldsPerOfficer + Systems + Skills + 3};
        if (boost > 1 || power > 250 || critical > 1)
            return {san9::PackedReadError::outOfRange, i * FieldsPerOfficer + Systems + Skills + 1};
        candidate.activationBoost[i] = static_cast<std::uint8_t>(boost);
        candidate.powerPercent[i] = static_cast<std::uint8_t>(power);
        candidate.criticalHit[i] = static_cast<std::uint8_t>(critical);
    }
    if (const auto tail = san9::packed::finish(input, Count * FieldsPerOfficer); tail.error != san9::PackedReadError::none)
        return tail;
    *this = candidate;
    return {};
}

bool OfficerPersonalization2026::write(std::ostream &output) const {
    for (size_t i = 0; i < Count; ++i) {
        for (size_t k = 0; k < Systems; ++k)
            output << proficiencyCap[i][k] << '\n';
        for (size_t k = 0; k < Skills; ++k)
            output << static_cast<unsigned>(interval[i][k]) << '\n';
        output << static_cast<unsigned>(activationBoost[i]) << '\n'
               << static_cast<unsigned>(powerPercent[i]) << '\n'
               << static_cast<unsigned>(criticalHit[i]) << '\n';
        if (!output)
            return false;
    }
    return true;
}
