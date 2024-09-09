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
	scuff::shm::group group;
	scuff::shm::sandbox sandbox;
};

static std::unique_ptr<model> M_;

auto open(std::string_view group, std::string_view sandbox) -> bool {
	try {
		M_ = std::make_unique<model>();
		M_->group   = scuff::shm::group{bip::open_only, group};
		M_->sandbox = scuff::shm::sandbox{bip::open_only, sandbox};
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

auto receive_input_messages(std::vector<scuff::msg::in::msg>* msgs) -> void {
	M_->sandbox.data->msgs_in.take(msgs);
}

} // sbox::shm
