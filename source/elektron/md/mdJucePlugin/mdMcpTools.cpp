#include "mdPluginProcessor.h"

#include "mdController.h"
#include "mdLcdText.h"
#include "mdMachinedrumHelp.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdmidiprotocol.h"
#include "mcpServerLib/mcpServer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace mdJucePlugin
{
	namespace
	{
		using Json = mcpServer::JsonValue;
		using namespace std::chrono_literals;

		int stepPage(const md::FrontPanel& _panel)
		{
			if(_panel.getStatusLed(md::FrontPanel::StatusLed::Page1)) return 1;
			if(_panel.getStatusLed(md::FrontPanel::StatusLed::Page2)) return 2;
			if(_panel.getStatusLed(md::FrontPanel::StatusLed::Page3)) return 3;
			if(_panel.getModeLed(md::FrontPanel::ModeLed::Page4)) return 4;
			return 0;
		}

		void writeU16(std::ofstream& _out, const uint16_t _value)
		{
			const char data[] = {static_cast<char>(_value), static_cast<char>(_value >> 8)};
			_out.write(data, 2);
		}

		void writeU32(std::ofstream& _out, const uint32_t _value)
		{
			writeU16(_out, static_cast<uint16_t>(_value));
			writeU16(_out, static_cast<uint16_t>(_value >> 16));
		}

		std::vector<uint8_t> decode7(const std::vector<uint8_t>& _message,
			const size_t _offset, const size_t _decodedSize)
		{
			std::vector<uint8_t> decoded;
			decoded.reserve(_decodedSize);
			size_t pos = _offset;
			while(decoded.size() < _decodedSize)
			{
				if(pos >= _message.size()) throw std::runtime_error("Truncated 7-bit data");
				const auto highBits = _message[pos++];
				for(unsigned bit = 0; bit < 7 && decoded.size() < _decodedSize; ++bit)
				{
					if(pos >= _message.size()) throw std::runtime_error("Truncated 7-bit data");
					decoded.push_back(static_cast<uint8_t>(_message[pos++] |
						(((highBits >> (6 - bit)) & 1u) << 7)));
				}
			}
			return decoded;
		}

		std::vector<uint8_t> encode7(const std::vector<uint8_t>& _data)
		{
			std::vector<uint8_t> encoded;
			encoded.reserve(_data.size() + (_data.size() + 6) / 7);
			for(size_t i = 0; i < _data.size(); i += 7)
			{
				const auto count = std::min<size_t>(7, _data.size() - i);
				uint8_t highBits = 0;
				for(size_t bit = 0; bit < count; ++bit)
					highBits |= static_cast<uint8_t>((_data[i + bit] >> 7) << (6 - bit));
				encoded.push_back(highBits);
				for(size_t bit = 0; bit < count; ++bit)
					encoded.push_back(_data[i + bit] & 0x7f);
			}
			return encoded;
		}

		// Checksum and length cover everything from the slot byte up to the checksum itself.
		void sealDump(std::vector<uint8_t>& _message)
		{
			unsigned checksum = 0;
			for(size_t i = 9; i + 5 < _message.size(); ++i)
				checksum += _message[i];
			const auto length = _message.size() - 10;
			_message[_message.size() - 5] = static_cast<uint8_t>((checksum >> 7) & 0x7f);
			_message[_message.size() - 4] = static_cast<uint8_t>(checksum & 0x7f);
			_message[_message.size() - 3] = static_cast<uint8_t>((length >> 7) & 0x7f);
			_message[_message.size() - 2] = static_cast<uint8_t>(length & 0x7f);
		}

		void putBe32(uint8_t* const _data, const uint32_t _value)
		{
			_data[0] = static_cast<uint8_t>(_value >> 24);
			_data[1] = static_cast<uint8_t>(_value >> 16);
			_data[2] = static_cast<uint8_t>(_value >> 8);
			_data[3] = static_cast<uint8_t>(_value);
		}

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

		uint32_t be32(const uint8_t* const _data)
		{
			return (static_cast<uint32_t>(_data[0]) << 24)
				| (static_cast<uint32_t>(_data[1]) << 16)
				| (static_cast<uint32_t>(_data[2]) << 8) | _data[3];
		}

		// Offsets within the extended pattern dump's second 7-bit block: trigs for steps
		// 33..64, then (after the accent/slide/swing words) lock values for steps 33..64.
		constexpr size_t g_patternExtra = 0xAC6;
		constexpr size_t g_patternExtraSize = 2316;
		constexpr size_t g_patternExtraLocks = 0x4C;

		Json patternDumpToJson(const std::vector<uint8_t>& _message, const int _slot)
		{
			if((_message.size() != 0xACB && _message.size() != 0x1522)
				|| _message[6] != 0x67 || _message[7] != 3 || _message[9] != _slot
				|| _message.back() != 0xf7)
				throw std::runtime_error("Unsupported Machinedrum pattern dump format");
			const auto trigs = decode7(_message, 0x0A, 64);
			const auto lockMask = decode7(_message, 0x54, 64);
			const auto lockValues = decode7(_message, 0xB7, 64 * 32);
			const bool extended = _message.size() == 0x1522;
			const auto extra = extended ? decode7(_message, g_patternExtra, g_patternExtraSize)
				: std::vector<uint8_t>{};
			auto result = Json::object();
			result.set("pattern", Json::fromInt(_slot));
			result.set("length", Json::fromInt(_message[0xB2]));
			result.set("scale", Json::fromInt(_message[0xB4]));
			result.set("tempoMultiplier", Json::fromInt(_message[0xB3]));
			result.set("kit", Json::fromInt(_message[0xB5]));
			auto tracks = Json::array();
			for(unsigned track = 0; track < 16; ++track)
			{
				auto item = Json::object();
				item.set("track", Json::fromInt(track));
				auto steps = Json::array();
				const auto first = be32(trigs.data() + track * 4);
				const auto second = extended ? be32(extra.data() + track * 4) : 0u;
				for(unsigned step = 0; step < 64; ++step)
					if((step < 32 ? first & (1u << step) : second & (1u << (step - 32))) != 0)
						steps.append(Json::fromInt(step + 1));
				item.set("trigs", steps);
				tracks.append(item);
			}
			result.set("tracks", tracks);
			auto locks = Json::array();
			unsigned row = 0;
			for(unsigned track = 0; track < 16; ++track)
			{
				const auto mask = be32(lockMask.data() + track * 4);
				for(unsigned parameter = 0; parameter < 24; ++parameter)
				{
					if((mask & (1u << parameter)) == 0) continue;
					if(row >= 64) throw std::runtime_error("Pattern lock rows exceed format limit");
					for(unsigned step = 0; step < (extended ? 64u : 32u); ++step)
					{
						const auto value = step < 32 ? lockValues[row * 32 + step]
							: extra[g_patternExtraLocks + row * 32 + step - 32];
						if(value == 0xff) continue;
						auto lock = Json::object();
						lock.set("track", Json::fromInt(track));
						lock.set("parameterIndex", Json::fromInt(parameter));
						lock.set("step", Json::fromInt(step + 1));
						lock.set("value", Json::fromInt(value));
						locks.append(lock);
					}
					++row;
				}
			}
			result.set("parameterLocks", locks);
			return result;
		}

		void applyPatternEdits(std::vector<uint8_t>& _message, const Json& _args)
		{
			const bool extended = _message.size() == 0x1522;
			auto trigs = decode7(_message, 0x0A, 64);
			auto lockMask = decode7(_message, 0x54, 64);
			auto lockValues = decode7(_message, 0xB7, 64 * 32);
			auto extra = extended ? decode7(_message, g_patternExtra, g_patternExtraSize)
				: std::vector<uint8_t>{};
			const int maxStep = extended ? 64 : 32;

			if(_args.hasProperty("length"))
				_message[0xB2] = static_cast<uint8_t>(intInRange(_args.get("length"), 2, maxStep, "length"));
			if(_args.hasProperty("scale"))
				_message[0xB4] = static_cast<uint8_t>(intInRange(_args.get("scale"), 0, extended ? 3 : 1, "scale"));
			if(_args.hasProperty("kit"))
				_message[0xB5] = static_cast<uint8_t>(intInRange(_args.get("kit"), 0, 63, "kit"));
			if(_message[0xB2] > 16 * (_message[0xB4] + 1))
				throw std::runtime_error("length exceeds the pattern scale; scale is 0..3 for 16/32/48/64 steps");

			// (track, parameter) -> 64 step values, 0xff where the step has no lock
			std::array<std::array<std::array<uint8_t, 64>, 24>, 16> locks;
			std::array<std::array<bool, 24>, 16> used{};
			for(auto& track : locks)
				for(auto& row : track)
					row.fill(0xff);
			unsigned row = 0;
			for(unsigned track = 0; track < 16; ++track)
			{
				const auto mask = be32(lockMask.data() + track * 4);
				for(unsigned parameter = 0; parameter < 24; ++parameter)
				{
					if((mask & (1u << parameter)) == 0) continue;
					if(row >= 64) throw std::runtime_error("Pattern lock rows exceed format limit");
					used[track][parameter] = true;
					for(unsigned step = 0; step < 32; ++step)
						locks[track][parameter][step] = lockValues[row * 32 + step];
					for(unsigned step = 32; extended && step < 64; ++step)
						locks[track][parameter][step] = extra[g_patternExtraLocks + row * 32 + step - 32];
					++row;
				}
			}

			const auto tracks = _args.get("tracks");
			if(!tracks.isVoid() && !tracks.isArray())
				throw std::runtime_error("tracks must be an array of {track, trigs}");
			for(int i = 0; i < tracks.getArraySize(); ++i)
			{
				const auto item = tracks.getArrayElement(i);
				const auto track = static_cast<unsigned>(intInRange(item.get("track"), 0, 15, "track"));
				const auto steps = item.get("trigs");
				if(!steps.isArray())
					throw std::runtime_error("Each tracks entry needs a trigs array of 1-based steps");
				uint64_t bits = 0;
				for(int j = 0; j < steps.getArraySize(); ++j)
					bits |= uint64_t{1} << (intInRange(steps.getArrayElement(j), 1, maxStep, "trig step") - 1);
				putBe32(trigs.data() + track * 4, static_cast<uint32_t>(bits));
				if(extended)
					putBe32(extra.data() + track * 4, static_cast<uint32_t>(bits >> 32));
				// The firmware keeps locks only on steps that have a trig.
				for(auto& values : locks[track])
					for(unsigned step = 0; step < 64; ++step)
						if((bits & (uint64_t{1} << step)) == 0)
							values[step] = 0xff;
			}

			if(_args.get("clearParameterLocks").getBool())
				for(auto& track : locks)
					for(auto& values : track)
						values.fill(0xff);
			const auto edits = _args.get("parameterLocks");
			if(!edits.isVoid() && !edits.isArray())
				throw std::runtime_error("parameterLocks must be an array of {track, parameterIndex, step, value}");
			for(int i = 0; i < edits.getArraySize(); ++i)
			{
				const auto item = edits.getArrayElement(i);
				const auto track = static_cast<unsigned>(intInRange(item.get("track"), 0, 15, "track"));
				const auto parameter = static_cast<unsigned>(
					intInRange(item.get("parameterIndex"), 0, 23, "parameterIndex"));
				const auto step = static_cast<unsigned>(intInRange(item.get("step"), 1, maxStep, "lock step") - 1);
				if(item.get("value").isVoid())
				{
					locks[track][parameter][step] = 0xff;
					continue;
				}
				const auto first = be32(trigs.data() + track * 4);
				const auto second = extended ? be32(extra.data() + track * 4) : 0u;
				if(((step < 32 ? first >> step : second >> (step - 32)) & 1u) == 0)
					throw std::runtime_error("Parameter lock on track " + std::to_string(track) + " step "
						+ std::to_string(step + 1) + " needs a trig on that step");
				locks[track][parameter][step] = static_cast<uint8_t>(
					intInRange(item.get("value"), 0, 127, "lock value"));
				used[track][parameter] = true;
			}

			// Unused lock rows are zero in the firmware's own dumps.
			std::fill(lockMask.begin(), lockMask.end(), uint8_t{0});
			std::fill(lockValues.begin(), lockValues.end(), uint8_t{0});
			if(extended)
				std::fill(extra.begin() + g_patternExtraLocks, extra.begin() + g_patternExtraLocks + 64 * 32,
					uint8_t{0});
			row = 0;
			for(unsigned track = 0; track < 16; ++track)
			{
				uint32_t mask = 0;
				for(unsigned parameter = 0; parameter < 24; ++parameter)
				{
					const auto& values = locks[track][parameter];
					if(!used[track][parameter]
						|| std::all_of(values.begin(), values.end(), [](const uint8_t _v) { return _v == 0xff; }))
						continue;
					if(row >= 64)
						throw std::runtime_error("A pattern holds at most 64 locked track parameters");
					mask |= 1u << parameter;
					std::copy_n(values.begin(), 32, lockValues.begin() + row * 32);
					if(extended)
						std::copy_n(values.begin() + 32, 32, extra.begin() + g_patternExtraLocks + row * 32);
					++row;
				}
				putBe32(lockMask.data() + track * 4, mask);
			}

			const auto store = [&](const size_t _offset, const std::vector<uint8_t>& _data)
			{
				const auto encoded = encode7(_data);
				std::copy(encoded.begin(), encoded.end(), _message.begin() + _offset);
			};
			store(0x0A, trigs);
			store(0x54, lockMask);
			store(0xB7, lockValues);
			if(extended)
				store(g_patternExtra, extra);
			sealDump(_message);
		}

		Json songDumpToJson(const std::vector<uint8_t>& _message, const int _slot)
		{
			if(_message.size() < 0x1F || _message[6] != 0x69 || _message[7] != 2
				|| _message[9] != _slot || _message.back() != 0xf7
				|| (_message.size() - 0x1F) % 12 != 0)
				throw std::runtime_error("Unsupported Machinedrum song dump format");
			const auto count = (_message.size() - 0x1F) / 12;
			if(count > 256) throw std::runtime_error("Song has more than 256 rows");
			auto result = Json::object();
			result.set("song", Json::fromInt(_slot));
			std::string name;
			for(size_t i = 0x0A; i < 0x1A && _message[i] != 0 && _message[i] != 0x7f; ++i)
				name.push_back(static_cast<char>(_message[i]));
			result.set("name", Json::fromString(name));
			auto rows = Json::array();
			for(size_t i = 0; i < count; ++i)
			{
				const auto row = decode7(_message, 0x1A + 12 * i, 10);
				auto item = Json::object();
				item.set("row", Json::fromInt(static_cast<int>(i)));
				item.set("pattern", Json::fromInt(static_cast<int8_t>(row[0])));
				item.set("kit", Json::fromInt(row[1]));
				item.set("loops", Json::fromInt(row[2]));
				item.set("jumpTo", Json::fromInt(row[3]));
				item.set("muteMask", Json::fromInt((row[4] << 8) | row[5]));
				const auto tempoRaw = (row[6] << 8) | row[7];
				// 0xffff: the row leaves the tempo alone. Otherwise BPM * 24, as in the set-tempo SysEx:
				// a row written as 138 * 30 played at 172.5 BPM on OS 1.63.
				item.set("tempo", tempoRaw == 0xffff ? Json::null() : Json::fromDouble(tempoRaw / 24.0));
				item.set("start", Json::fromInt(row[8]));
				item.set("end", Json::fromInt(row[9]));
				rows.append(item);
			}
			result.set("rows", rows);
			return result;
		}

		std::vector<uint8_t> buildSongDump(const std::vector<uint8_t>& _current, const Json& _args)
		{
			const auto rows = _args.get("rows");
			if(!rows.isArray() || rows.getArraySize() > 255)
				throw std::runtime_error("rows must be an array of at most 255 song rows");
			std::vector<uint8_t> message(_current.begin(), _current.begin() + 0x1A);
			if(_args.hasProperty("name"))
			{
				const auto name = _args.get("name").getString().toStdString();
				if(!_args.get("name").isString() || name.size() > 16)
					throw std::runtime_error("name must be a string of at most 16 characters");
				for(size_t i = 0; i < 16; ++i)
				{
					const auto c = i < name.size() ? static_cast<unsigned char>(name[i]) : 0u;
					if(c >= 0x7f || (c != 0 && c < 0x20))
						throw std::runtime_error("name must be printable ASCII");
					message[0x0A + i] = static_cast<uint8_t>(c);
				}
			}
			const auto append = [&](const std::vector<uint8_t>& _row)
			{
				const auto encoded = encode7(_row);
				message.insert(message.end(), encoded.begin(), encoded.end());
			};
			bool ended = false;
			for(int i = 0; i < rows.getArraySize() && !ended; ++i)
			{
				const auto item = rows.getArrayElement(i);
				const auto optional = [&](const char* const _key, const int _min, const int _max, const int _default)
				{
					return item.hasProperty(_key) ? intInRange(item.get(_key), _min, _max, _key) : _default;
				};
				const auto pattern = intInRange(item.get("pattern"), -3, 127, "pattern");
				const auto mutes = optional("muteMask", 0, 0xffff, 0);
				int tempo = 0xffff;
				const auto tempoValue = item.get("tempo");
				if(!tempoValue.isVoid())
				{
					if((!tempoValue.isInt() && !tempoValue.isDouble())
						|| tempoValue.getDouble() < 30.0 || tempoValue.getDouble() > 300.0)
						throw std::runtime_error("tempo must be 30..300 BPM, or null to leave it unchanged");
					tempo = static_cast<int>(std::lround(tempoValue.getDouble() * 24.0));
				}
				append({static_cast<uint8_t>(pattern), static_cast<uint8_t>(optional("kit", 0, 63, 0)),
					static_cast<uint8_t>(optional("loops", 0, 63, 0)),
					static_cast<uint8_t>(optional("jumpTo", 0, 255, 0)),
					static_cast<uint8_t>(mutes >> 8), static_cast<uint8_t>(mutes),
					static_cast<uint8_t>(tempo >> 8), static_cast<uint8_t>(tempo),
					static_cast<uint8_t>(optional("start", 0, 63, 0)),
					static_cast<uint8_t>(optional("end", 1, 64, 16))});
				ended = pattern == -1;
			}
			if(!ended)
				append({0xff, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 16});
			message.insert(message.end(), {0, 0, 0, 0, 0xf7});
			sealDump(message);
			return message;
		}

		void requireStepPage(AudioPluginAudioProcessor& _processor, const Json& _params)
		{
			const auto panel = _processor.getMcpPanelSnapshot();
			if(!panel.getModeLed(md::FrontPanel::ModeLed::Record))
				throw std::runtime_error("Enable grid recording on the Machinedrum before editing steps");
			if(stepPage(panel) != _params.get("page").getInt())
				throw std::runtime_error("Requested page is not selected; select the page on the machine first");
		}

		void selectTrackAndWait(AudioPluginAudioProcessor& _processor, const int _track)
		{
			if(_track < 0 || _track > 15)
				throw std::runtime_error("track must be 0..15");
			if(_processor.getCurrentTrack() == _track)
				return;
			if(!_processor.selectMcpTrack(_track))
				throw std::runtime_error("Could not send track selection");
			for(int i = 0; i < 40; ++i)
			{
				std::this_thread::sleep_for(25ms);
				if(_processor.getCurrentTrack() == _track)
					return;
			}
			throw std::runtime_error("Timed out waiting for the firmware to select the track");
		}

		Json panelState(AudioPluginAudioProcessor& _processor)
		{
			const auto panel = _processor.getMcpPanelSnapshot();
			auto result = Json::object();
			result.set("selectedTrack", Json::fromInt(_processor.getCurrentTrack()));
			const auto& controller = dynamic_cast<const Controller&>(_processor.getController());
			result.set("pattern", Json::fromInt(controller.getCurrentPattern()));
			result.set("kit", Json::fromInt(controller.getCurrentKitSlot()));
			result.set("stepPage", Json::fromInt(stepPage(panel)));
			result.set("recordMode", Json::fromBool(panel.getModeLed(md::FrontPanel::ModeLed::Record)));
			result.set("songMode", Json::fromBool(panel.getStatusLed(md::FrontPanel::StatusLed::Song)));
			result.set("patternMode", Json::fromBool(panel.getStatusLed(md::FrontPanel::StatusLed::Pattern)));
			result.set("classicMode", Json::fromBool(panel.getModeLed(md::FrontPanel::ModeLed::Classic)));
			result.set("extendedMode", Json::fromBool(panel.getModeLed(md::FrontPanel::ModeLed::Extended)));
			const auto transport = _processor.getObservedTransportState();
			result.set("transport", Json::fromString(transport < 0 ? "unknown" : transport ? "playing" : "stopped"));
			result.set("transportSource", Json::fromString("observed MIDI start/continue/stop"));
			auto steps = Json::array();
			auto trigs = Json::array();
			for(unsigned i = 0; i < 16; ++i)
			{
				const bool lit = panel.getStepLed(i);
				steps.append(Json::fromBool(lit));
				if(lit && panel.getModeLed(md::FrontPanel::ModeLed::Record))
					trigs.append(Json::fromInt(static_cast<int>(i + 1)));
			}
			result.set("stepLeds", steps);
			if(panel.getModeLed(md::FrontPanel::ModeLed::Record))
				result.set("trigsOnSelectedPage", trigs);

			// These hashes are already used by the editor's accessible help. Unknown screen
			// regions are left unknown instead of guessing text from the LCD bitmap.
			auto lcd = Json::object();
			uint16_t key = 0;
			if(const auto* machine = machinedrumHelp::machineForHash(
				lcdText::hash(panel, lcdText::g_mdMachineName), key))
				lcd.set("machine", Json::fromString(machine->lcdName));
			auto labels = Json::array();
			for(unsigned i = 0; i < 8; ++i)
			{
				const auto* label = machinedrumHelp::labelForHash(
					lcdText::inkHash(panel, lcdText::fieldLabel(i)));
				labels.append(label ? Json::fromString(label) : Json::null());
			}
			lcd.set("dataEntryLabels", labels);
			lcd.set("partial", Json::fromBool(true));
			result.set("lcdText", lcd);
			return result;
		}
	}

	md::FrontPanel AudioPluginAudioProcessor::getMcpPanelSnapshot()
	{
		return getPlugin().withDeviceLocked([](synthLib::Device* const _device)
		{
			const auto* device = dynamic_cast<const md::Device*>(_device);
			return device ? device->getFrontPanelSnapshot() : md::FrontPanel();
		});
	}

	bool AudioPluginAudioProcessor::sendMcpPanelControl(const md::PanelControl _control, const bool _down)
	{
		const auto packet = md::panelPacket(m_model, _control);
		if(!packet) return false;
		std::lock_guard panelLock(m_mcpPanelMutex);
		std::lock_guard inputLock(m_remotePanelInputMutex);
		if(!m_remotePanelInput || m_remotePanelInput->isRetired())
		{
			m_mcpPanelRows.reset();
			m_remotePanelInput = getPlugin().withDeviceLocked([](synthLib::Device* const _device)
			{
				auto* device = dynamic_cast<md::Device*>(_device);
				return device ? device->getPanelInput() : std::shared_ptr<md::PanelInput>();
			});
		}
		const auto combined = _down ? m_mcpPanelRows.press(*packet) : m_mcpPanelRows.release(*packet);
		return m_remotePanelInput && m_remotePanelInput->trySend(combined.row, combined.mask, md::PanelSource::Mcp);
	}

	bool AudioPluginAudioProcessor::sendMcpPanelEncoder(const md::PanelEncoder _encoder, const int _steps)
	{
		const auto command = md::panelEncoderCommand(m_model, _encoder);
		if(!command || _steps == 0 || std::abs(_steps) > 32) return false;
		std::lock_guard lock(m_remotePanelInputMutex);
		if(!m_remotePanelInput || m_remotePanelInput->isRetired())
			return false;
		for(int i = 0; i < std::abs(_steps); ++i)
			if(!m_remotePanelInput->trySend(*command, _steps > 0 ? 1 : 0xff, md::PanelSource::Mcp))
				return false;
		return true;
	}

	bool AudioPluginAudioProcessor::selectMcpTrack(const int _track)
	{
		if(m_model != md::MachineModel::Machinedrum || _track < 0 || _track > 15)
			return false;
		const auto body = md::midiProtocol::selectTrack(_track);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.push_back(0xf0);
		event.sysex.insert(event.sysex.end(), body.begin(), body.end());
		event.sysex.push_back(0xf7);
		getPlugin().addMidiEvent(event);
		return true;
	}

	std::vector<uint8_t> AudioPluginAudioProcessor::requestMcpDump(
		const uint8_t _requestId, const uint8_t _replyId, const int _slot)
	{
		if(m_model != md::MachineModel::Machinedrum || _slot < 0 || _slot > 127
			|| !((_requestId == 0x68 && _replyId == 0x67)
				|| (_requestId == 0x6a && _replyId == 0x69)))
			throw std::runtime_error("Invalid Machinedrum dump request");
		std::lock_guard requestLock(m_mcpDumpRequestMutex);
		m_mcpDumpReplyId.store(_replyId, std::memory_order_relaxed);
		m_mcpDumpSlot.store(static_cast<uint8_t>(_slot), std::memory_order_relaxed);
		m_mcpDumpState.store(1, std::memory_order_release);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			_requestId, static_cast<uint8_t>(_slot), 0xf7};
		// Through the processor, so the reply counts as editor-requested and stays off MIDI OUT.
		addMidiEvent(event);
		for(int attempt = 0; attempt < 800; ++attempt)
		{
			if(m_mcpDumpState.load(std::memory_order_acquire) == 3)
			{
				auto bytes = std::vector<uint8_t>(m_mcpDumpBuffer.begin(),
					m_mcpDumpBuffer.begin() + m_mcpDumpSize);
				m_mcpDumpState.store(0, std::memory_order_release);
				return bytes;
			}
			std::this_thread::sleep_for(10ms);
		}
		while(m_mcpDumpState.load(std::memory_order_acquire) == 2)
			std::this_thread::sleep_for(1ms);
		m_mcpDumpState.store(0, std::memory_order_release);
		throw std::runtime_error("Timed out waiting for Machinedrum dump; keep audio processing active");
	}

	std::vector<uint8_t> AudioPluginAudioProcessor::writeMcpDump(const std::vector<uint8_t>& _dump)
	{
		if(_dump.size() < 16 || (_dump[6] != 0x67 && _dump[6] != 0x69))
			throw std::runtime_error("Invalid Machinedrum dump");
		const auto reply = _dump[6];
		const auto request = static_cast<uint8_t>(reply == 0x67 ? 0x68 : 0x6a);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_dump.begin(), _dump.end());
		addMidiEvent(event);
		// The firmware stores a dump asynchronously; it is done when the slot reads back equal.
		std::vector<uint8_t> stored;
		for(int attempt = 0; attempt < 6; ++attempt)
		{
			std::this_thread::sleep_for(250ms);
			stored = requestMcpDump(request, reply, _dump[9]);
			if(stored == _dump)
				return stored;
		}
		throw std::runtime_error("The Machinedrum did not store the dump unchanged; "
			"check GLOBAL > SYSEX RECEIVE (position ORG) and read the slot back before retrying");
	}

	std::string AudioPluginAudioProcessor::startMcpAudioCapture(const double _seconds)
	{
		const auto rate = static_cast<uint32_t>(std::lround(getSampleRate()));
		if(_seconds <= 0 || _seconds > 120 || rate < 8000 || rate > 192000)
			throw std::runtime_error("Capture requires an active audio device and duration in (0, 120] seconds");
		auto capture = std::make_unique<McpAudioCapture>();
		capture->sampleRate = rate;
		capture->pcm.resize(static_cast<size_t>(std::ceil(_seconds * rate)) * 2);
		const auto folder = juce::File(juce::String::fromUTF8(getDataFolder().c_str())).getChildFile("mcp-captures");
		if(folder.createDirectory().failed())
			throw std::runtime_error("Could not create MCP capture folder");
		capture->path = folder.getNonexistentChildFile("machinedrum", ".wav").getFullPathName().toStdString();
		const auto path = capture->path;
		std::lock_guard lock(m_mcpAudioMutex);
		if(m_mcpAudioCapture)
			throw std::runtime_error("An MCP audio capture is already active");
		m_mcpAudioCapture = std::move(capture);
		return path;
	}

	std::string AudioPluginAudioProcessor::stopMcpAudioCapture()
	{
		std::unique_ptr<McpAudioCapture> capture;
		{
			std::lock_guard lock(m_mcpAudioMutex);
			capture = std::move(m_mcpAudioCapture);
		}
		if(!capture)
			throw std::runtime_error("No MCP audio capture is active");
		const auto bytes = capture->framesWritten * 4;
		std::ofstream out(capture->path, std::ios::binary);
		if(!out)
			throw std::runtime_error("Could not write MCP audio capture");
		out.write("RIFF", 4); writeU32(out, static_cast<uint32_t>(bytes + 36));
		out.write("WAVEfmt ", 8); writeU32(out, 16); writeU16(out, 1); writeU16(out, 2);
		writeU32(out, capture->sampleRate); writeU32(out, capture->sampleRate * 4);
		writeU16(out, 4); writeU16(out, 16);
		out.write("data", 4); writeU32(out, static_cast<uint32_t>(bytes));
		for(size_t i = 0; i < capture->framesWritten * 2; ++i)
			writeU16(out, static_cast<uint16_t>(capture->pcm[i]));
		if(!out)
			throw std::runtime_error("Could not finish MCP audio capture");
		return capture->path;
	}

	void AudioPluginAudioProcessor::registerMcpTools(mcpServer::McpServer& _server)
	{
		{
			mcpServer::ToolDef tool;
			tool.name = "get_pattern";
			tool.description = "Request and decode a Machinedrum pattern dump: length, kit, trigs on all tracks, and parameter locks";
			tool.inputSchema.addIntProperty("pattern", "Pattern slot 0..127 (A1=0, H16=127)", true, 0, 127);
			tool.inputSchema.addProperty("raw", "boolean", "Also return the undecoded SysEx dump as hex", false);
			tool.handler = [this](const Json& args)
			{
				const auto slot = args.get("pattern").getInt();
				if(!args.get("pattern").isInt() || slot < 0 || slot > 127)
					throw std::runtime_error("pattern must be 0..127");
				const auto dump = requestMcpDump(0x68, 0x67, slot);
				auto result = patternDumpToJson(dump, slot);
				if(args.get("raw").getBool())
					result.set("sysexHex", Json::fromString(toHex(dump)));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "get_song_rows";
			tool.description = "Request and decode a Machinedrum song dump into named rows, patterns, kits, mutes and tempos";
			tool.inputSchema.addIntProperty("song", "Song slot 0..31", true, 0, 31);
			tool.inputSchema.addProperty("raw", "boolean", "Also return the undecoded SysEx dump as hex", false);
			tool.handler = [this](const Json& args)
			{
				const auto slot = args.get("song").getInt();
				if(!args.get("song").isInt() || slot < 0 || slot > 31)
					throw std::runtime_error("song must be 0..31");
				const auto dump = requestMcpDump(0x6a, 0x69, slot);
				auto result = songDumpToJson(dump, slot);
				if(args.get("raw").getBool())
					result.set("sysexHex", Json::fromString(toHex(dump)));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "set_pattern";
			tool.description = "Edit a stored Machinedrum pattern by slot with absolute values: length, scale, kit, the full trig list of any track, and parameter locks. Only the given fields change. Works on any pattern, including the playing one, and needs no grid recording. Returns the pattern as read back from the unit.";
			tool.inputSchema.addIntProperty("pattern", "Pattern slot 0..127 (A1=0, H16=127)", true, 0, 127);
			tool.inputSchema.addIntProperty("length", "Pattern length in steps, 2..64", false, 2, 64);
			tool.inputSchema.addIntProperty("scale", "0..3 for 16/32/48/64 steps; must cover length", false, 0, 3);
			tool.inputSchema.addIntProperty("kit", "Kit slot 0..63 the pattern loads", false, 0, 63);
			tool.inputSchema.addProperty("tracks", "array", "Array of {track: 0..15, trigs: [1-based steps]}; replaces all trigs of each listed track and drops locks on steps that lose their trig", false);
			tool.inputSchema.addProperty("parameterLocks", "array", "Array of {track: 0..15, parameterIndex: 0..23 as in get_pattern, step: 1..64, value: 0..127 or null to remove}; the step needs a trig", false);
			tool.inputSchema.addProperty("clearParameterLocks", "boolean", "Remove every parameter lock before applying parameterLocks", false);
			tool.handler = [this](const Json& args)
			{
				const auto slot = intInRange(args.get("pattern"), 0, 127, "pattern");
				auto dump = requestMcpDump(0x68, 0x67, slot);
				patternDumpToJson(dump, slot);
				applyPatternEdits(dump, args);
				return patternDumpToJson(writeMcpDump(dump), slot);
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "set_song_rows";
			tool.description = "Replace the rows of a stored Machinedrum song (and optionally its name). Rows use the fields get_song_rows returns; the END row is appended when missing. Returns the song as read back from the unit.";
			tool.inputSchema.addIntProperty("song", "Song slot 0..31", true, 0, 31);
			tool.inputSchema.addProperty("name", "string", "Song name, up to 16 ASCII characters", false);
			tool.inputSchema.addProperty("rows", "array", "Array of {pattern: 0..127 (-1 END, -2 loop/jump/halt, -3 remark), kit, loops, jumpTo, muteMask: 16-bit track mask, tempo: BPM or null, start, end}; only pattern is required", true);
			tool.handler = [this](const Json& args)
			{
				const auto slot = intInRange(args.get("song"), 0, 31, "song");
				const auto current = requestMcpDump(0x6a, 0x69, slot);
				songDumpToJson(current, slot);
				return songDumpToJson(writeMcpDump(buildSongDump(current, args)), slot);
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "get_machinedrum_panel_state";
			tool.description = "Read selected track/pattern, grid page and LEDs, mode, and recognized LCD labels";
			tool.handler = [this](const Json&) { return panelState(*this); };
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "get_pattern_page";
			tool.description = "Read the selected 16-step page from the Machinedrum's step LEDs; stop playback and enable grid recording for reliable trig readback";
			tool.handler = [this](const Json&) { return panelState(*this); };
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "set_trigs";
			tool.description = "Set multiple trigs on the selected pattern page using firmware panel events. Stop playback and enable grid recording first; steps are 1..16 within page 1..4. Selects track and verifies LED readback.";
			tool.inputSchema.addIntProperty("track", "Track 0..15", true, 0, 15);
			tool.inputSchema.addIntProperty("page", "Currently selected page 1..4", true, 1, 4);
			tool.inputSchema.addProperty("steps", "array", "Array of {step: 1..16, on: boolean}", true);
			tool.handler = [this](const Json& args) -> Json
			{
				const auto entries = args.get("steps");
				if(!entries.isArray() || entries.getArraySize() < 1 || entries.getArraySize() > 16)
					throw std::runtime_error("steps must contain 1..16 entries");
				for(int i = 0; i < entries.getArraySize(); ++i)
				{
					const auto entry = entries.getArrayElement(i);
					if(!entry.isObject() || !entry.get("step").isInt() || !entry.get("on").isBool()
						|| entry.get("step").getInt() < 1 || entry.get("step").getInt() > 16)
						throw std::runtime_error("Each step must have step 1..16 and boolean on");
				}
				requireStepPage(*this, args);
				selectTrackAndWait(*this, args.get("track").getInt());
				std::this_thread::sleep_for(80ms);
				for(int i = 0; i < entries.getArraySize(); ++i)
				{
					const auto entry = entries.getArrayElement(i);
					const auto step = entry.get("step").getInt();
					const auto on = entry.get("on").getBool();
					if(getMcpPanelSnapshot().getStepLed(step - 1) == on)
						continue;
					const auto control = static_cast<md::PanelControl>(static_cast<int>(md::PanelControl::Trigger1) + step - 1);
					if(!sendMcpPanelControl(control, true))
						throw std::runtime_error("Could not press trig button");
					std::this_thread::sleep_for(40ms);
					if(!sendMcpPanelControl(control, false))
						throw std::runtime_error("Could not release trig button");
					bool confirmed = false;
					for(int retry = 0; retry < 20; ++retry)
					{
						std::this_thread::sleep_for(25ms);
						if(getMcpPanelSnapshot().getStepLed(step - 1) == on)
						{
							confirmed = true;
							break;
						}
					}
					if(!confirmed)
						throw std::runtime_error("Trig LED did not confirm step " + std::to_string(step));
				}
				return panelState(*this);
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "set_parameter_locks";
			tool.description = "Edit several parameter locks by holding each trig while turning a data encoder. Uses relative encoder steps, not absolute values; stop playback and enable grid recording first.";
			tool.inputSchema.addIntProperty("track", "Track 0..15", true, 0, 15);
			tool.inputSchema.addIntProperty("page", "Currently selected step page 1..4", true, 1, 4);
			tool.inputSchema.addProperty("locks", "array", "Array of {step: 1..16, encoder: 0..7 (A..H), delta: -32..32}", true);
			tool.handler = [this](const Json& args) -> Json
			{
				const auto entries = args.get("locks");
				if(!entries.isArray() || entries.getArraySize() < 1 || entries.getArraySize() > 64)
					throw std::runtime_error("locks must contain 1..64 entries");
				for(int i = 0; i < entries.getArraySize(); ++i)
				{
					const auto lock = entries.getArrayElement(i);
					if(!lock.get("step").isInt() || !lock.get("encoder").isInt() || !lock.get("delta").isInt()
						|| lock.get("step").getInt() < 1 || lock.get("step").getInt() > 16
						|| lock.get("encoder").getInt() < 0 || lock.get("encoder").getInt() > 7
						|| lock.get("delta").getInt() < -32 || lock.get("delta").getInt() > 32
						|| lock.get("delta").getInt() == 0)
						throw std::runtime_error("Each lock needs step 1..16, encoder 0..7, delta -32..-1 or 1..32");
				}
				requireStepPage(*this, args);
				selectTrackAndWait(*this, args.get("track").getInt());
				std::this_thread::sleep_for(80ms);
				for(int i = 0; i < entries.getArraySize(); ++i)
				{
					const auto lock = entries.getArrayElement(i);
					const auto control = static_cast<md::PanelControl>(
						static_cast<int>(md::PanelControl::Trigger1) + lock.get("step").getInt() - 1);
					const auto encoder = static_cast<md::PanelEncoder>(lock.get("encoder").getInt());
					if(!sendMcpPanelControl(control, true))
						throw std::runtime_error("Could not hold trig for parameter lock");
					std::this_thread::sleep_for(50ms);
					const bool sent = sendMcpPanelEncoder(encoder, lock.get("delta").getInt());
					std::this_thread::sleep_for(50ms);
					const bool released = sendMcpPanelControl(control, false);
					if(!sent || !released)
						throw std::runtime_error("Could not complete parameter lock edit");
				}
				auto result = Json::object();
				result.set("edited", Json::fromInt(entries.getArraySize()));
				result.set("track", Json::fromInt(args.get("track").getInt()));
				result.set("page", Json::fromInt(args.get("page").getInt()));
				return result;
			};
			_server.registerTool(std::move(tool));
		}
		{
			mcpServer::ToolDef tool;
			tool.name = "start_audio_capture";
			tool.description = "Start capturing the Machinedrum Main A/B output directly to a bounded stereo 16-bit WAV; call stop_audio_capture to finish the file";
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
			tool.description = "Finish direct Machinedrum audio capture and write the WAV file";
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
