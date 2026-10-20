#pragma once

// Companion-side configuration for the death gradient that replaces the
// original "any ability >= 80 never dies" binary protection.
struct DeathGradient2026 {
    unsigned lowRate = 40;   // Death chance (percent) when MAX(abilities) < 60.
    unsigned threshold = 100; // MAX(abilities) at/above which death chance is 0.
};
