#pragma once

namespace bm
{
// Loudness targets. Values are the commonly published figures for each destination; platforms change their
// specs from time to time, so use "Custom" if yours differs. Twitch and Kick publish no official target.
struct LoudnessPreset
{
    const char* name;
    float targetLufs;     // integrated / programme target
    float tolerance;      // +/- LU treated as "on target" (green)
    float ceilingDbtp;    // true-peak ceiling
};

inline constexpr LoudnessPreset kLoudnessPresets[] =
{
    { "YouTube",               -14.0f, 1.0f, -1.0f },
    { "Twitch / Kick (typical)", -14.0f, 1.0f, -1.0f },
    { "Spotify",               -14.0f, 1.0f, -1.0f },
    { "Apple Music / Podcast", -16.0f, 1.0f, -1.0f },
    { "EBU R128",              -23.0f, 0.5f, -1.0f },
    { "ATSC A/85",             -24.0f, 2.0f, -2.0f },
    { "ARIB TR-B32",           -24.0f, 1.0f, -1.0f },
    { "Custom",                -16.0f, 1.0f, -1.0f },
};

inline constexpr int kNumLoudnessPresets = (int) (sizeof (kLoudnessPresets) / sizeof (kLoudnessPresets[0]));
inline constexpr int kCustomPresetIndex = kNumLoudnessPresets - 1;
} // namespace bm
