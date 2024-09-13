#pragma once

#include "data.hpp"

namespace scuff::sbox::clap {

[[nodiscard]] static
auto get_param_value_text(const sbox::app& app, id::device dev_id, scuff_param param_idx, double value) -> std::string {
	static constexpr auto BUFFER_SIZE = 50;
	const auto dev = app.working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.params) {
		const auto param = dev.params[param_idx];
		char buffer[BUFFER_SIZE];
		if (!dev.iface_plugin.params->value_to_text(dev.iface_plugin.plugin, param.id, value, buffer, BUFFER_SIZE)) {
			return std::to_string(value);
		}
		return buffer;
	}
	return std::to_string(value);
}

[[nodiscard]] static
auto load(sbox::app* app, id::device dev_id, const std::vector<std::byte>& state) -> bool {
	const auto dev = app->working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.state) {
		auto bytes = std::span{state};
		clap_istream_t is;
		is.ctx = (void*)(&bytes);
		is.read = [](const clap_istream_t *stream, void *buffer, uint64_t size) -> int64_t {
			const auto bytes = reinterpret_cast<std::span<std::byte>*>(stream->ctx);
			auto clap_bytes  = static_cast<std::byte*>(buffer);
			const auto read_size = std::min(size, static_cast<uint64_t>(bytes->size()));
			std::copy(bytes->data(), bytes->data() + read_size, clap_bytes);
			*bytes = bytes->subspan(read_size);
			return read_size;
		};
		return dev.iface_plugin.state->load(dev.iface_plugin.plugin, &is);
	}
	return false;
}

[[nodiscard]] static
auto save(sbox::app* app, id::device dev_id) -> std::vector<std::byte> {
	const auto dev = app->working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.state) {
		std::vector<std::byte> bytes;
		clap_ostream_t os;
		os.ctx = &bytes;
		os.write = [](const clap_ostream_t *stream, const void *buffer, uint64_t size) -> int64_t {
			auto &bytes = *static_cast<std::vector<std::byte>*>(stream->ctx);
			const auto clap_bytes = static_cast<const std::byte*>(buffer);
			std::copy(clap_bytes, clap_bytes + size, std::back_inserter(bytes));
			return size;
		};
		if (!dev.iface_plugin.state->save(dev.iface_plugin.plugin, &os)) {
			return {};
		}
		return bytes;
	}
	return {};
}

[[nodiscard]] static
auto set_sample_rate(const sbox::app& app, id::device dev_id, double sr) -> bool {
	const auto m   = *app.working_model.lock();
	const auto dev = m.clap_devices.at(dev_id);
	dev.iface_plugin.plugin->deactivate(dev.iface_plugin.plugin);
	return dev.iface_plugin.plugin->activate(dev.iface_plugin.plugin, sr, SCUFF_VECTOR_SIZE, SCUFF_VECTOR_SIZE);
}

} // scuff::sbox::clap