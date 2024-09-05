#pragma once

#include "common/shm.hpp"
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <format>
#include <memory>
#include <mutex>
#include <osbs/log.h>
#include <string_view>

namespace bip = boost::interprocess;

namespace sbox::shm {

struct model {
	tom::shm::group group;
	tom::shm::sandbox sandbox;
};

static std::unique_ptr<model> M_;

auto open(std::string_view group, std::string_view sandbox) -> bool {
	try {
		M_ = std::make_unique<model>();
		if (!tom::shm::open(&M_->group, group)) {
			throw std::runtime_error(std::format("Failed to open shared memory segment: '{}'", group.data()));
		}
		if (!tom::shm::open(&M_->sandbox, sandbox)) {
			throw std::runtime_error(std::format("Failed to open shared memory segment: '{}'", sandbox.data()));
		}
	}
	catch (const std::exception& err) {
		log_printf(err.what());
		return false;
	}
	return true;
}

auto destroy() -> void {
	M_.reset();
}

auto receive_input_messages(std::vector<tom::msg::in::msg>* msgs) -> void {
	auto lock = std::unique_lock(M_->sandbox.msgs_in->mutex);
	for (const auto& msg : M_->sandbox.msgs_in->list) {
		msgs->push_back(msg);
	}
	M_->sandbox.msgs_in->list.clear();
}

} // sbox::shm
