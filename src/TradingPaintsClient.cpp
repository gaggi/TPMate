#include "TradingPaintsClient.h"

#include <windows.h>
#include <winhttp.h>
#include <objidl.h>
#include <xmllite.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <map>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    constexpr wchar_t kUserEndpoint[] = L"https://fetch.tradingpaints.gg/fetch_user.php?user=";
    constexpr wchar_t kTeamEndpoint[] = L"https://fetch.tradingpaints.gg/fetch.php";
    constexpr size_t kMaximumResponseBytes = 64 * 1024 * 1024;

    std::wstring Utf8ToWide(std::string_view value)
    {
        if (value.empty())
            return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0)
            return {};
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), count);
        return result;
    }

    std::string WideToUtf8(std::wstring_view value)
    {
        if (value.empty())
            return {};
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (count <= 0)
            return {};
        std::string result(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), count, nullptr, nullptr);
        return result;
    }

    std::string Trim(std::string value)
    {
        const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string UrlEncode(std::string_view value)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(value.size() * 3);
        for (unsigned char ch : value)
        {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            {
                result.push_back(static_cast<char>(ch));
            }
            else if (ch == ' ')
            {
                result.push_back('+');
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[(ch >> 4) & 0xF]);
                result.push_back(hex[ch & 0xF]);
            }
        }
        return result;
    }

    bool HttpRequest(HINTERNET session, const std::wstring& url, const std::string* postBody,
        std::vector<unsigned char>& response, std::string& error, const std::atomic_bool& stopping)
    {
        response.clear();
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS)
        {
            error = "Only valid HTTPS URLs are supported.";
            return false;
        }

        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength)
            path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        if (path.empty())
            path = L"/";

        if (!session)
        {
            error = "Could not initialize Windows HTTP.";
            return false;
        }

        HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
        if (!connection)
        {
            error = "Could not connect to the Trading Paints server.";
            return false;
        }

        const wchar_t* method = postBody ? L"POST" : L"GET";
        HINTERNET request = WinHttpOpenRequest(connection, method, path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request)
        {
            error = "Could not create the HTTPS request.";
            WinHttpCloseHandle(connection);
            return false;
        }

        const wchar_t* headers = postBody ? L"Content-Type: application/x-www-form-urlencoded\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
        const DWORD headerLength = postBody ? static_cast<DWORD>(-1) : 0;
        LPVOID body = postBody ? const_cast<char*>(postBody->data()) : WINHTTP_NO_REQUEST_DATA;
        const DWORD bodyLength = postBody ? static_cast<DWORD>(postBody->size()) : 0;
        bool ok = !stopping.load() && WinHttpSendRequest(request, headers, headerLength,
            body, bodyLength, bodyLength, 0) && WinHttpReceiveResponse(request, nullptr);
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (ok)
            ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
        if (!ok || statusCode < 200 || statusCode >= 300)
        {
            error = stopping.load() ? "Request cancelled." :
                (statusCode ? "Trading Paints returned HTTP " + std::to_string(statusCode) + "." : "The HTTPS request failed.");
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            return false;
        }

        DWORD contentLength = 0;
        DWORD contentLengthSize = sizeof(contentLength);
        const bool hasContentLength = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
        if (hasContentLength && contentLength <= kMaximumResponseBytes)
            response.reserve(contentLength);
        constexpr size_t readBlock = 64 * 1024;
        while (!stopping.load())
        {
            const size_t oldSize = response.size();
            const size_t capacity = (std::min)(readBlock, kMaximumResponseBytes + 1 - oldSize);
            response.resize(oldSize + capacity);
            DWORD received = 0;
            if (!WinHttpReadData(request, response.data() + oldSize, static_cast<DWORD>(capacity), &received))
            {
                error = "Failed while downloading the response body.";
                response.resize(oldSize);
                ok = false;
                break;
            }
            response.resize(oldSize + received);
            if (response.size() > kMaximumResponseBytes)
            {
                error = "The HTTPS response exceeded the 64 MB safety limit.";
                ok = false;
                break;
            }
            if (received == 0)
                break;
        }
        if (stopping.load())
        {
            error = "Request cancelled.";
            ok = false;
        }
        // A connection that closes early can still look like a normal end of the body.
        if (ok && hasContentLength && response.size() != contentLength)
        {
            error = "The download ended early (" + std::to_string(response.size()) + " of " +
                std::to_string(contentLength) + " bytes).";
            ok = false;
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        return ok;
    }

    bool ParseType(std::string value, PaintType& result)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        if (value == "car") result = PaintType::Car;
        else if (value == "car_decal") result = PaintType::CarDecal;
        else if (value == "car_num") result = PaintType::CarNumber;
        else if (value == "car_spec") result = PaintType::CarSpec;
        else if (value == "helmet") result = PaintType::Helmet;
        else if (value == "suit") result = PaintType::Suit;
        else return false;
        return true;
    }

    bool ParseInteger(const std::string& value, int& result)
    {
        if (value.empty())
            return false;
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || parsed < 0 || parsed > 2'000'000'000)
            return false;
        result = static_cast<int>(parsed);
        return true;
    }

    bool XmlToPaints(const std::vector<unsigned char>& xml, int fallbackUserId, std::vector<PaintFile>& paints)
    {
        if (xml.empty() || xml.size() > MAXDWORD)
            return false;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, xml.size());
        if (!memory)
            return false;
        void* bytes = GlobalLock(memory);
        if (!bytes)
        {
            GlobalFree(memory);
            return false;
        }
        std::memcpy(bytes, xml.data(), xml.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)))
        {
            GlobalFree(memory);
            return false;
        }
        IXmlReader* reader = nullptr;
        HRESULT hr = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr);
        if (FAILED(hr))
        {
            stream->Release();
            return false;
        }
        if (FAILED(reader->SetInput(stream)))
        {
            reader->Release();
            stream->Release();
            return false;
        }

        PaintFile current;
        std::wstring currentField;
        bool inCar = false;
        XmlNodeType nodeType{};
        while ((hr = reader->Read(&nodeType)) == S_OK)
        {
            if (nodeType == XmlNodeType_Element)
            {
                const wchar_t* localName = nullptr;
                UINT length = 0;
                reader->GetLocalName(&localName, &length);
                const std::wstring name(localName, length);
                if (name == L"Car")
                {
                    current = {};
                    current.userId = fallbackUserId;
                    inCar = true;
                    currentField.clear();
                    if (reader->IsEmptyElement())
                        inCar = false;
                }
                else if (inCar)
                {
                    currentField = name;
                }
            }
            else if ((nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA) && inCar && !currentField.empty())
            {
                const wchar_t* value = nullptr;
                UINT length = 0;
                if (SUCCEEDED(reader->GetValue(&value, &length)))
                {
                    const std::string text = Trim(WideToUtf8(std::wstring_view(value, length)));
                    if (currentField == L"file") current.url += text;
                    else if (currentField == L"directory") current.carPath += text;
                    else if (currentField == L"type") current.hasValidType = ParseType(text, current.type);
                    else if (currentField == L"userid") ParseInteger(text, current.userId);
                    else if (currentField == L"teamid") ParseInteger(text, current.teamId);
                }
            }
            else if (nodeType == XmlNodeType_EndElement && inCar)
            {
                const wchar_t* localName = nullptr;
                UINT length = 0;
                reader->GetLocalName(&localName, &length);
                const std::wstring name(localName, length);
                if (name == L"Car")
                {
                    if (!current.url.empty() && current.hasValidType)
                        paints.push_back(current);
                    inCar = false;
                    currentField.clear();
                }
                else if (name == currentField)
                {
                    currentField.clear();
                }
            }
        }

        reader->Release();
        stream->Release();
        // S_FALSE is the regular end of the document; anything else means truncated or broken XML.
        return hr == S_FALSE;
    }

    std::wstring BuildUserUrl(int userId)
    {
        return std::wstring(kUserEndpoint) + std::to_wstring(userId);
    }

    bool IsWearable(PaintType type)
    {
        return type == PaintType::Helmet || type == PaintType::Suit;
    }
}

