#pragma once

#include "common/messages.hpp"
#include <string_view>
#include <vector>

namespace sbox::shm {

[[nodiscard]] auto open(std::string_view group, std::string_view sandbox) -> bool;
auto destroy() -> void;
auto receive_input_messages(std::vector<scuff::msg::in::msg>* msgs) -> void;

} // sbox::shm
