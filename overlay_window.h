#pragma once

#include "app_state.h"

#include <windows.h>

namespace overlay_window
{
    bool create(AppState& state, HINSTANCE hInstance);
    void destroy(HINSTANCE hInstance);
    void tick(AppState& state);
    void hide();
}
