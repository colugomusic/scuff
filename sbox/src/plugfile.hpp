#pragma once

#include "common/types.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace sbox::plugfile {

[[nodiscard]] auto add(const fs::path& path) -> tom::id::plugfile;
[[nodiscard]] auto find(const fs::path& path) -> tom::id::plugfile;
[[nodiscard]] auto get_path(tom::id::plugfile idx) -> const fs::path&;
auto create() -> void;
auto destroy() -> void;

} // sbox::plugfile