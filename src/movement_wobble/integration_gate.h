#pragma once
namespace nova::movement_wobble {
// Ordinary play uses camera-view reduction with fresh local-owner checks.
// The former animation pre-mix adapter is no longer constructed by play.
inline constexpr bool kNativeAdapterConnected=true;
inline constexpr bool kControlAvailable=kNativeAdapterConnected;
}
