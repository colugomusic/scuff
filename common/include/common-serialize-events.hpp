#pragma once

#include "common-events.hpp"
#include "common-serialize.hpp"

static
auto deserialize(std::span<const std::byte>* bytes, scuff::event* e) -> void {
	deserialize(bytes, e, "scuff::event");
}
