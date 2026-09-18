// Golden output regression test for the Machinedrum and Monomachine emulation.
//
// The emulation is deterministic: the same firmware, driven the same way, produces the same
// samples. This test boots each machine, plays every machine type through it, and compares hashes
// of the DSP's output words, the MIDI output and the LCD against reference values in goldenOutputs.h. Any change
// to emulated behaviour, however small, changes a hash. The behavioural tests (RMS, pitch, boot)
// catch a broken sound; this catches a different one.
//
// The hashes cover the DSP's 24-bit integer output words, before the host converts them to float,
// so they do not depend on compiler, optimisation level or CPU. Both audio inputs carry a fixed test
// signal whose samples are exact binary fractions, so input machines, FX machines and RAM recording
// have something to process and its conversion is exact too.
//
//   mdGoldenOutputTest            compare against goldenOutputs.h (skips a model without firmware)
//   mdGoldenOutputTest --update FILE   write a new goldenOutputs.h to FILE (stdout carries emulator logs)
//   mdGoldenOutputTest --sensitivity   rerun with a smaller host block size, which moves the points where
//                                      the scheduler synchronises the processors, and require every
//                                      scenario to change, apart from the few that are silent by design
//
// Both checking modes first prove the comparison can fail: corrupted, missing and extra reference
// values must each be reported.
//
// A hash change is not automatically a bug, but it must be intended: a fix to emulated behaviour
// changes hashes, a performance or refactoring change must not. When a change is intended, run
// --update, replace goldenOutputs.h and say in the commit message why the output changed.
//
// Needs the canonical firmware images (GEARMULATOR_MM_FIRMWARE_BIN, GEARMULATOR_MD_FIRMWARE_BIN);
// the reference values are only meaningful for exactly those images.

