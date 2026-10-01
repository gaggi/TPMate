#pragma once

#include <windows.h>

class StartupRegistration final
{
public:
    static bool IsEnabled();
    static bool Apply(HWND owner, bool enabled);
    // Process the one-shot elevated helper before the single-instance check.
    static int HandleCommandLine();
};
