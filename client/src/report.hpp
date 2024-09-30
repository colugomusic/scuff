#pragma once

#include "data.hpp"
#include "report_types.hpp"

namespace scuff {
namespace report {

static
auto send(const report::msg::general& msg) -> void {
	DATA_->reporter.lock()->push_back(msg);
}

static
auto send(const scuff::group& group, const report::msg::group& msg) -> void {
	if (group.flags & scuff::group_flag_no_reporting) {
		return;
	}
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