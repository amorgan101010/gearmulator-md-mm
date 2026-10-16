/*
 * Firmware-backed state-snapshot integration test. It uses only the public device ABI for the
 * running machine; mdLib's public MIDI encoders/parsers describe the firmware-visible operation.
 */
#include "gmDeviceAbi/gm_device.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mdsysexautomation.h"
#include "mdLib/mdtypes.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	constexpr uint32_t g_block = 512;
	constexpr uint32_t g_rate = 44100;
	constexpr size_t g_snapshotCapacity = 16u << 20;

	void require(const bool _ok, const std::string& _message)
	{
		if(!_ok)
			throw std::runtime_error(_message);
	}

	std::vector<uint8_t> readRom(const char* const _path)
	{
		std::ifstream input(_path, std::ios::binary);
		require(input.good(), std::string("cannot open ROM: ") + _path);
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}

	struct Machine
	{
		const gm_device_api* api;
		gm_device* device = nullptr;
		std::array<std::array<float, g_block>, 6> audio{};
		std::array<float*, 6> outputs{};
		std::vector<gm_midi_out_event> midiOut = std::vector<gm_midi_out_event>(4096);
		std::vector<uint8_t> midiData = std::vector<uint8_t>(1u << 20);

		Machine(const gm_device_api* _api, const uint32_t _model,
			const std::vector<uint8_t>& _rom)
			: api(_api)
		{
			for(size_t i = 0; i < outputs.size(); ++i)
				outputs[i] = audio[i].data();
			gm_device_create_info info{};
			info.struct_size = sizeof(info);
			info.model = _model;
			info.rom = _rom.data();
			info.rom_size = _rom.size();
			require(api->create(&info, &device) == GM_OK && device,
				"device creation failed");
		}

		~Machine()
		{
			if(device)
				api->destroy(device);
		}

		std::vector<std::vector<uint8_t>> renderBlock()
		{
			gm_render_io io{};
			io.struct_size = sizeof(io);
			io.frames = g_block;
			io.outputs = outputs.data();
			io.midi_out = midiOut.data();
			io.midi_out_capacity = static_cast<uint32_t>(midiOut.size());
			io.midi_out_data = midiData.data();
			io.midi_out_data_capacity = static_cast<uint32_t>(midiData.size());
			require(api->render(device, &io) == GM_OK,
				std::string("render failed: ") + api->last_error(device));
			std::vector<std::vector<uint8_t>> messages;
			for(uint32_t i = 0; i < io.midi_out_count; ++i)
			{
				const auto& event = midiOut[i];
				if(event.data_offset > io.midi_out_data_size
					|| event.size > io.midi_out_data_size - event.data_offset)
					continue;
				const auto first = midiData.begin() + event.data_offset;
				if(event.size && *first == 0xf0)
					messages.emplace_back(first, first + event.size);
			}
			return messages;
		}

		void renderSeconds(const double _seconds)
		{
			const auto blocks = static_cast<uint32_t>(_seconds * g_rate / g_block);
			for(uint32_t i = 0; i < blocks; ++i)
				(void)renderBlock();
		}

		void send(const std::vector<uint8_t>& _message)
		{
			require(api->send_midi(device, _message.data(),
				static_cast<uint32_t>(_message.size())) == GM_OK,
				"firmware rejected MIDI/SysEx input");
		}

		void send(const md::automation::ControlChange& _message)
		{
			require(api->send_midi(device, _message.data(),
				static_cast<uint32_t>(_message.size())) == GM_OK,
				"firmware rejected parameter CC");
		}

		template<typename Predicate>
		std::vector<uint8_t> request(const std::vector<uint8_t>& _message,
			Predicate _matches, const char* _what)
		{
			send(_message);
			for(unsigned attempt = 0; attempt < 600; ++attempt)
				for(auto& reply : renderBlock())
					if(_matches(reply))
						return reply;
			throw std::runtime_error(_what);
		}

		uint8_t status(const md::MachineModel _model,
			const md::automation::sysex::StatusParameter _parameter)
		{
			using namespace md::automation::sysex;
			const auto response = request(statusRequest(_model, _parameter),
				[&](const auto& bytes) {
					return parseStatusResponse(_model, MessageView(bytes))
						.has_value();
				}, "firmware status request timed out");
			const auto parsed = parseStatusResponse(_model, MessageView(response));
			require(parsed && parsed->parameter == _parameter,
				"firmware returned the wrong status parameter");
			return parsed->value;
		}

		uint8_t baseChannel(const md::MachineModel _model)
		{
			using namespace md::automation::sysex;
			const auto slot = status(_model, StatusParameter::Global);
			const auto response = request(globalRequest(_model, slot),
				[&](const auto& bytes) {
					return parseGlobalDump(_model, MessageView(bytes)).has_value();
				}, "firmware global dump request timed out");
			const auto parsed = parseGlobalDump(_model, MessageView(response));
			require(parsed && parsed->slot == slot,
				"firmware returned the wrong global slot");
			return parsed->baseChannel;
		}

		std::vector<uint8_t> getState()
		{
			size_t size = 0;
			require(api->get_state(device, nullptr, 0, &size) == GM_OK && size,
				"get_state size query failed");
			std::vector<uint8_t> bytes(size);
			require(api->get_state(device, bytes.data(), bytes.size(), &size) == GM_OK,
				"get_state copy failed");
			bytes.resize(size);
			return bytes;
		}

		struct Snapshot
		{
			std::vector<uint8_t> bytes;
			uint64_t generation = 0;
		};

		Snapshot snapshot()
		{
			size_t size = 0;
			uint64_t generation = 0;
			gm_status status = api->read_state_snapshot(device, nullptr, 0,
				&size, &generation);
			require(status == GM_OK || status == GM_ERROR_REJECTED,
				"snapshot request failed");
			(void)renderBlock();
			status = api->read_state_snapshot(device, nullptr, 0, &size, &generation);
			require(status == GM_OK && size && size <= g_snapshotCapacity,
				"snapshot was not published after render");
			Snapshot result;
			result.bytes.resize(size);
			status = api->read_state_snapshot(device, result.bytes.data(),
				result.bytes.size(), &size, &generation);
			require(status == GM_OK && size == result.bytes.size(),
				"snapshot copy failed");
			result.generation = generation;
			return result;
		}
	};

	void exercise(const gm_device_api* _api, const uint32_t _model,
		const md::MachineModel _machineModel, const char* _name,
		const std::vector<uint8_t>& _rom)
	{
		Machine machine(_api, _model, _rom);
		for(uint32_t i = 0; i < g_rate * 20 / 256; ++i)
			require(_api->advance(machine.device, 256) == GM_OK,
				"firmware boot advance failed");
		require(_api->prepare(machine.device, g_block) == GM_OK,
			"prepare failed");

		const auto initial = machine.snapshot();
		require(initial.bytes == machine.getState(),
			"first ABI snapshot differs from get_state");
		machine.renderSeconds(2.0);
		const auto idle = machine.snapshot();
		require(idle.generation == initial.generation
			&& idle.bytes == initial.bytes,
			"unchanged playback changed persistent snapshot generation or bytes");

		const auto kit = machine.status(_machineModel,
			md::automation::sysex::StatusParameter::Kit);
		const auto channel = machine.baseChannel(_machineModel);
		const auto parameter = _machineModel == md::MachineModel::Machinedrum
			? md::automation::ParameterChange{md::automation::machinedrum::Level, 0, 0, 97}
			: md::automation::ParameterChange{md::automation::monomachine::Level, 0, 0, 97};
		const auto cc = md::automation::encodeParameterChange(
			_machineModel, parameter, channel);
		require(cc.has_value(), "could not encode public parameter CC");
		machine.send(*cc);
		machine.renderSeconds(1.0);
		machine.send(md::automation::sysex::kitSave(_machineModel, kit));
		machine.renderSeconds(3.0);

		const auto changedKit = machine.request(
			md::automation::sysex::kitRequest(_machineModel, kit),
			[&](const auto& bytes) {
				return md::automation::sysex::parseKitDump(_machineModel,
					md::automation::sysex::MessageView(bytes)).has_value();
			}, "firmware kit dump request timed out");
		const auto parsedKit = md::automation::sysex::parseKitDump(_machineModel,
			md::automation::sysex::MessageView(changedKit));
		require(parsedKit && parsedKit->slot == kit
			&& std::find(parsedKit->parameters.begin(), parsedKit->parameters.end(), parameter)
				!= parsedKit->parameters.end(),
			"firmware did not retain the edited kit parameter");

		const auto changed = machine.snapshot();
		require(changed.generation > idle.generation,
			"firmware-visible kit edit did not advance snapshot generation");
		require(changed.bytes != idle.bytes,
			"firmware-visible kit edit did not change persistent state bytes");
		require(changed.bytes == machine.getState(),
			"post-edit ABI snapshot differs from get_state");

		std::cout << _name << ": firmware kit save, stable idle generation, snapshot generation "
			<< idle.generation << " -> " << changed.generation << ", "
			<< changed.bytes.size() << " state bytes\n";
	}
}

int main()
{
	const auto* mdPath = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const auto* mmPath = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(!mdPath || !*mdPath || !mmPath || !*mmPath)
	{
		std::cout << "gmMdSnapshotFirmwareTest: SKIP (set both MD and MM firmware paths)\n";
		return 77;
	}
	try
	{
		const gm_device_api* api = nullptr;
		require(gm_get_device_api(GM_DEVICE_ABI_VERSION, &api) == GM_OK && api,
			"ABI negotiation failed");
		require((api->capabilities & (GM_CAP_STATUS | GM_CAP_CONTROLS))
			== (GM_CAP_STATUS | GM_CAP_CONTROLS)
			&& api->read_state_snapshot,
			"minor-5 status/snapshot capabilities are not advertised");
		exercise(api, GM_MODEL_MACHINEDRUM, md::MachineModel::Machinedrum,
			"Machinedrum", readRom(mdPath));
		exercise(api, GM_MODEL_MONOMACHINE, md::MachineModel::Monomachine,
			"Monomachine", readRom(mmPath));
		std::cout << "gmMdSnapshotFirmwareTest: PASS\n";
		return EXIT_SUCCESS;
	}
	catch(const std::exception& error)
	{
		std::cerr << "gmMdSnapshotFirmwareTest: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
