#include "mdPluginProcessor.h"

#include "mdMonomachineDump.h"
#include "mcpServerLib/mcpServer.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace mdJucePlugin
{
	namespace
	{
		using Json = mcpServer::JsonValue;
		using namespace std::chrono_literals;
		using namespace mnmDump;

		int intInRange(const Json& _value, const int _min, const int _max, const char* const _what)
		{
			if(!_value.isInt() || _value.getInt() < _min || _value.getInt() > _max)
				throw std::runtime_error(std::string(_what) + " must be " + std::to_string(_min)
					+ ".." + std::to_string(_max));
			return _value.getInt();
		}

		std::string toHex(const std::vector<uint8_t>& _bytes)
		{
			static constexpr char digits[] = "0123456789ABCDEF";
			std::string hex;
			hex.reserve(_bytes.size() * 3);
			for(const auto byte : _bytes)
			{
				if(!hex.empty()) hex.push_back(' ');
				hex.push_back(digits[byte >> 4]);
				hex.push_back(digits[byte & 15]);
			}
			return hex;
		}

		Dump unpackChecked(const std::vector<uint8_t>& _message, const uint8_t _id, const size_t _size)
		{
			auto dump = unpack(_message);
			if(dump.id != _id || dump.data.size() != _size)
				throw std::runtime_error("Unexpected Monomachine dump (another OS version?)");
			return dump;
		}

		Json stepList(const uint64_t _word)
		{
			auto steps = Json::array();
			for(size_t step = 0; step < g_steps; ++step)
				if((_word >> step) & 1u)
					steps.append(Json::fromInt(static_cast<int>(step) + 1));
			return steps;
		}

		Json patternToJson(const std::vector<uint8_t>& _message)
		{
			const auto dump = unpackChecked(_message, g_patternId, g_patternSize);
			const auto& data = dump.data;
			auto result = Json::object();
			result.set("pattern", Json::fromInt(dump.position));
			result.set("length", Json::fromInt(data[g_length]));
			result.set("tempoMultiplier", Json::fromInt(data[g_tempoMultiplier]));
			result.set("kit", Json::fromInt(data[g_kit]));
			auto tracks = Json::array();
			for(size_t track = 0; track < g_tracks; ++track)
			{
				auto item = Json::object();
				item.set("track", Json::fromInt(static_cast<int>(track)));
				auto notes = Json::array();
				const auto amp = trigs(data, AmpTrigs, track);
				for(size_t step = 0; step < g_steps; ++step)
				{
					if(!((amp >> step) & 1u))
						continue;
					auto note = Json::object();
					note.set("step", Json::fromInt(static_cast<int>(step) + 1));
					note.set("note", Json::fromInt(data[g_notes + track * g_steps + step]));
					notes.append(note);
				}
				item.set("notes", notes);
				item.set("noteOffs", stepList(trigs(data, NoteOffs, track)));
				item.set("slides", stepList(trigs(data, Slides, track)));
				tracks.append(item);
			}
			result.set("tracks", tracks);
			const auto table = readLocks(data);
			auto locks = Json::array();
			for(size_t track = 0; track < g_tracks; ++track)
			{
				for(size_t parameter = 0; parameter < g_lockableParameters; ++parameter)
				{
					const auto& values = table[track][parameter];
					for(size_t step = 0; step < values.size(); ++step)
					{
						if(values[step] == 0xff)
							continue;
						auto lock = Json::object();
						lock.set("track", Json::fromInt(static_cast<int>(track)));
						lock.set("parameterIndex", Json::fromInt(static_cast<int>(parameter)));
						lock.set("step", Json::fromInt(static_cast<int>(step) + 1));
						lock.set("value", Json::fromInt(values[step]));
						locks.append(lock);
					}
				}
			}
			result.set("parameterLocks", locks);
			result.set("lockedParameters", Json::fromInt(data[g_lockRowCount]));
			return result;
		}

		uint64_t stepsToWord(const Json& _steps, const char* const _what)
		{
			if(!_steps.isArray())
				throw std::runtime_error(std::string(_what) + " must be an array of steps");
			uint64_t word = 0;
			for(int i = 0; i < _steps.getArraySize(); ++i)
				word |= uint64_t{1} << (intInRange(_steps.getArrayElement(i), 1, 64, _what) - 1);
			return word;
		}

		void applyPatternEdits(std::vector<uint8_t>& _data, const Json& _args)
		{
			if(_args.hasProperty("length"))
				_data[g_length] = static_cast<uint8_t>(intInRange(_args.get("length"), 2, 64, "length"));
			if(_args.hasProperty("kit"))
				_data[g_kit] = static_cast<uint8_t>(intInRange(_args.get("kit"), 0, 127, "kit"));

			auto table = readLocks(_data);
			std::array<bool, g_tracks> touched{};
			if(_args.get("clearParameterLocks").getBool())
			{
				table = LockTable{};
				touched.fill(true);
			}

			if(_args.hasProperty("tracks"))
			{
				const auto tracks = _args.get("tracks");
				if(!tracks.isArray())
					throw std::runtime_error("tracks must be an array");
				for(int i = 0; i < tracks.getArraySize(); ++i)
				{
					const auto item = tracks.getArrayElement(i);
					const auto track = static_cast<size_t>(intInRange(item.get("track"), 0, 5, "track"));
					touched[track] = true;
					if(item.hasProperty("notes"))
					{
						const auto notes = item.get("notes");
						if(!notes.isArray())
							throw std::runtime_error("notes must be an array of {step, note}");
						uint64_t word = 0;
						std::fill_n(_data.begin() + static_cast<std::ptrdiff_t>(g_notes + track * g_steps), g_steps, uint8_t{0xff});
						for(int n = 0; n < notes.getArraySize(); ++n)
						{
							const auto note = notes.getArrayElement(n);
							const auto step = static_cast<size_t>(intInRange(note.get("step"), 1, 64, "step")) - 1;
							_data[g_notes + track * g_steps + step] =
								static_cast<uint8_t>(intInRange(note.get("note"), 0, 127, "note"));
							word |= uint64_t{1} << step;
						}
						// A note trigs the amp, the filter envelope and the LFOs, as a trig placed on the panel does.
						for(const auto group : {AmpTrigs, FilterTrigs, LfoTrigs})
							setTrigs(_data, group, track, word);
					}
					if(item.hasProperty("noteOffs"))
						setTrigs(_data, NoteOffs, track, stepsToWord(item.get("noteOffs"), "noteOffs"));
					if(item.hasProperty("slides"))
						setTrigs(_data, Slides, track, stepsToWord(item.get("slides"), "slides"));
				}
			}

			if(_args.hasProperty("parameterLocks"))
			{
				const auto locks = _args.get("parameterLocks");
				if(!locks.isArray())
					throw std::runtime_error("parameterLocks must be an array");
				for(int i = 0; i < locks.getArraySize(); ++i)
				{
					const auto item = locks.getArrayElement(i);
					const auto track = static_cast<size_t>(intInRange(item.get("track"), 0, 5, "track"));
					const auto parameter = static_cast<size_t>(intInRange(item.get("parameterIndex"), 0, 63, "parameterIndex"));
					const auto step = static_cast<size_t>(intInRange(item.get("step"), 1, 64, "step")) - 1;
					const auto value = item.get("value");
					auto& row = table[track][parameter];
					if(row.empty())
						row.assign(g_steps, 0xff);
					row[step] = value.isVoid() ? uint8_t{0xff} : static_cast<uint8_t>(intInRange(value, 0, 127, "value"));
					touched[track] = true;
				}
			}

			for(size_t track = 0; track < g_tracks; ++track)
			{
				if(!touched[track])
					continue;
				auto word = trigs(_data, AmpTrigs, track);
				for(const auto& row : table[track])
					for(size_t step = 0; step < row.size(); ++step)
						if(row[step] != 0xff)
							word |= uint64_t{1} << step;
				setTrigs(_data, LockSteps, track, word);
			}
			writeLocks(_data, table);
			std::fill_n(_data.begin() + static_cast<std::ptrdiff_t>(g_unusedFlags), 4, uint8_t{0});
		}

		Json kitToJson(const std::vector<uint8_t>& _message)
		{
			const auto dump = unpackChecked(_message, g_kitId, g_kitSize);
			const auto& data = dump.data;
			auto result = Json::object();
			result.set("kit", Json::fromInt(dump.position));
			std::string name;
			for(size_t i = 0; i < g_kitNameLength && data[g_kitName + i] != 0 && data[g_kitName + i] < 0x80; ++i)
				name.push_back(static_cast<char>(data[g_kitName + i]));
			result.set("name", Json::fromString(name));
			auto tracks = Json::array();
			for(size_t track = 0; track < g_tracks; ++track)
			{
				auto item = Json::object();
				item.set("track", Json::fromInt(static_cast<int>(track)));
				item.set("machine", Json::fromInt(data[g_kitMachines + track]));
				item.set("level", Json::fromInt(data[g_kitLevels + track]));
				auto parameters = Json::array();
				for(size_t i = 0; i < g_kitParametersPerTrack; ++i)
					parameters.append(Json::fromInt(data[g_kitParameters + track * g_kitParametersPerTrack + i]));
				item.set("parameters", parameters);
				tracks.append(item);
			}
			result.set("tracks", tracks);
			return result;
		}
	}

	std::vector<uint8_t> AudioPluginAudioProcessor::writeMonomachineMcpDump(const std::vector<uint8_t>& _dump)
	{
		if(m_model != md::MachineModel::Monomachine || _dump.size() < 16 || _dump[6] != g_patternId)
			throw std::runtime_error("Invalid Monomachine dump");
		const auto tap = [this](const md::PanelControl _control)
		{
			if(!sendMcpPanelControl(_control, true))
				throw std::runtime_error("The Monomachine panel is not available");
			std::this_thread::sleep_for(60ms);
			sendMcpPanelControl(_control, false);
			std::this_thread::sleep_for(220ms);
		};
		const auto leaveMenus = [&tap]
		{
			for(int i = 0; i < 5; ++i)
				tap(md::PanelControl::Exit);
		};

		// The Monomachine stores a dump only while GLOBAL > FILE > SYSEX RECEIVE shows WAITING. FUNCTION + KIT
		// opens GLOBAL; the menus remember their cursor, so walk up to the top before counting down.
		leaveMenus();
		sendMcpPanelControl(md::PanelControl::Function, true);
		std::this_thread::sleep_for(80ms);
		tap(md::PanelControl::Kit);
		sendMcpPanelControl(md::PanelControl::Function, false);
		std::this_thread::sleep_for(220ms);
		using C = md::PanelControl;
		for(const auto control : {C::Enter, C::Up, C::Up, C::Up, C::Up, C::Down, C::Down, C::Right,
			C::Up, C::Up, C::Up, C::Down, C::Enter, C::Enter})
			tap(control);
		std::this_thread::sleep_for(500ms);

		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_dump.begin(), _dump.end());
		addMidiEvent(event);
		std::this_thread::sleep_for(900ms);
		leaveMenus();

		const auto stored = requestMcpDump(g_patternRequestId, g_patternId, _dump[9]);
		if(unpack(stored).data != unpack(_dump).data)
			throw std::runtime_error("The Monomachine did not store the pattern. It takes dumps only while "
				"GLOBAL > FILE > SYSEX RECEIVE is waiting; this tool opens that screen from the panel, which "
				"fails if a key is held or another menu will not close");
		return stored;
	}

	void AudioPluginAudioProcessor::registerMonomachineMcpTools(mcpServer::McpServer& _server)
	{
		{
			mcpServer::ToolDef tool;
			tool.name = "get_pattern";
			tool.description = "Request and decode a Monomachine pattern dump: length, kit, the notes, note-offs and slides of the six tracks, and parameter locks";
			tool.inputSchema.addIntProperty("pattern", "Pattern slot 0..127 (A01=0, H16=127)", true, 0, 127);
			tool.inputSchema.addProperty("raw", "boolean", "Also return the undecoded SysEx dump as hex", false);
			tool.handler = [this](const Json& args)
			{
				const auto dump = requestMcpDump(g_patternRequestId, g_patternId, intInRange(args.get("pattern"), 0, 127, "pattern"));
				auto result = patternToJson(dump);
				if(args.get("raw").getBool())
					result.set("sysexHex", Json::fromString(toHex(dump)));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "set_pattern";
			tool.description = "Edit a stored Monomachine pattern by slot with absolute values: length, kit, the notes of any track, and parameter locks. Only the given fields change. "
				"The unit stores a dump only from its GLOBAL > FILE > SYSEX RECEIVE screen, so this drives the panel there and back (about 8 seconds, and it leaves any open menu). "
				"Works while the sequencer plays. Returns the pattern as read back from the unit.";
			tool.inputSchema.addIntProperty("pattern", "Pattern slot 0..127 (A01=0, H16=127)", true, 0, 127);
			tool.inputSchema.addIntProperty("length", "Pattern length in steps, 2..64", false, 2, 64);
			tool.inputSchema.addIntProperty("kit", "Kit slot 0..127 the pattern loads", false, 0, 127);
			tool.inputSchema.addProperty("tracks", "array", "Array of {track: 0..5, notes: [{step: 1..64, note: 0..127 (MIDI note number)}], noteOffs: [steps], slides: [steps]}; each list given replaces that list for the track", false);
			tool.inputSchema.addProperty("parameterLocks", "array", "Array of {track: 0..5, parameterIndex: 0..63 (page * 8 + knob: 0 synthesis, 8 amp, 16 filter, 24 effects, 32/40/48 LFO 1-3), step: 1..64, value: 0..127 or null to remove}; a step needs no note to carry a lock. At most 62 track/parameter pairs per pattern", false);
			tool.inputSchema.addProperty("clearParameterLocks", "boolean", "Remove every parameter lock before applying parameterLocks", false);
			tool.handler = [this](const Json& args)
			{
				const auto slot = intInRange(args.get("pattern"), 0, 127, "pattern");
				auto dump = unpackChecked(requestMcpDump(g_patternRequestId, g_patternId, slot), g_patternId, g_patternSize);
				applyPatternEdits(dump.data, args);
				return patternToJson(writeMonomachineMcpDump(pack(dump)));
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "get_kit";
			tool.description = "Request and decode a stored Monomachine kit: name and, per track, machine number, level and the 72 parameter values (synthesis, amp, filter, effects, LFO 1-3, 8 each, then 16 more). To change a kit, load it, use set_parameter and the assign-machine SysEx, and save it";
			tool.inputSchema.addIntProperty("kit", "Kit slot 0..127", true, 0, 127);
			tool.inputSchema.addProperty("raw", "boolean", "Also return the undecoded SysEx dump as hex", false);
			tool.handler = [this](const Json& args)
			{
				const auto dump = requestMcpDump(g_kitRequestId, g_kitId, intInRange(args.get("kit"), 0, 127, "kit"));
				auto result = kitToJson(dump);
				if(args.get("raw").getBool())
					result.set("sysexHex", Json::fromString(toHex(dump)));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "start_audio_capture";
			tool.description = "Start capturing the Monomachine main output directly to a bounded stereo 16-bit WAV; call stop_audio_capture to finish the file";
			tool.inputSchema.addProperty("seconds", "number", "Maximum duration, up to 120 seconds", true);
			tool.handler = [this](const Json& args)
			{
				auto result = Json::object();
				result.set("path", Json::fromString(startMcpAudioCapture(args.get("seconds").getDouble())));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "stop_audio_capture";
			tool.description = "Finish direct Monomachine audio capture and write the WAV file";
			tool.handler = [this](const Json&)
			{
				auto result = Json::object();
				result.set("path", Json::fromString(stopMcpAudioCapture()));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
	}
}
