#include "SessionInfo.h"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace
{
    struct YamlLine
    {
        int indent = 0;
        std::string_view text;
    };

    std::vector<YamlLine> Lines(const std::string& yaml)
    {
        std::vector<YamlLine> lines;
        size_t start = 0;
        while (start < yaml.size())
        {
            const size_t end = yaml.find('\n', start);
            const size_t length = (end == std::string::npos ? yaml.size() : end) - start;
            std::string_view line(yaml.data() + start, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (start == 0 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.remove_prefix(3);
            int indent = 0;
            while (indent < static_cast<int>(line.size()) && line[indent] == ' ')
                ++indent;
            auto content = line.substr(static_cast<size_t>(indent));
            if (!content.empty() && content.front() != '#')
                lines.push_back({indent, content});
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return lines;
    }

    std::string_view Value(std::string_view line, std::string_view key)
    {
        if (line.size() <= key.size() || line.substr(0, key.size()) != key || line[key.size()] != ':')
            return {};
        auto result = line.substr(key.size() + 1);
        while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
            result.remove_prefix(1);
        while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
            result.remove_suffix(1);
        if (result.size() >= 2 && ((result.front() == '\'' && result.back() == '\'') ||
            (result.front() == '"' && result.back() == '"')))
        {
            result.remove_prefix(1);
            result.remove_suffix(1);
        }
        if (result == ",")
            return {};
        return result;
    }

    bool ToInt(std::string_view text, int& value)
    {
        if (text.empty())
            return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    std::string GetSectionValue(const std::vector<YamlLine>& lines, std::string_view section, std::string_view key)
    {
        for (size_t index = 0; index < lines.size(); ++index)
        {
            if (lines[index].indent != 0 || Value(lines[index].text, section).data() == nullptr)
                continue;
            int directChildIndent = -1;
            for (size_t child = index + 1; child < lines.size() && lines[child].indent > 0; ++child)
            {
                if (directChildIndent < 0)
                    directChildIndent = lines[child].indent;
                if (lines[child].indent == directChildIndent)
                {
                    const auto value = Value(lines[child].text, key);
                    if (!value.empty())
                        return std::string(value);
                }
            }
            break;
        }
        return {};
    }

    int GetTopInt(const std::vector<YamlLine>& lines, std::string_view section, std::string_view key)
    {
        int value = 0;
        const auto raw = GetSectionValue(lines, section, key);
        ToInt(raw, value);
        return value;
    }

    bool HasSectionKey(const std::vector<YamlLine>& lines, std::string_view section, std::string_view key)
    {
        for (size_t index = 0; index < lines.size(); ++index)
        {
            if (lines[index].indent != 0 || Value(lines[index].text, section).data() == nullptr)
                continue;
            int directChildIndent = -1;
            for (size_t child = index + 1; child < lines.size() && lines[child].indent > 0; ++child)
            {
                if (directChildIndent < 0)
                    directChildIndent = lines[child].indent;
                if (lines[child].indent == directChildIndent && Value(lines[child].text, key).data() != nullptr)
                    return true;
            }
            return false;
        }
        return false;
    }

    bool SafeCarPath(std::string_view path)
    {
        if (path.empty() || path.size() > 96)
            return false;
        return std::all_of(path.begin(), path.end(), [](unsigned char ch)
        {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        });
    }

    bool HasPrefix(std::string_view line, std::string_view prefix)
    {
        return line.size() >= prefix.size() && line.substr(0, prefix.size()) == prefix;
    }
}

std::wstring SessionInfo::Key() const
{
    return std::to_wstring(sessionId) + L"_" + std::to_wstring(subSessionId);
}

std::optional<SessionInfo> ParseSessionInfo(const std::string& yaml, std::string* error)
{
    if (error) error->clear();
    const auto lines = Lines(yaml);
    SessionInfo session;
    if (!HasSectionKey(lines, "WeekendInfo", "SessionID"))
    {
        if (error)
        {
            bool hasWeekendInfo = false;
            for (const auto& line : lines)
                hasWeekendInfo |= line.indent == 0 && Value(line.text, "WeekendInfo").data() != nullptr;
            *error = hasWeekendInfo ? "WeekendInfo.SessionID was missing" : "top-level WeekendInfo section was missing";
            if (yaml.empty()) *error = "session info buffer was empty";
        }
        return std::nullopt;
    }
    session.sessionId = GetTopInt(lines, "WeekendInfo", "SessionID");
    session.subSessionId = GetTopInt(lines, "WeekendInfo", "SubSessionID");
    session.seriesId = GetTopInt(lines, "WeekendInfo", "SeriesID");
    session.leagueId = GetTopInt(lines, "WeekendInfo", "LeagueID");
    session.teamRacing = GetTopInt(lines, "WeekendInfo", "TeamRacing") == 1;
    session.playerCarIndex = GetTopInt(lines, "DriverInfo", "DriverCarIdx");
    session.trackName = GetSectionValue(lines, "WeekendInfo", "TrackDisplayName");

    bool inDrivers = false;
    int driversIndent = -1;
    int entryIndent = -1;
    SessionDriver current;
    bool hasDriver = false;
    const auto saveDriver = [&]()
    {
        if (hasDriver && current.carIndex >= 0 && current.carIndex == session.playerCarIndex)
            session.playerCarName = current.carName;
        if (hasDriver && current.userId > 0 && SafeCarPath(current.carPath))
            session.drivers.push_back(current);
    };

    for (const auto& line : lines)
    {
        if (!inDrivers && Value(line.text, "Drivers").data() != nullptr)
        {
            inDrivers = true;
            driversIndent = line.indent;
            continue;
        }
        if (!inDrivers)
            continue;
        if (line.indent < driversIndent)
        {
            saveDriver();
            break;
        }
        if (line.indent == driversIndent && HasPrefix(line.text, "- "))
        {
            saveDriver();
            current = {};
            hasDriver = true;
            entryIndent = line.indent;
            const auto firstField = line.text.substr(2);
            const auto carIdx = Value(firstField, "CarIdx");
            if (!carIdx.empty())
                ToInt(carIdx, current.carIndex);
            continue;
        }
        if (!hasDriver || line.indent <= entryIndent)
            continue;

        const auto assignInt = [&](std::string_view key, int& target)
        {
            const auto value = Value(line.text, key);
            if (!value.empty())
                ToInt(value, target);
        };
        assignInt("UserID", current.userId);
        assignInt("TeamID", current.teamId);
        assignInt("CarIdx", current.carIndex);
        const auto carPath = Value(line.text, "CarPath");
        if (!carPath.empty())
            current.carPath.assign(carPath);
        const auto carName = Value(line.text, "CarScreenName");
        if (!carName.empty())
            current.carName.assign(carName);
        const auto userName = Value(line.text, "UserName");
        if (!userName.empty())
            current.userName.assign(userName);
        const auto teamName = Value(line.text, "TeamName");
        if (!teamName.empty())
            current.teamName.assign(teamName);
        const auto carNumber = Value(line.text, "CarNumber");
        if (!carNumber.empty())
            current.carNumber.assign(carNumber);
    }
    if (inDrivers)
        saveDriver();

    std::sort(session.drivers.begin(), session.drivers.end(), [](const auto& left, const auto& right)
    {
        if (left.userId != right.userId)
            return left.userId < right.userId;
        if (left.teamId != right.teamId)
            return left.teamId < right.teamId;
        if (left.carPath != right.carPath)
            return left.carPath < right.carPath;
        return left.carIndex < right.carIndex;
    });
    session.drivers.erase(std::unique(session.drivers.begin(), session.drivers.end(), [](const auto& left, const auto& right)
    {
        return left.userId == right.userId && left.teamId == right.teamId &&
            left.carPath == right.carPath && left.carIndex == right.carIndex;
    }), session.drivers.end());
    return session;
}
