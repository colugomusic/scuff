#pragma once

#include "common-param-info.hpp"
#include "common-serialize.hpp"

template <> inline
auto deserialize<scuff::client_param_info>(std::span<const std::byte>* bytes, scuff::client_param_info* e) -> void {
	deserialize(bytes, &e->id.value);
	deserialize(bytes, &e->flags);
	deserialize(bytes, &e->name);
	deserialize(bytes, &e->min_value);
	deserialize(bytes, &e->max_value);
	deserialize(bytes, &e->default_value);
}

template <> inline
auto serialize<scuff::client_param_info>(const scuff::client_param_info& info, std::vector<std::byte>* bytes) -> void {
	serialize(info.id.value, bytes);
	serialize(info.flags, bytes);
	serialize(info.name, bytes);
	serialize(info.min_value, bytes);
	serialize(info.max_value, bytes);
	serialize(info.default_value, bytes);
}
