#pragma once

#include "AppLog.h"
#include "TradingPaintsClient.h"

#include <atomic>
#include <mutex>
#include <semaphore>
#include <string>
#include <unordered_set>
#include <vector>

class PaintStore final
{
public:
    explicit PaintStore(LogCallback logCallback);
    bool Install(const PaintFile& paint, const std::vector<unsigned char>& compressedContents,
        const std::atomic_bool& stopping);
    bool DeleteDownloadedPaints();
    // Stops tracking the downloaded paints without deleting them, e.g. because the
    // whole paint folder goes to the Recycle Bin next.
    void ForgetDownloadedPaints();
    // Returns false for damaged or truncated data, an oversized result, or when stopping.
    static bool DecompressBzip2(const std::vector<unsigned char>& input, std::vector<unsigned char>& output,
        const std::atomic_bool& stopping);

private:
    std::wstring DestinationPath(const PaintFile& paint) const;
    std::wstring StateDirectory() const;
    std::wstring ManifestPath() const;
    void LoadManifest();
    bool EnsureStateDirectory() const;
    bool SaveManifestLocked();
    bool AppendManifestLocked(const std::wstring& destination);
    void Log(LogLevel level, const std::string& message) const;

    LogCallback logCallback_;
    mutable std::mutex mutex_;
    std::counting_semaphore<2> installSlots_{2};
    std::unordered_set<std::wstring> managedFiles_;
};
