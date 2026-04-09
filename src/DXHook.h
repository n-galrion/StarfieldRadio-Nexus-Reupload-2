#pragma once
#include <d3d11.h>
#include <dxgi.h>

namespace DXHook {
    bool Install();
    bool IsInitialized();
    bool IsMenuOpen();
    void SetMenuOpen(bool open);
}
