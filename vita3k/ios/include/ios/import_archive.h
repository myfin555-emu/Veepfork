#pragma once

#include <filesystem>
#include <string>

namespace ios::import {
// Streams ZIP/VPK entries to disk; memory use does not scale with game size.
// The caller owns (and cleans up) the private extraction directory.
bool extract_zip(const std::filesystem::path &source, const std::filesystem::path &destination, std::string &error);
}
