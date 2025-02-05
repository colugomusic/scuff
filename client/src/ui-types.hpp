#pragma once

#include "common-types.hpp"
#include <cs_plain_guarded.h>
#include <deque>
#include <string>
#include <variant>

namespace lg = libguarded;

namespace scuff {
namespace ui {

using general_task = std::function<void(const general_ui& ui)>;
using group_task   = std::function<void(const group_ui& ui)>;
template <typename T> using q = lg::plain_guarded<std::deque<T>>;
using general_q = q<general_task>;
using group_q   = q<group_task>;

} // ui
} // scuff