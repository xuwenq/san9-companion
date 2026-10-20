#pragma once
#include <string>

// Companion-side configuration for the per-officer ability growth multiplier.
struct GrowthBoost2026 {
    std::wstring officerList; // Comma-separated officer IDs, e.g. L"5,12,33".
    unsigned multiplier = 1;  // 1..10; growth probability is multiplied by this.
};
