#include "ReloadShortcut.h"
#include <iostream>

int main()
{
    ReloadShortcut shortcut;
    const auto key = [&](WPARAM message, bool ctrl = true, bool alt = false, bool shift = false, bool injected = false)
    { return shortcut.OnKey('R', message, injected, ctrl, alt, shift); };
    if (!key(WM_KEYDOWN)) return 1;
    if (key(WM_KEYDOWN)) return 2; // Auto-repeat must not enqueue downloads.
    if (key(WM_KEYUP) || !key(WM_KEYDOWN)) return 3;
    shortcut.Reset();
    if (key(WM_KEYDOWN, false)) return 4;
    shortcut.Reset();
    if (key(WM_SYSKEYDOWN, true, true)) return 5;
    shortcut.Reset();
    if (key(WM_KEYDOWN, true, false, true)) return 6;
    shortcut.Reset();
    if (key(WM_KEYDOWN, true, false, false, true)) return 7;
    if (!key(WM_KEYDOWN)) return 8; // Injected input must not change physical-key state.
    shortcut.Reset();
    if (shortcut.OnKey('A', WM_KEYDOWN, false, true, false, false)) return 9;
    if (!ReloadShortcut::IsSimulator(L"IRACINGSim64DX11.exe") ||
        !ReloadShortcut::IsSimulator(L"iRacingSim64.exe") ||
        ReloadShortcut::IsSimulator(L"iRacingUI.exe") || ReloadShortcut::IsSimulator(L"notepad.exe")) return 10;
    std::cout << "Ctrl+R detection checks passed.\n";
    return 0;
}
