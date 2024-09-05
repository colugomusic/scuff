#pragma once

#include "common/types.hpp"
#include <compare>
#include <limits>

namespace sbox::device {

auto create() -> void;
auto destroy() -> void;

} // sbox::device