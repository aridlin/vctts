#pragma once
#include "app_state.h"

// Fills s.outDevices / s.outDevicesUtf8 and clamps s.devA/s.devB.
// Uses the SAME backend/order as playback (miniaudio).
void RefreshOutputDevices(AppState& s);
