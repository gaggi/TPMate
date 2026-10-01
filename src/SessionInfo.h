#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct SessionDriver
{
    int carIndex = -1;
    int userId = 0;
    int teamId = 0;
    std::string carPath;
    std::string carNumber;
};

struct SessionInfo
{
    int sessionId = 0;
    int subSessionId = 0;
    int seriesId = 0;
    int leagueId = 0;
    int playerCarIndex = -1;
    bool teamRacing = false;
    std::vector<SessionDriver> drivers;

    std::wstring Key() const;
};

std::optional<SessionInfo> ParseSessionInfo(const std::string& yaml, std::string* error = nullptr);
