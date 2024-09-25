#pragma once

#include "client.hpp"
#include <memory>

namespace scuff {

struct device_raii {
	device_raii() = default;
	~device_raii() { if (id_) { scuff::erase(id_); } }
	device_raii(id::device id) : id_{id} { assert (id); }
	device_raii(device_raii&& other) noexcept : id_{other.id_} { other.id_ = {}; }
	device_raii& operator=(device_raii&& other) noexcept { id_ = other.id_; other.id_ = {}; return *this; }
	device_raii(const device_raii&) = delete;
	device_raii& operator=(const device_raii&) = delete;
	[[nodiscard]] auto id() const -> id::device { return id_; }
private:
	id::device id_;
};

using shared_device = std::shared_ptr<device_raii>;
using weak_device   = std::weak_ptr<device_raii>;

} // scuff