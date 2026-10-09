#include "PaintStore.h"

#include "bzlib.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    constexpr size_t kMaxUncompressedBytes = 64 * 1024 * 1024;

    std::wstring GetKnownFolderPath(int folder)
    {
        wchar_t path[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, path)))
            return {};
        return path;
    }

    std::wstring GetPaintRoot()
    {
        const auto documents = GetKnownFolderPath(CSIDL_PERSONAL);
        return documents.empty() ? std::wstring{} : documents + L"\\iRacing\\paint";
    }

    std::wstring PaintName(const PaintFile& paint)
    {
        const bool team = paint.teamId > 0;
        const std::wstring userId = std::to_wstring(paint.userId);
        const std::wstring teamId = std::to_wstring(paint.teamId);
        switch (paint.type)
        {
        case PaintType::Car: return team ? L"car_team_" + teamId + L".tga" : L"car_" + userId + L".tga";
        case PaintType::CarDecal: return team ? L"decal_team_" + teamId + L".tga" : L"decal_" + userId + L".tga";
        case PaintType::CarNumber: return team ? L"car_num_team_" + teamId + L".tga" : L"car_num_" + userId + L".tga";
        case PaintType::CarSpec: return team ? L"car_spec_team_" + teamId + L".mip" : L"car_spec_" + userId + L".mip";
        case PaintType::Helmet: return L"helmet_" + userId + L".tga";
        case PaintType::Suit: return team ? L"suit_team_" + teamId + L".tga" : L"suit_" + userId + L".tga";
        }
        return {};
    }

    bool IsSafeCarPath(const std::string& path)
    {
        if (path.empty() || path.size() > 96)
            return false;
        return std::all_of(path.begin(), path.end(), [](unsigned char ch)
        {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        });
    }

    bool IsWithin(const std::wstring& root, const std::wstring& candidate)
    {
        if (root.empty() || candidate.empty())
            return false;
        const auto normalizedRoot = std::filesystem::absolute(root).lexically_normal().wstring();
        const auto normalizedCandidate = std::filesystem::absolute(candidate).lexically_normal().wstring();
        if (normalizedCandidate.size() <= normalizedRoot.size() ||
            _wcsnicmp(normalizedRoot.c_str(), normalizedCandidate.c_str(), normalizedRoot.size()) != 0)
            return false;
        return normalizedCandidate[normalizedRoot.size()] == L'\\' || normalizedRoot.back() == L'\\';
    }

    bool WriteFileContents(const std::wstring& path, const std::vector<unsigned char>& contents)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        size_t writtenTotal = 0;
        bool ok = true;
        while (writtenTotal < contents.size())
        {
            const DWORD toWrite = static_cast<DWORD>((std::min)(contents.size() - writtenTotal,
                static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written = 0;
            if (!WriteFile(file, contents.data() + writtenTotal, toWrite, &written, nullptr) || written == 0)
            {
                ok = false;
                break;
            }
            writtenTotal += written;
        }
        CloseHandle(file);
        return ok;
    }

    bool EndsWithBz2(const std::string& url)
    {
        const auto query = url.find_first_of("?#");
        const auto pathEnd = query == std::string::npos ? url.size() : query;
        if (pathEnd < 4)
            return false;
        return _stricmp(url.substr(pathEnd - 4, 4).c_str(), ".bz2") == 0;
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
            return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0)
            return {};
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
        return result;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (count <= 0) return {};
        std::string result(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), count, nullptr, nullptr);
        return result;
    }
}

PaintStore::PaintStore(LogCallback logCallback) : logCallback_(std::move(logCallback))
{
    LoadManifest();
}

void PaintStore::Log(LogLevel level, const std::string& message) const
{
    if (logCallback_)
        logCallback_(level, message);
}

std::wstring PaintStore::StateDirectory() const
{
    const auto appData = GetKnownFolderPath(CSIDL_LOCAL_APPDATA);
    return appData.empty() ? std::wstring{} : appData + L"\\TPMate";
}

std::wstring PaintStore::ManifestPath() const
{
    return StateDirectory() + L"\\managed-paints.dat";
}

std::wstring PaintStore::DestinationPath(const PaintFile& paint) const
{
    if ((paint.userId <= 0 && (paint.teamId <= 0 || paint.type == PaintType::Helmet)) || paint.teamId < 0)
        return {};
    std::wstring path = GetPaintRoot();
    if (path.empty())
        return {};
    if (!IsSafeCarPath(paint.carPath) && paint.type != PaintType::Helmet && paint.type != PaintType::Suit)
        return {};
    if (!paint.carPath.empty() && paint.type != PaintType::Helmet && paint.type != PaintType::Suit)
        path += L"\\" + Utf8ToWide(paint.carPath);
    const auto name = PaintName(paint);
    return name.empty() ? std::wstring{} : path + L"\\" + name;
}

