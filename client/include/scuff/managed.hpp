#pragma once

#include "client.hpp"
#include <memory>

namespace scuff {

auto ref(id::device id) -> void;
auto ref(id::group id) -> void;
auto ref(id::sandbox id) -> void;
auto unref(id::device id) -> void;
auto unref(id::group id) -> void;
auto unref(id::sandbox id) -> void;

template <typename RefCounter>
struct ref_counted {
	ref_counted() = default;
	ref_counted(RefCounter counter)
		: counter_{counter}
	{
		counter_.ref_();
	}
	~ref_counted() {
		counter_.unref_();
	}
	ref_counted(const ref_counted& other) {
		counter_ = other.counter_;
		counter_.ref_();
	}
	ref_counted(ref_counted&& other) noexcept {
		counter_ = other.counter_;
		other.counter_ = {};
	}
	ref_counted& operator=(const ref_counted& other) {
		if (this != &other) {
			counter_.unref_();
			counter_ = other.counter_;
			counter_.ref_();
		}
		return *this;
	}
	ref_counted& operator=(ref_counted&& other) noexcept {
		if (this != &other) {
			counter_.unref_();
			counter_ = other.counter_;
			other.counter_ = {};
		}
		return *this;
	}
	auto operator==(const ref_counted& rhs) const { return counter_ == rhs.counter_; }
	[[nodiscard]] auto get_counter() const -> RefCounter { return counter_; }
private:
	RefCounter counter_;
};

template <typename ID>
struct ref_counter {
	ref_counter() = default;
	explicit ref_counter(ID id) : id_{id} {}
	auto ref_() const -> void { if (id_) { ref(id_); } }
	auto unref_() const -> void { if (id_) { unref(id_); } }
	[[nodiscard]] auto id() const -> ID { return id_; }
	explicit operator bool() const { return bool(id_); }
	auto operator==(const ref_counter<ID>& rhs) const { return id_ == rhs.id_; }
private:
	ID id_;
};

template <typename ID, typename RefCounter = ref_counter<ID>>
struct managed_t {
	managed_t() = default;
	explicit managed_t(ID id) : deleter_{RefCounter{id}} {}
	[[nodiscard]] auto id() const -> ID { return deleter_.get_counter().id(); }
	explicit operator bool() const { return bool(deleter_.get_counter()); }
	auto operator==(const managed_t& rhs) const { return deleter_ == rhs.deleter_; }
private:
	ref_counted<RefCounter> deleter_;
};

using managed_device  = managed_t<id::device>;
using managed_group   = managed_t<id::group>;
using managed_sandbox = managed_t<id::sandbox>;

} // scuff