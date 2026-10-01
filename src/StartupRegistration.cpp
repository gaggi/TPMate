#include "StartupRegistration.h"

#include <taskschd.h>
#include <sddl.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace
{
    template<class T> struct ComPtr
    {
        T* value = nullptr;
        ~ComPtr() { if (value) value->Release(); }
        T* operator->() const { return value; }
        T** Out() { return &value; }
    };

    struct BStr
    {
        BSTR value;
        explicit BStr(const wchar_t* text) : value(SysAllocString(text)) {}
        ~BStr() { SysFreeString(value); }
        operator BSTR() const { return value; }
    };

    struct ComScope
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
        bool Ready() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
    };

    std::wstring CurrentUserSid()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> data(size);
        const bool ok = size && GetTokenInformation(token, TokenUser, data.data(), size, &size);
        CloseHandle(token);
        if (!ok) return {};
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) return {};
        std::wstring result(sid);
        LocalFree(sid);
        return result;
    }

    bool IsElevated()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }

    std::wstring ExecutablePath()
    {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return {};
        path.resize(length);
        return path;
    }

    bool OpenTaskFolder(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& folder)
    {
        VARIANT empty{};
        return SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                   IID_ITaskService, reinterpret_cast<void**>(service.Out()))) &&
            SUCCEEDED(service->Connect(empty, empty, empty, empty)) &&
            SUCCEEDED(service->GetFolder(BStr(L"\\"), folder.Out()));
    }

    bool Configure(bool enabled, const std::wstring& userSid)
    {
        // Preserve the original user even if UAC uses a different administrator account.
        PSID parsedSid = nullptr;
        if (!ConvertStringSidToSidW(userSid.c_str(), &parsedSid)) return false;
        const bool validSid = IsValidSid(parsedSid) != FALSE;
        LocalFree(parsedSid);
        if (!validSid || !IsElevated()) return false;
        ComScope com;
        if (!com.Ready()) return false;
        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> folder;
        if (!OpenTaskFolder(service, folder)) return false;
        const std::wstring name = L"TPMate Startup " + userSid;
        if (!enabled)
        {
            const HRESULT result = folder->DeleteTask(BStr(name.c_str()), 0);
            return SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
        const auto path = ExecutablePath();
        if (path.empty()) return false;
        ComPtr<ITaskDefinition> task;
        ComPtr<IPrincipal> principal;
        ComPtr<ITaskSettings> settings;
        ComPtr<ITriggerCollection> triggers;
        ComPtr<ITrigger> trigger;
        ComPtr<ILogonTrigger> logon;
        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> exec;
        if (FAILED(service->NewTask(0, task.Out())) ||
            FAILED(task->get_Principal(principal.Out())) ||
            FAILED(principal->put_UserId(BStr(userSid.c_str()))) ||
            FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
            FAILED(principal->put_RunLevel(TASK_RUNLEVEL_LUA)) ||
            FAILED(task->get_Settings(settings.Out())) ||
            FAILED(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
            FAILED(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
            FAILED(settings->put_ExecutionTimeLimit(BStr(L"PT0S"))) ||
            FAILED(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)) ||
            FAILED(task->get_Triggers(triggers.Out())) ||
            FAILED(triggers->Create(TASK_TRIGGER_LOGON, trigger.Out())) ||
            FAILED(trigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(logon.Out()))) ||
            FAILED(logon->put_UserId(BStr(userSid.c_str()))) ||
            FAILED(task->get_Actions(actions.Out())) ||
            FAILED(actions->Create(TASK_ACTION_EXEC, action.Out())) ||
            FAILED(action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(exec.Out()))) ||
            FAILED(exec->put_Path(BStr(path.c_str())))) return false;
        BStr sid(userSid.c_str());
        VARIANT user{};
        user.vt = VT_BSTR;
        user.bstrVal = sid;
        VARIANT empty{};
        ComPtr<IRegisteredTask> registered;
        return SUCCEEDED(folder->RegisterTaskDefinition(BStr(name.c_str()), task.value,
            TASK_CREATE_OR_UPDATE, user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, registered.Out()));
    }
}

bool StartupRegistration::IsEnabled()
{
    const auto sid = CurrentUserSid();
    if (sid.empty()) return false;
    ComScope com;
    if (!com.Ready()) return false;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    ComPtr<IRegisteredTask> task;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    const auto name = L"TPMate Startup " + sid;
    return OpenTaskFolder(service, folder) &&
        SUCCEEDED(folder->GetTask(BStr(name.c_str()), task.Out())) &&
        SUCCEEDED(task->get_Enabled(&enabled)) && enabled != VARIANT_FALSE;
}

bool StartupRegistration::Apply(HWND owner, bool enabled)
{
    SetLastError(ERROR_SUCCESS);
    const auto sid = CurrentUserSid();
    const auto path = ExecutablePath();
    if (sid.empty() || path.empty()) return false;
    if (IsElevated()) return Configure(enabled, sid);
    const auto arguments = L"--tpmate-configure-startup " + std::wstring(enabled ? L"1" : L"0") + L" \"" + sid + L"\"";
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.hwnd = owner;
    execute.lpVerb = L"runas";
    execute.lpFile = path.c_str();
    execute.lpParameters = arguments.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute) || !execute.hProcess) return false;
    const DWORD wait = WaitForSingleObject(execute.hProcess, 60000);
    DWORD result = 1;
    const bool ok = wait == WAIT_OBJECT_0 && GetExitCodeProcess(execute.hProcess, &result) && result == 0;
    CloseHandle(execute.hProcess);
    return ok;
}

int StartupRegistration::HandleCommandLine()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return -1;
    int result = -1;
    if (count >= 2 && std::wstring(arguments[1]) == L"--tpmate-configure-startup")
    {
        result = 1;
        if (count == 4 && (std::wstring(arguments[2]) == L"1" || std::wstring(arguments[2]) == L"0"))
            result = Configure(std::wstring(arguments[2]) == L"1", arguments[3]) ? 0 : 1;
    }
    LocalFree(arguments);
    return result;
}
