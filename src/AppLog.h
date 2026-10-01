#pragma once

#include <functional>
#include <string>

enum class LogLevel
{
    Verbose,
    Info,
    Warning,
    Error,
};

using LogCallback = std::function<void(LogLevel level, const std::string& message)>;
