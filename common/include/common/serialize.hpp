#pragma once

#include <format>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace concepts {

template <typename T> concept is_pod     = std::is_pod_v<T>;
template <typename T> concept is_variant = requires { typename std::variant_size<T>::type; };

} // concepts

template <typename T> auto deserialize(std::span<const std::byte>* bytes, T*) -> void;
template <typename T> auto serialize(const T& value, std::vector<std::byte>* bytes) -> void;

template <typename T> requires std::is_pod_v<T> static
auto deserialize(std::span<const std::byte>* bytes, T* out) -> void {
	std::memcpy(out, bytes->data(), sizeof(T));
	*bytes = bytes->subspan(sizeof(T));
}

static
auto deserialize(std::span<const std::byte>* bytes, std::string* out) -> void {
	size_t size;
	deserialize(bytes, &size);
	const auto data = reinterpret_cast<const char*>(bytes->data());
	*out = std::string(data, size);
	*bytes = bytes->subspan(size * sizeof(char));
}

static
auto deserialize(std::span<const std::byte>* bytes, std::vector<std::byte>* out) -> void {
	size_t size;
	deserialize(bytes, &size);
	out->resize(size);
	std::memcpy(out->data(), bytes->data(), size);
	*bytes = bytes->subspan(size);
}

template <size_t Index, concepts::is_variant VariantT> static
auto deserialize(size_t type, std::span<const std::byte>* bytes, VariantT* out, std::string_view variant_desc) -> void {
	if (type == Index) {
		auto alt = std::variant_alternative_t<Index, VariantT>{};
		deserialize(bytes, &alt);
		*out = alt;
		return;
	}
	if constexpr (Index + 1 < std::variant_size_v<VariantT>) {
		deserialize<Index + 1>(type, bytes, out, variant_desc);
		return;
	}
	throw std::runtime_error{std::format("Invalid {} type", variant_desc)};
}

template <concepts::is_variant VariantT> static
auto deserialize(size_t type, std::span<const std::byte>* bytes, VariantT* out, std::string_view desc) -> void {
	return deserialize<0>(type, bytes, out, desc);
}

template <concepts::is_variant VariantT> static
auto deserialize(std::span<const std::byte>* bytes, VariantT* out, std::string_view desc) -> void {
	auto type = size_t{};
	deserialize(bytes, &type);
	deserialize(type, bytes, out, desc);
}

template <concepts::is_variant VariantT> static
auto deserialize(const std::vector<std::byte>& bytes, VariantT* out, std::string_view desc) -> void {
	auto span = std::span{bytes};
	deserialize(&span, out, desc);
}

template <concepts::is_pod T> static
auto serialize(const T& value, std::vector<std::byte>* bytes) -> void {
	const auto offset = bytes->size();
	bytes->resize(offset + sizeof(T));
	std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

static
auto serialize(std::string_view value, std::vector<std::byte>* bytes) -> void {
	serialize(value.size(), bytes);
	const auto offset = bytes->size();
	bytes->resize(offset + (value.size() * sizeof(char)));
	const auto dest = reinterpret_cast<char*>(bytes->data() + offset);
	std::copy(value.begin(), value.end(), dest);
}

static
auto serialize(const std::vector<std::byte>& value, std::vector<std::byte>* bytes) -> void {
	serialize(value.size(), bytes);
	const auto offset = bytes->size();
	bytes->resize(offset + value.size());
	std::memcpy(bytes->data() + offset, value.data(), value.size());
}

template <concepts::is_variant VariantT> static
auto serialize(const VariantT& value, std::vector<std::byte>* bytes) -> void {
	serialize(value.index(), bytes);
	std::visit([bytes](const auto& alt) { serialize(alt, bytes); }, value);
}

template <concepts::is_variant VariantT> [[nodiscard]] static
auto serialize(const VariantT& value) -> std::vector<std::byte> {
	auto bytes = std::vector<std::byte>{};
	serialize(value, &bytes);
	return bytes;
}
