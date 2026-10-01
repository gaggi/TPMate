#pragma once

#include <windows.h>

class ReloadShortcut final
{
public:
    void Reset() { rDown_ = false; }
    bool OnKey(DWORD key, WPARAM message, bool injected, bool control, bool alt, bool shift)
    {
        if (key != 'R' || injected) return false;
        if (message == WM_KEYUP || message == WM_SYSKEYUP) { rDown_ = false; return false; }
        if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
        const bool firstPress = !rDown_;
        rDown_ = true;
        return firstPress && control && !alt && !shift;
    }
    static bool IsSimulator(const wchar_t* executableName)
    {
        return _wcsicmp(executableName, L"iRacingSim64DX11.exe") == 0 ||
            _wcsicmp(executableName, L"iRacingSim64.exe") == 0;
    }
private:
    bool rDown_ = false;
};
