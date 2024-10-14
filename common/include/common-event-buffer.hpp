#pragma once

#include "common-constants.hpp"
#include "common-events.hpp"
#include <boost/container/static_vector.hpp>

namespace bc = boost::container;

namespace scuff {

using event_buffer = bc::static_vector<event, scuff::EVENT_PORT_SIZE>;

} // scuff