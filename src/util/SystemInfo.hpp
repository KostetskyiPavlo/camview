#pragma once

namespace camview {

// Logs facts about the machine that change performance numbers (board model, CPU frequency
// governor), so every log carries the context needed to compare measurements.
void logSystemInfo();

} // namespace camview