TradingPaintsClient::TradingPaintsClient(LogCallback logCallback) : logCallback_(std::move(logCallback))
{
    httpSession_ = WinHttpOpen(L"TPMate/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (httpSession_)
        WinHttpSetTimeouts(httpSession_, 10000, 10000, 20000, 30000);
}

TradingPaintsClient::~TradingPaintsClient()
{
    if (httpSession_)
        WinHttpCloseHandle(httpSession_);
}

void TradingPaintsClient::Log(LogLevel level, const std::string& message) const
{
    if (logCallback_)
        logCallback_(level, message);
}

std::vector<PaintFile> TradingPaintsClient::FetchSessionPaints(const SessionInfo& session,
    const std::atomic_bool& stopping, unsigned int maxConcurrency, std::vector<int>* failedUsers)
{
    Log(LogLevel::Info, "Checking Trading Paints for session " + std::to_string(session.sessionId) + ".");
    if (session.drivers.empty())
    {
        Log(LogLevel::Warning, "iRacing session info contained no paint-eligible drivers.");
        return {};
    }

    std::vector<PaintFile> files;
    if (session.teamRacing)
    {
        Log(LogLevel::Verbose, "Using the Trading Paints team-session lookup.");
        bool failed = false;
        files = FetchTeamPaints(session, stopping, failed);
        if (failed && failedUsers)
            for (const auto& driver : session.drivers) failedUsers->push_back(driver.userId);
    }
    else
    {
        struct UserQuery
        {
            int userId = 0;
            std::vector<std::string> carPaths;
        };
        std::vector<UserQuery> users;
        std::map<int, size_t> userIndexes;
        for (const auto& driver : session.drivers)
        {
            if (driver.userId <= 0)
                continue;
            auto [it, inserted] = userIndexes.emplace(driver.userId, users.size());
            if (inserted)
                users.push_back({driver.userId, {}});
            auto& carPaths = users[it->second].carPaths;
            if (!driver.carPath.empty() && std::find_if(carPaths.begin(), carPaths.end(), [&](const std::string& path)
                { return _stricmp(path.c_str(), driver.carPath.c_str()) == 0; }) == carPaths.end())
                carPaths.push_back(driver.carPath);
        }

        std::vector<std::vector<PaintFile>> userResults(users.size());
        std::vector<char> lookupFailed(users.size(), 0);
        std::atomic_size_t nextUser{0};
        const auto lookupWorker = [&]()
        {
            while (!stopping.load())
            {
                const size_t index = nextUser.fetch_add(1);
                if (index >= users.size())
                    break;
                const auto& user = users[index];
                std::string carList;
                for (const auto& path : user.carPaths)
                {
                    if (!carList.empty()) carList += ", ";
                    carList += path;
                }
                Log(LogLevel::Verbose, "Checking paints for user " + std::to_string(user.userId) +
                    (carList.empty() ? "." : " in cars " + carList + "."));
                bool failed = false;
                auto userFiles = FetchUserPaints(user.userId, stopping, failed);
                lookupFailed[index] = failed ? 1 : 0;
                for (auto& file : userFiles)
                {
                    const bool belongsToCar = std::find_if(user.carPaths.begin(), user.carPaths.end(),
                        [&](const std::string& path) { return _stricmp(file.carPath.c_str(), path.c_str()) == 0; }) != user.carPaths.end();
                    if (IsWearable(file.type) || belongsToCar)
                    {
                        file.userId = user.userId;
                        userResults[index].push_back(std::move(file));
                    }
                }
            }
        };
        const size_t workerCount = (std::min)(static_cast<size_t>((std::clamp)(maxConcurrency, 1u, 10u)), users.size());
        std::vector<std::thread> workers;
        bool workerCreationFailed = false;
        for (size_t i = 0; i < workerCount; ++i)
        {
            try { workers.emplace_back(lookupWorker); }
            catch (...) { workerCreationFailed = true; break; }
        }
        for (auto& worker : workers)
            if (worker.joinable()) worker.join();
        if (workerCreationFailed)
            lookupWorker();
        for (auto& userFiles : userResults)
            files.insert(files.end(), std::make_move_iterator(userFiles.begin()), std::make_move_iterator(userFiles.end()));
        if (failedUsers)
            for (size_t index = 0; index < users.size(); ++index)
                if (lookupFailed[index]) failedUsers->push_back(users[index].userId);
    }

    std::map<std::string, PaintFile> uniqueFiles;
    for (auto& file : files)
    {
        const auto key = std::to_string(file.userId) + "|" + std::to_string(file.teamId) + "|" + file.carPath + "|" +
            std::to_string(static_cast<int>(file.type));
        uniqueFiles.insert_or_assign(key, std::move(file));
    }
    files.clear();
    for (auto& [key, file] : uniqueFiles)
    {
        (void)key;
        files.push_back(std::move(file));
    }
    Log(LogLevel::Info, "Trading Paints returned " + std::to_string(files.size()) + " paint files.");
    return files;
}

std::vector<PaintFile> TradingPaintsClient::FetchUserPaints(int userId, const std::atomic_bool& stopping, bool& failed)
{
    std::vector<unsigned char> response;
    std::string error;
    failed = false;
    if (!HttpRequest(httpSession_, BuildUserUrl(userId), nullptr, response, error, stopping))
    {
        failed = !stopping.load();
        if (failed)
            Log(LogLevel::Warning, "Could not fetch paints for user " + std::to_string(userId) + ": " + error);
        return {};
    }
    std::vector<PaintFile> paints;
    if (!response.empty() && !ParsePaintXml(response, userId, paints))
    {
        failed = !stopping.load();
        Log(LogLevel::Warning, "Trading Paints sent an incomplete or invalid answer for user " + std::to_string(userId) + ".");
        return {};
    }
    if (paints.empty())
        Log(LogLevel::Verbose, "Trading Paints has no paints for user " + std::to_string(userId) + ".");
    return paints;
}

std::vector<PaintFile> TradingPaintsClient::FetchTeamPaints(const SessionInfo& session, const std::atomic_bool& stopping, bool& failed)
{
    failed = false;
    std::string list;
    for (const auto& driver : session.drivers)
    {
        if (driver.userId <= 0 || driver.carPath.empty())
            continue;
        if (!list.empty())
            list.push_back(',');
        list += std::to_string(driver.userId) + "=" + driver.carPath + "=" +
            std::to_string(driver.teamId) + "=" + driver.carNumber;
    }
    if (list.empty())
        return {};

    const std::string body = "list=" + UrlEncode(list) + "&series=" + std::to_string(session.seriesId) +
        "&league=" + std::to_string(session.leagueId) + "&team=1&user=" + std::to_string(session.playerCarIndex);
    std::vector<unsigned char> response;
    std::string error;
    if (!HttpRequest(httpSession_, kTeamEndpoint, &body, response, error, stopping))
    {
        failed = !stopping.load();
        if (failed)
            Log(LogLevel::Warning, "Could not fetch team paints: " + error);
        return {};
    }
    std::vector<PaintFile> paints;
    if (!response.empty() && !ParsePaintXml(response, 0, paints))
    {
        failed = !stopping.load();
        Log(LogLevel::Warning, "Trading Paints sent an incomplete or invalid answer for the team session.");
        return {};
    }
    if (paints.empty())
        Log(LogLevel::Verbose, "Trading Paints has no paints for the team session.");
    return paints;
}

bool TradingPaintsClient::ParsePaintXml(const std::vector<unsigned char>& xml, int fallbackUserId, std::vector<PaintFile>& paints)
{
    paints.clear();
    if (!XmlToPaints(xml, fallbackUserId, paints))
    {
        paints.clear();
        return false;
    }
    paints.erase(std::remove_if(paints.begin(), paints.end(), [](const PaintFile& file)
    {
        return !file.hasValidType || file.url.empty() || file.url.rfind("https://", 0) != 0 ||
            (!IsWearable(file.type) && file.carPath.empty());
    }), paints.end());
    return true;
}

bool TradingPaintsClient::Download(const std::string& url, std::vector<unsigned char>& contents,
    std::string& error, const std::atomic_bool& stopping)
{
    const auto wideUrl = Utf8ToWide(url);
    if (wideUrl.empty())
    {
        error = "Trading Paints returned an invalid file URL.";
        return false;
    }
    return HttpRequest(httpSession_, wideUrl, nullptr, contents, error, stopping);
}
