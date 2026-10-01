#pragma once

#include "AppLog.h"
#include "SessionInfo.h"

#include <atomic>
#include <string>
#include <vector>

enum class PaintType
{
    Car,
    CarDecal,
    CarNumber,
    CarSpec,
    Helmet,
    Suit,
};

struct PaintFile
{
    int userId = 0;
    int teamId = 0;
    std::string carPath;
    PaintType type = PaintType::Car;
    bool hasValidType = false;
    std::string url;
};

class TradingPaintsClient final
{
public:
    explicit TradingPaintsClient(LogCallback logCallback);
    ~TradingPaintsClient();
    TradingPaintsClient(const TradingPaintsClient&) = delete;
    TradingPaintsClient& operator=(const TradingPaintsClient&) = delete;
    std::vector<PaintFile> FetchSessionPaints(const SessionInfo& session, const std::atomic_bool& stopping,
        unsigned int maxConcurrency);
    bool Download(const std::string& url, std::vector<unsigned char>& contents, std::string& error,
        const std::atomic_bool& stopping);

private:
    std::vector<PaintFile> FetchUserPaints(int userId, const std::atomic_bool& stopping);
    std::vector<PaintFile> FetchTeamPaints(const SessionInfo& session, const std::atomic_bool& stopping);
    std::vector<PaintFile> ParsePaintXml(const std::vector<unsigned char>& xml, int fallbackUserId);
    void Log(LogLevel level, const std::string& message) const;

    LogCallback logCallback_;
    void* httpSession_ = nullptr;
};
