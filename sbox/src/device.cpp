#include "device.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sbox::device {

struct flags {
	enum e {
		has_gui                    = 1 << 0,
		has_params                 = 1 << 1,
		was_created_successfully   = 1 << 2,
		nappgui_window_was_resized = 1 << 3,
	};
	int value = 0;
};

struct name { std::string value; };

struct record {
	device::flags flags;
	device::name name;
};

struct model {
	std::vector<record> records;
};

static std::unique_ptr<model> M_;

auto create() -> void {
	M_ = std::make_unique<model>();
}

auto destroy() -> void {
	M_.reset();
}

} // sbox::device
