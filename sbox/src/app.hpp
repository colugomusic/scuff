#pragma once

#include "options.hpp"
#include <nappgui.h>

namespace sbox {

struct app {
	Panel* panel;
	View* view;
	Window* window;
	sbox::options options;
};

} // sbox