void PaintStore::LoadManifest()
{
    std::lock_guard lock(mutex_);
    const auto manifest = ManifestPath();
    HANDLE file = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 2 || size.QuadPart > 8 * 1024 * 1024)
    {
        CloseHandle(file);
        Log(LogLevel::Warning, "The saved paint cleanup list is invalid and was ignored.");
        return;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool readOk = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!readOk || read != bytes.size() || (read % sizeof(wchar_t)) != 0)
        return;

    const auto* chars = reinterpret_cast<const wchar_t*>(bytes.data());
    size_t count = bytes.size() / sizeof(wchar_t);
    size_t position = count > 0 && chars[0] == 0xFEFF ? 1 : 0;
    const auto paintRoot = GetPaintRoot();
    while (position < count)
    {
        size_t end = position;
        while (end < count && chars[end] != L'\r' && chars[end] != L'\n' && chars[end] != L'\0')
            ++end;
        const std::wstring line(chars + position, end - position);
        // Older manifests also contain a backup path after '|'. Only retain the destination.
        const auto destination = line.substr(0, line.find(L'|'));
        if (IsWithin(paintRoot, destination))
            managedFiles_.insert(destination);
        else
            Log(LogLevel::Warning, "Ignored an unsafe entry in the saved paint cleanup list.");
        position = end;
        while (position < count && (chars[position] == L'\r' || chars[position] == L'\n' || chars[position] == L'\0'))
            ++position;
    }
    if (!managedFiles_.empty())
        Log(LogLevel::Verbose, "Loaded " + std::to_string(managedFiles_.size()) + " previously managed paint files.");
}

