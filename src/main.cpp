#include "TrayApp.h"
#include "StartupRegistration.h"
#include "UpdateChecker.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const int updateResult = UpdateChecker::HandleSelfUpdateCommandLine();
    if (updateResult >= 0) return updateResult;
    const int helperResult = StartupRegistration::HandleCommandLine();
    if (helperResult >= 0) return helperResult;
    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\TPMate.SingleInstance");
    if (!singleInstance)
        return 1;
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    if (alreadyRunning)
    {
        // The first process may still be creating its window.
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            HWND window = FindWindowW(L"TPMateNativeWindow", nullptr);
            if (window)
            {
                DWORD processId = 0;
                GetWindowThreadProcessId(window, &processId);
                AllowSetForegroundWindow(processId);
                PostMessageW(window, WM_APP + 5, 0, 0);
                break;
            }
            Sleep(50);
        }
        CloseHandle(singleInstance);
        return 0;
    }
    TrayApp app;
    const int result = app.Run(instance);
    CloseHandle(singleInstance);
    return result;
}
