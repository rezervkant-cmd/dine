#pragma once
#include <filesystem>
#include <string>

namespace vw::app {

std::filesystem::path download_vanilla_jar(const std::string& version_hint,
                                           std::string& error_out);

}
