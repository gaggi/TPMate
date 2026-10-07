#pragma once

#include "SessionInfo.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// nullopt means no trustworthy telemetry snapshot; an empty vector is a valid empty world.
using PresentCars = std::optional<std::vector<int>>;
PresentCars ReadPresentCars(std::span<const std::byte> sharedMemory);
SessionInfo SelectPaintDrivers(const SessionInfo& session, const PresentCars& presentCars, bool onlyPresent);
void RememberPaintDrivers(SessionInfo& remembered, const SessionInfo& processed);
bool SamePaintDriver(const SessionDriver& left, const SessionDriver& right);