#include "goldenOutputs.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr uint32_t g_maxBlock = 512;
	constexpr uint32_t g_sensitivityBlock = 256;
	// Scenario length and note spacing, in frames: multiples of every host block size the test uses.
	constexpr uint32_t g_scenarioFrames = 86 * 512;	// ~1 s
	constexpr uint32_t g_retriggerFrames = 21 * 512;	// ~0.25 s
	// FNV-1a style. The seed is not the standard FNV offset basis (it matches mdmmBench's); for these
	// hashes only consistency matters, so it stays.
	struct Fnv
	{
		uint64_t value = 1469598103934665603ull;
		void byte(const uint8_t _b) { value ^= _b; value *= 1099511628211ull; }
		void word(const uint32_t _w) { for(int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(_w >> (8 * i))); }
	};

	struct Result
	{
		std::string model, scenario;
		uint64_t audio = 0, midi = 0, lcd = 0;
		uint64_t activeSamples = 0, midiEvents = 0;
	};

	class Runner
	{
	public:
		Runner(md::Hardware& _hardware, const md::MachineModel _model, const uint32_t _block)
			: m_hw(_hardware), m_model(_model), m_block(_block)
		{
			for(size_t ch = 0; ch < m_buffers.size(); ++ch)
			{
				m_buffers[ch].resize(g_maxBlock);
				m_outputs[ch] = m_buffers[ch].data();
			}
			for(size_t ch = 0; ch < m_inputBuffers.size(); ++ch)
			{
				m_inputBuffers[ch].resize(g_maxBlock);
				m_inputs[ch] = m_inputBuffers[ch].data();
			}
		}

		void advance(uint32_t _frames)
		{
			while(_frames)
			{
				const auto chunk = std::min<uint32_t>(256, _frames);
				m_hw.advance(chunk);
				_frames -= chunk;
			}
		}

		void sysex(const std::vector<uint8_t>& _message, const uint32_t _settleFrames)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex.assign(_message.begin(), _message.end());
			if(!m_hw.sendMidi(e))
				throw std::runtime_error("SysEx rejected");
			advance(_settleFrames);
		}

		void tap(const md::PanelControl _control)
		{
			const auto packet = md::panelPacket(m_model, _control);
			if(!packet)
				throw std::runtime_error("missing panel control");
			md::PanelRowState rows;
			for(const auto p : {rows.press(*packet), rows.release(*packet)})
			{
				if(!m_hw.trySendPanelEvent(p.row, p.mask))
					throw std::runtime_error("panel event rejected");
				advance(2048);
			}
		}

		// Renders g_scenarioFrames through processAudio, the plug-in's entry point, in host blocks of
		// m_block frames, calling _events with the frame position before each block, and returns the
		// result for this scenario.
		Result render(const std::string& _scenario, const std::function<void(uint32_t _frame)>& _events)
		{
			Result r{m_model == md::MachineModel::Monomachine ? "MM" : "MD", _scenario};
			Fnv audio, midi;
			std::vector<synthLib::SMidiEvent> midiOut;
			const auto blocks = g_scenarioFrames / m_block;
			for(uint32_t b = 0; b < blocks; ++b)
			{
				if(_events)
					_events(b * m_block);
				// Two slow ramps of k/1024 values: exact in float and in the DSP's 24-bit format.
				for(uint32_t i = 0; i < m_block; ++i, ++m_inputSample)
				{
					m_inputBuffers[0][i] = static_cast<float>(static_cast<int>((m_inputSample * 7) % 512) - 256) / 1024.0f;
					m_inputBuffers[1][i] = static_cast<float>(static_cast<int>((m_inputSample * 13) % 384) - 192) / 1024.0f;
				}
				m_hw.processAudio(m_inputs, m_outputs, m_block, 0);
				midiOut.clear();
				m_hw.readMidiOut(midiOut);
				for(const auto& e : midiOut)
				{
					++r.midiEvents;
					midi.word(b * m_block + e.offset);
					midi.byte(e.a); midi.byte(e.b); midi.byte(e.c);
					for(const auto byte : e.sysex)
						midi.byte(byte);
				}
				const auto& words = m_hw.getAudioOutputs();
				for(const auto& channel : words)
				{
					for(uint32_t i = 0; i < m_block && i < channel.size(); ++i)
					{
						const uint32_t w = channel[i] & 0xffffff;
						audio.word(w);
						if(w)
							++r.activeSamples;
					}
				}
			}
			r.audio = audio.value;
			r.midi = midi.value;

			// The screen at the end of the scenario: machine names, parameter pages, menus.
			const auto panel = m_hw.getFrontPanelSnapshot();
			Fnv lcd;
			for(uint32_t half = 0; half < 2; ++half)
				for(uint32_t page = 0; page < 8; ++page)
					for(uint32_t col = 0; col < 64; ++col)
						lcd.byte(panel.getLcdVram(half, page, col));
			r.lcd = lcd.value;
			return r;
		}

		void note(const uint8_t _channel, const uint8_t _note, const uint8_t _velocity)
		{
			m_hw.sendMidi({synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x90 | _channel), _note, _velocity});
		}

	private:
		md::Hardware& m_hw;
		md::MachineModel m_model;
		uint32_t m_block;
		std::array<std::vector<float>, 6> m_buffers;
		synthLib::TAudioOutputs m_outputs{};
		std::array<std::vector<float>, 2> m_inputBuffers;
		synthLib::TAudioInputs m_inputs{};
		uint64_t m_inputSample = 0;
	};

	// Every machine each model's stock OS offers, in the order the scenarios run. The IDs are the
	// firmware's ASSIGN MACHINE numbers; Machinedrum IDs from 128 up are UW machines, sent as
	// (id - 128) with the UW flag set. The names only label scenarios; empty where the firmware
	// shows a numbered name. Kept here so the test does not depend on the editor's catalogue.
	struct Machine
	{
		uint16_t id;
		const char* name;
	};

	constexpr Machine g_monomachineMachines[] =
	{
		{0, "GND-GND"}, {1, "GND-SIN"}, {2, "GND-NOIS"}, {4, "SWAVE-SAW"}, {5, "SWAVE-PULS"}, {14, "SWAVE-ENS"},
		{3, "SID-6581"}, {6, "DPRO-WAVE"}, {7, "DPRO-BBOX"}, {32, "DPRO-DDRW"}, {33, "DPRO-DENS"}, {8, "FM+-STAT"},
		{9, "FM+-PAR"}, {10, "FM+-DYN"}, {11, "VO-VO-6"}, {12, "FX-THRU"}, {13, "FX-REVERB"}, {15, "FX-CHORUS"},
		{16, "FX-DYNAMIX"}, {17, "FX-RINGMOD"},
	};

	constexpr Machine g_machinedrumMachines[] =
	{
		{0, "GND---"}, {1, "GND-SN"}, {2, "GND-NS"}, {3, "GND-IM"}, {16, "TRX-BD"}, {17, "TRX-SD"}, {18, "TRX-XT"},
		{19, "TRX-CP"}, {20, "TRX-RS"}, {21, "TRX-CB"}, {22, "TRX-CH"}, {23, "TRX-OH"}, {24, "TRX-CY"},
		{25, "TRX-MA"}, {26, "TRX-CL"}, {27, "TRX-XC"}, {28, "TRX-B2"}, {29, "TRX-S2"}, {32, "EFM-BD"},
		{33, "EFM-SD"}, {34, "EFM-XT"}, {35, "EFM-CP"}, {36, "EFM-RS"}, {37, "EFM-CB"}, {38, "EFM-HH"},
		{39, "EFM-CY"}, {48, "E12-BD"}, {49, "E12-SD"}, {50, "E12-HT"}, {51, "E12-LT"}, {52, "E12-CP"},
		{53, "E12-RS"}, {54, "E12-CB"}, {55, "E12-CH"}, {56, "E12-OH"}, {57, "E12-RC"}, {58, "E12-CC"},
		{59, "E12-BR"}, {60, "E12-TA"}, {61, "E12-TR"}, {62, "E12-SH"}, {63, "E12-BC"}, {64, "P-I-BD"},
		{65, "P-I-SD"}, {66, "P-I-MT"}, {67, "P-I-ML"}, {68, "P-I-MA"}, {69, "P-I-RS"}, {70, "P-I-RC"},
		{71, "P-I-CC"}, {72, "P-I-HH"}, {80, "INP-GA"}, {81, "INP-GB"}, {82, "INP-FA"}, {83, "INP-FB"},
		{84, "INP-EA"}, {85, "INP-EB"}, {96, ""}, {97, ""}, {98, ""}, {99, ""}, {100, ""}, {101, ""}, {102, ""},
		{103, ""}, {104, ""}, {105, ""}, {106, ""}, {107, ""}, {108, ""}, {109, ""}, {110, ""}, {111, ""},
		{112, "CTR-AL"}, {113, "CTR-8P"}, {120, "CTR-RE"}, {121, "CTR-GB"}, {122, "CTR-EQ"}, {123, "CTR-DX"},
		{128, ""}, {129, ""}, {130, ""}, {131, ""}, {132, ""}, {133, ""}, {134, ""}, {135, ""}, {136, ""},
		{137, ""}, {138, ""}, {139, ""}, {140, ""}, {141, ""}, {142, ""}, {143, ""}, {144, ""}, {145, ""},
		{146, ""}, {147, ""}, {148, ""}, {149, ""}, {150, ""}, {151, ""}, {152, ""}, {153, ""}, {154, ""},
		{155, ""}, {156, ""}, {157, ""}, {158, ""}, {159, ""}, {176, ""}, {177, ""}, {178, ""}, {179, ""},
		{180, ""}, {181, ""}, {182, ""}, {183, ""}, {184, ""}, {185, ""}, {186, ""}, {187, ""}, {188, ""},
		{189, ""}, {190, ""}, {191, ""}, {160, "RAM-R1"}, {161, "RAM-R2"}, {165, "RAM-R3"}, {166, "RAM-R4"},
		{162, "RAM-P1"}, {163, "RAM-P2"}, {167, "RAM-P3"}, {168, "RAM-P4"},
	};

	std::string machineScenario(const Machine& _m)
	{
		std::ostringstream s;
		s << "machine " << std::setw(3) << std::setfill('0') << _m.id;
		if(_m.name && *_m.name)
			s << ' ' << _m.name;
		return s.str();
	}

	// Monomachine: clear the kit, then give all six tracks each machine in turn (as poly mode
	// does) and play a rising chord, retriggered every quarter second.
	std::vector<Result> runMonomachine(md::Hardware& _hw, const uint32_t _block)
	{
		Runner run(_hw, md::MachineModel::Monomachine, _block);
		std::vector<Result> results;

		// KIT > LOAD, FUNCTION+PLAY clears the selected kit, ENTER loads it: six GND-SIN tracks.
		run.tap(md::PanelControl::Kit);
		run.tap(md::PanelControl::Enter);
		{
			const auto function = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Function);
			const auto play = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Play);
			md::PanelRowState rows;
			for(const auto p : {rows.press(*function), rows.press(*play), rows.release(*play), rows.release(*function)})
			{
				if(!_hw.trySendPanelEvent(p.row, p.mask))
					throw std::runtime_error("clear-kit control rejected");
				run.advance(2048);
			}
		}
		run.advance(md::g_samplerate * 2);
		run.tap(md::PanelControl::Enter);
		run.tap(md::PanelControl::Exit);

		results.push_back(run.render("idle", nullptr));

		{
			for(const auto& m : g_monomachineMachines)
			{
				for(uint8_t track = 0; track < 6; ++track)	// init=1 resets the track's data pages
					run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, track, static_cast<uint8_t>(m.id & 0x7f), 0x01, 0xf7},
						md::g_samplerate / 10);
				results.push_back(run.render(machineScenario(m), [&](const uint32_t _frame)
				{
					if(_frame % g_retriggerFrames)
						return;
					const auto step = static_cast<uint8_t>(_frame / g_retriggerFrames);
					for(uint8_t ch = 0; ch < 6; ++ch)
						run.note(ch, static_cast<uint8_t>(48 + step * 2 + ch * 3), 100);
				}));
			}
		}
		return results;
	}

	// Machinedrum: each machine on the first track (BD, note 36), trigged every quarter second
	// with alternating velocities.
	std::vector<Result> runMachinedrum(md::Hardware& _hw, const uint32_t _block)
	{
		Runner run(_hw, md::MachineModel::Machinedrum, _block);
		std::vector<Result> results;
		results.push_back(run.render("idle", nullptr));

		{
			for(const auto& m : g_machinedrumMachines)
			{
				const bool uw = m.id >= 128;
				run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, 0x00,
					static_cast<uint8_t>((uw ? m.id - 128 : m.id) & 0x7f), static_cast<uint8_t>(uw ? 1 : 0), 0xf7},
					md::g_samplerate / 10);
				results.push_back(run.render(machineScenario(m), [&](const uint32_t _frame)
				{
					if(_frame % g_retriggerFrames == 0)
						run.note(0, 36, (_frame / g_retriggerFrames) % 2 ? 64 : 127);
				}));
			}
		}
		return results;
	}

	// True for exactly the stock images the references were made with (the ROM loader's fingerprint:
	// standard FNV-1a 64 over the whole image).
	bool isCanonicalImage(const std::vector<uint8_t>& _rom, const md::MachineModel _model)
	{
		uint64_t fingerprint = 14695981039346656037ull;
		for(const auto byte : _rom)
		{
			fingerprint ^= byte;
			fingerprint *= 1099511628211ull;
		}
		return md::RomLoader::isSupportedImage(_rom.size(), fingerprint, _model);
	}

	// Boots a model and plays its scenarios in host blocks of _block frames, or returns false (skip)
	// when its firmware is not available.
	bool runModel(const md::MachineModel _model, const uint32_t _block, std::vector<Result>& _results)
	{
		const bool mm = _model == md::MachineModel::Monomachine;
		const auto* path = std::getenv(mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::cerr << (mm ? "MM" : "MD") << ": SKIP (firmware not supplied)\n";
			return false;
		}
		std::vector<uint8_t> rom;
		if(!baseLib::filesystem::readFile(rom, path) || !isCanonicalImage(rom, _model))
		{
			std::cerr << (mm ? "MM" : "MD") << ": SKIP (not the canonical "
				<< (mm ? "Monomachine OS 1.32b" : "Machinedrum OS 1.63") << " image the reference values come from)\n";
			return false;
		}

		auto hardware = std::make_unique<md::Hardware>(rom, path, _model);
		Runner(*hardware, _model, _block).advance(md::g_samplerate * 20);
		if(!hardware->isAudioReady())
			throw std::runtime_error(std::string(mm ? "MM" : "MD") + " boot incomplete");

		auto results = mm ? runMonomachine(*hardware, _block) : runMachinedrum(*hardware, _block);
		_results.insert(_results.end(), results.begin(), results.end());
		return true;
	}

	std::string hex(const uint64_t _v)
	{
		std::ostringstream s;
		s << "0x" << std::hex << std::setw(16) << std::setfill('0') << _v << "ull";
		return s.str();
	}

	using Key = std::pair<std::string, std::string>;

	bool sameOutput(const Result& _r, const mdGolden::Entry& _e)
	{
		return _e.audio == _r.audio && _e.midi == _r.midi && _e.lcd == _r.lcd;
	}

	// Compares results with references. Returns one line per problem: a scenario whose output
	// differs, a scenario without a reference, or a reference whose scenario no longer runs (for a
	// model that did run). Empty means everything matches.
	std::vector<std::string> compare(const std::vector<Result>& _results, const std::vector<mdGolden::Entry>& _references)
	{
		std::map<Key, const mdGolden::Entry*> golden;
		for(const auto& e : _references)
			if(*e.model)	// skip the placeholder of an empty table
				golden[{e.model, e.scenario}] = &e;

		std::vector<std::string> problems;
		for(const auto& r : _results)
		{
			const auto it = golden.find({r.model, r.scenario});
			if(it == golden.end())
			{
				problems.push_back(r.model + " '" + r.scenario + "': no reference value (run --update)");
				continue;
			}
			const auto& e = *it->second;
			if(!sameOutput(r, e))
			{
				problems.push_back(r.model + " '" + r.scenario + "':"
					+ (e.audio != r.audio ? " audio " + hex(r.audio) + " expected " + hex(e.audio) : std::string())
					+ (e.midi != r.midi ? " midi " + hex(r.midi) + " expected " + hex(e.midi) : std::string())
					+ (e.lcd != r.lcd ? " lcd " + hex(r.lcd) + " expected " + hex(e.lcd) : std::string()));
			}
			golden.erase(it);
		}
		for(const auto& [key, e] : golden)
		{
			const bool modelRan = std::any_of(_results.begin(), _results.end(), [&](const Result& _r) { return _r.model == key.first; });
			if(modelRan)
				problems.push_back(key.first + " '" + key.second + "': reference value but the scenario no longer runs");
		}
		return problems;
	}

	// The comparison must itself be able to fail. Feed it references with one scenario's hash
	// corrupted, one reference missing and one extra reference, and require each to be reported,
	// and nothing else.
	void selfCheck(const std::vector<Result>& _results)
	{
		if(_results.size() < 3)
			throw std::runtime_error("self-check needs at least three scenarios");
		std::vector<mdGolden::Entry> references;
		for(const auto& r : _results)
			references.push_back({r.model.c_str(), r.scenario.c_str(), r.audio, r.midi, r.lcd});
		const auto expectOnly = [&](const std::vector<mdGolden::Entry>& _refs, const std::string& _what, const std::string& _scenario)
		{
			const auto problems = compare(_results, _refs);
			if(problems.size() != 1 || problems[0].find(_scenario) == std::string::npos || problems[0].find(_what) == std::string::npos)
				throw std::runtime_error("self-check: comparison did not report " + _what + " for '" + _scenario + "'");
		};
		if(!compare(_results, references).empty())
			throw std::runtime_error("self-check: identical references reported a difference");

		auto corrupted = references;
		corrupted[1].lcd ^= 1;
		expectOnly(corrupted, "lcd", _results[1].scenario);

		auto missing = references;
		missing.erase(missing.begin() + 2);
		expectOnly(missing, "no reference value", _results[2].scenario);

		auto extra = references;
		extra.push_back({_results[0].model.c_str(), "a scenario that does not exist", 0, 0, 0});
		expectOnly(extra, "no longer runs", "a scenario that does not exist");
	}

	// Scenarios that are silent by design, with nothing that depends on timing: they are expected
	// to come out identical when the host block size changes. Every other scenario must change.
	const std::set<Key> g_timingInsensitive =
	{
		{"MD", "idle"},
		{"MD", "machine 000 GND---"},
	};

	// Runs with a different host block size and checks that every scenario notices, apart from
	// g_timingInsensitive. Each block boundary is a point where the scheduler brings the three
	// processors to the same emulated time, so a different block size is a small change of
	// interleaving. A scenario that does not react to it could not catch a timing regression, which
	// is exactly what refactoring the scheduler risks.
	int checkSensitivity(const std::vector<Result>& _perturbed, const std::vector<mdGolden::Entry>& _references)
	{
		std::map<Key, const mdGolden::Entry*> golden;
		for(const auto& e : _references)
			if(*e.model)
				golden[{e.model, e.scenario}] = &e;

		int failures = 0;
		size_t changed = 0;
		std::set<Key> seen;
		for(const auto& r : _perturbed)
		{
			const Key key{r.model, r.scenario};
			seen.insert(key);
			const auto it = golden.find(key);
			if(it == golden.end())
			{
				std::cerr << "FAIL " << r.model << " '" << r.scenario << "': no reference value (run --update)\n";
				++failures;
				continue;
			}
			const bool reacted = !sameOutput(r, *it->second);
			changed += reacted ? 1 : 0;
			const bool insensitive = g_timingInsensitive.count(key) != 0;
			if(!reacted && !insensitive)
			{
				std::cerr << "FAIL " << r.model << " '" << r.scenario << "': output unchanged by a different host block size, "
					"so this scenario cannot catch a timing regression\n";
				++failures;
			}
			else if(reacted && insensitive)
			{
				std::cerr << "FAIL " << r.model << " '" << r.scenario << "': listed as timing-insensitive but its output "
					"changed; remove it from g_timingInsensitive\n";
				++failures;
			}
		}
		for(const auto& key : g_timingInsensitive)
		{
			const bool modelRan = std::any_of(_perturbed.begin(), _perturbed.end(), [&](const Result& _r) { return _r.model == key.first; });
			if(modelRan && !seen.count(key))
			{
				std::cerr << "FAIL " << key.first << " '" << key.second << "': in g_timingInsensitive but no such scenario\n";
				++failures;
			}
		}
		if(failures)
		{
			std::cerr << failures << " problem(s) in the sensitivity check\n";
			return 1;
		}
		std::cout << "mdGoldenSensitivityTest: " << changed << " of " << _perturbed.size()
			<< " scenarios changed with host blocks of " << g_sensitivityBlock << " frames; the rest are silent by design\n";
		return 0;
	}

	void writeReferences(const char* _path, const std::vector<Result>& _results)
	{
		std::ofstream out(_path, std::ios::binary | std::ios::trunc);
		if(!out)
			throw std::runtime_error(std::string("cannot write ") + _path);
		out << "#pragma once\n\n"
			"// Reference output of mdGoldenOutputTest. Generated: run mdGoldenOutputTest --update and\n"
			"// replace this file, and say in the commit message why the emulated output changed.\n"
			"// Hashes only; no firmware or audio data.\n\n"
			"#include <cstdint>\n\n"
			"namespace mdGolden\n{\n"
			"\tstruct Entry\n\t{\n\t\tconst char* model;\n\t\tconst char* scenario;\n\t\tuint64_t audio;\n\t\tuint64_t midi;\n\t\tuint64_t lcd;\n\t};\n\n"
			"\tinline constexpr Entry g_entries[] =\n\t{\n";
		for(const auto& r : _results)
			out << "\t\t{ \"" << r.model << "\", \"" << r.scenario << "\", " << hex(r.audio) << ", " << hex(r.midi)
				<< ", " << hex(r.lcd) << " },\t// " << r.activeSamples << " non-zero samples, "
				<< r.midiEvents << " MIDI events\n";
		out << "\t};\n}\n";
	}
}

