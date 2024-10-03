#pragma once

#include "data.hpp"
#include "report-types.hpp"

namespace scuff {
namespace report {

static
auto send(const report::msg::general& msg) -> void {
	DATA_->reporter.lock()->push_back(msg);
}

static
auto send(const scuff::group& group, const report::msg::group& msg) -> void {
	group.services->reporter.lock()->push_back(msg);
}

static
auto send(const scuff::sandbox& sbox, const report::msg::group& msg) -> void {
	if (const auto group = DATA_->model.read().groups.find(sbox.group)) {
		send(*group, msg);
	}
}

} // report
} // scuff