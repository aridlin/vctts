#pragma once
#include "app_state.h"

// Legacy name used by UI: fills s.outDevices / s.outDevicesUtf8 and clamps indices.
// Internally this uses miniaudio enumeration (same backend as playback).
void RefreshOutputDevices(AppState& s);