bool PaintStore::EnsureStateDirectory() const
{
    const auto directory = StateDirectory();
    return !directory.empty() && (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
}

bool PaintStore::SaveManifestLocked()
{
    if (!EnsureStateDirectory())
        return false;
    if (managedFiles_.empty())
    {
        DeleteFileW(ManifestPath().c_str());
        return true;
    }

    std::wstring contents(1, static_cast<wchar_t>(0xFEFF));
    for (const auto& destination : managedFiles_)
        contents += destination + L"\r\n";
    const auto temporary = ManifestPath() + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const DWORD bytesToWrite = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
    DWORD written = 0;
    const bool writeOk = WriteFile(file, contents.data(), bytesToWrite, &written, nullptr) && written == bytesToWrite && FlushFileBuffers(file);
    CloseHandle(file);
    if (!writeOk || !MoveFileExW(temporary.c_str(), ManifestPath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool PaintStore::AppendManifestLocked(const std::wstring& destination)
{
    // Appending keeps each install O(1); full rewrites only happen when files are deleted.
    if (!EnsureStateDirectory())
        return false;
    HANDLE file = CreateFileW(ManifestPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    std::wstring line;
    if (GetLastError() != ERROR_ALREADY_EXISTS)
        line.push_back(static_cast<wchar_t>(0xFEFF));
    line += destination + L"\r\n";
    const DWORD bytesToWrite = static_cast<DWORD>(line.size() * sizeof(wchar_t));
    DWORD written = 0;
    const bool ok = WriteFile(file, line.data(), bytesToWrite, &written, nullptr) && written == bytesToWrite;
    CloseHandle(file);
    return ok;
}

bool PaintStore::DecompressBzip2(const std::vector<unsigned char>& input, std::vector<unsigned char>& output,
    const std::atomic_bool& stopping)
{
    if (input.empty() || input.size() > (std::numeric_limits<unsigned int>::max)())
        return false;
    bz_stream stream{};
    if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK)
        return false;
    stream.next_in = reinterpret_cast<char*>(const_cast<unsigned char*>(input.data()));
    stream.avail_in = static_cast<unsigned int>(input.size());
    // Decompress straight into the output buffer and grow it geometrically up to the safety limit.
    output.resize((std::min)(kMaxUncompressedBytes, (std::max)(input.size() * 4, static_cast<size_t>(256 * 1024))));
    size_t produced = 0;
    int result = BZ_OK;
    bool complete = false;
    while (result == BZ_OK && !stopping.load())
    {
        if (produced == output.size())
        {
            if (output.size() >= kMaxUncompressedBytes)
            {
                result = BZ_MEM_ERROR;
                break;
            }
            output.resize((std::min)(kMaxUncompressedBytes, output.size() * 2));
        }
        stream.next_out = reinterpret_cast<char*>(output.data() + produced);
        stream.avail_out = static_cast<unsigned int>(output.size() - produced);
        const unsigned int inputBefore = stream.avail_in;
        const size_t producedBefore = produced;
        result = BZ2_bzDecompress(&stream);
        produced = output.size() - stream.avail_out;
        complete = result == BZ_STREAM_END;
        // With the input used up before the end of the stream, bzip2 keeps answering BZ_OK
        // without consuming or producing anything: the file was truncated.
        if (result == BZ_OK && stream.avail_in == inputBefore && produced == producedBefore)
            break;
    }
    BZ2_bzDecompressEnd(&stream);
    output.resize(complete ? produced : 0);
    return !output.empty();
}

bool PaintStore::Install(const PaintFile& paint, const std::vector<unsigned char>& contents,
    const std::atomic_bool& stopping)
{
    if (stopping.load())
        return false;
    installSlots_.acquire();
    struct ReleaseSlot
    {
        std::counting_semaphore<2>& slots;
        ~ReleaseSlot() { slots.release(); }
    } releaseSlot{installSlots_};
    if (stopping.load())
        return false;
    std::vector<unsigned char> unpacked;
    const std::vector<unsigned char>* output = &contents;
    if (EndsWithBz2(paint.url))
    {
        Log(LogLevel::Verbose, "Decompressing " + paint.carPath + " paint for user " + std::to_string(paint.userId) + ".");
        if (!DecompressBzip2(contents, unpacked, stopping))
        {
            if (!stopping.load())
                Log(LogLevel::Error, "Could not decompress a Trading Paints file; it is damaged or incomplete.");
            return false;
        }
        output = &unpacked;
    }
    if (output->empty() || output->size() > kMaxUncompressedBytes)
    {
        Log(LogLevel::Error, "A Trading Paints file was empty or exceeded the 64 MB paint limit.");
        return false;
    }

    const auto destination = DestinationPath(paint);
    if (destination.empty())
    {
        Log(LogLevel::Warning, "Skipped a paint with an unsafe or incomplete car path.");
        return false;
    }

    std::lock_guard lock(mutex_);
    if (stopping.load())
        return false;
    const auto directory = std::filesystem::path(destination).parent_path().wstring();
    std::error_code fsError;
    std::filesystem::create_directories(directory, fsError);
    if (fsError)
    {
        Log(LogLevel::Error, "Could not create the iRacing paint directory.");
        return false;
    }

    const auto temporary = destination + L".tpmate.tmp";
    if (!WriteFileContents(temporary, *output) ||
        !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(temporary.c_str());
        Log(LogLevel::Error, "Could not install a downloaded paint at " + WideToUtf8(destination) + ".");
        return false;
    }
    if (managedFiles_.insert(destination).second && !AppendManifestLocked(destination))
        Log(LogLevel::Warning, "Paint installed, but its cleanup entry could not be saved.");
    Log(LogLevel::Info, "Installed " + WideToUtf8(std::filesystem::path(destination).filename().wstring()) +
        " for " + (paint.carPath.empty() ? std::string("driver") : paint.carPath) + ".");
    return true;
}

void PaintStore::ForgetDownloadedPaints()
{
    std::lock_guard lock(mutex_);
    managedFiles_.clear();
    if (!SaveManifestLocked())
        Log(LogLevel::Warning, "Could not update the saved paint cleanup list.");
}

bool PaintStore::DeleteDownloadedPaints()
{
    std::lock_guard lock(mutex_);
    bool allDeleted = true;
    size_t removedCount = 0;
    size_t failedCount = 0;
    for (auto it = managedFiles_.begin(); it != managedFiles_.end();)
    {
        if (DeleteFileW(it->c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            ++removedCount;
            it = managedFiles_.erase(it);
        }
        else
        {
            allDeleted = false;
            ++failedCount;
            Log(LogLevel::Warning, "Could not delete a downloaded paint file: " + WideToUtf8(*it));
            ++it;
        }
    }
    if (!SaveManifestLocked())
    {
        Log(LogLevel::Error, "Could not update the saved paint cleanup list.");
        allDeleted = false;
    }
    if (removedCount > 0 || failedCount > 0)
        Log(allDeleted ? LogLevel::Info : LogLevel::Warning,
            "Removed " + std::to_string(removedCount) + " downloaded paint files." +
            (failedCount ? " " + std::to_string(failedCount) + " files could not be deleted." : ""));
    return allDeleted;
}