int main(const int _argc, char** _argv)
{
	const std::string_view mode = _argc > 1 ? _argv[1] : "";
	const bool update = mode == "--update" && _argc > 2;
	const bool sensitivity = mode == "--sensitivity";
	uint32_t block = sensitivity ? g_sensitivityBlock : g_maxBlock;
	if(const char* const b = std::getenv("MD_GOLDEN_BLOCK"))
		block = static_cast<uint32_t>(std::atoi(b));
	if(block == 0 || block > g_maxBlock || g_retriggerFrames % block || g_scenarioFrames % block)
	{
		std::cerr << "MD_GOLDEN_BLOCK must divide " << g_retriggerFrames << " and be at most " << g_maxBlock << '\n';
		return 2;
	}
	if(!mode.empty() && !update && !sensitivity)
	{
		std::cerr << "usage: mdGoldenOutputTest [--update FILE | --sensitivity]\n";
		return 2;
	}
	try
	{
		std::vector<Result> results;
		bool ranAny = false;
		for(const auto model : {md::MachineModel::Monomachine, md::MachineModel::Machinedrum})
			ranAny |= runModel(model, block, results);
		if(!ranAny)
			return 77;

		if(update)
		{
			writeReferences(_argv[2], results);
			return 0;
		}

		selfCheck(results);
		const std::vector<mdGolden::Entry> references(std::begin(mdGolden::g_entries), std::end(mdGolden::g_entries));
		if(sensitivity)
			return checkSensitivity(results, references);

		const auto problems = compare(results, references);
		for(const auto& p : problems)
			std::cerr << "FAIL " << p << '\n';
		if(!problems.empty())
		{
			std::cerr << problems.size() << " scenario(s) differ from the reference output. If the change is intended, run\n"
				"mdGoldenOutputTest --update goldenOutputs.h and explain the change in the commit message.\n";
			return 1;
		}
		std::cout << "mdGoldenOutputTest: " << results.size() << " scenarios match the reference output\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mdGoldenOutputTest: " << _e.what() << '\n';
		return 1;
	}
}
