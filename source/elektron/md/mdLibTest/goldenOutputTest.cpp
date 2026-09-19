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
//   mdGoldenOutputTest --buffer-sizes  render at 512, 128 and 32 frames on parallel instances: 512 must
//                                      match the references, no host queue may lose data, and block
//                                      edges must not stand out in the audio
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

#include "mdLib/mdautomation.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdstate.h"
#include "mdLib/mdsysextransfer.h"

#include "emulatedBudget.h"
#include "sdsTestData.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	constexpr uint32_t g_maxBlock = 512;
	constexpr uint32_t g_sensitivityBlock = 256;
	// Clean renders measured 0.96 to 1.02; a zeroed frame per buffer gives about 4.4.
	constexpr double g_minEdgeRatio = 0.67;
	constexpr double g_maxEdgeRatio = 1.5;
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
		// Mono mix (sum of the six outputs) step sizes at host block edges and elsewhere, for
		// --buffer-sizes. A glitch tied to the host buffer makes block edges stand out.
		double edgeStep = 0, otherStep = 0;
		double sumSquares = 0;	// mono mix loudness, for checks that something played
		uint64_t monoSamples = 0;
		double rms() const { return monoSamples ? std::sqrt(sumSquares / double(monoSamples)) : 0.0; }
		uint64_t edgeCount = 0, otherCount = 0;
		std::vector<float> mono;	// kept only when capturing, for the self-check
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
		Result render(const std::string& _scenario, const std::function<void(uint32_t _frame)>& _events,
			const uint32_t _frames = g_scenarioFrames)
		{
			Result r{m_model == md::MachineModel::Monomachine ? "MM" : "MD", _scenario};
			Fnv audio, midi;
			m_haveLast = false;
			std::vector<synthLib::SMidiEvent> midiOut;
			const auto blocks = _frames / m_block;
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
				for(uint32_t i = 0; i < m_block; ++i)
				{
					double sum = 0;
					for(const auto& channel : words)
						if(i < channel.size())
							sum += static_cast<double>(static_cast<int32_t>(channel[i] << 8) >> 8) / 8388608.0;
					if(m_haveLast)
					{
						const double step = std::abs(sum - m_last);
						if(i == 0) { r.edgeStep += step; ++r.edgeCount; }
						else { r.otherStep += step; ++r.otherCount; }
					}
					m_last = sum;
					r.sumSquares += sum * sum;
					++r.monoSamples;
					m_haveLast = true;
					if(m_capture)
						r.mono.push_back(static_cast<float>(sum));
				}
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

		void controlChange(const uint8_t _status, const uint8_t _controller, const uint8_t _value)
		{
			if(!m_hw.sendMidi({synthLib::MidiEventSource::Host, _status, _controller, _value}))
				throw std::runtime_error("CC rejected");
		}

		md::MachineModel model() const { return m_model; }

		// Presses _press while _hold is held, as for FUNCTION + key combinations.
		void tapWith(const md::PanelControl _hold, const md::PanelControl _press)
		{
			const auto hold = md::panelPacket(m_model, _hold);
			const auto press = md::panelPacket(m_model, _press);
			if(!hold || !press)
				throw std::runtime_error("missing panel control");
			md::PanelRowState rows;
			for(const auto p : {rows.press(*hold), rows.press(*press), rows.release(*press), rows.release(*hold)})
			{
				if(!m_hw.trySendPanelEvent(p.row, p.mask))
					throw std::runtime_error("panel event rejected");
				advance(2048);
			}
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
		double m_last = 0;
		bool m_haveLast = false;
	public:
		bool m_capture = false;
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


	// Two bars at 120 BPM is 176400 frames; scenario lengths are multiples of 512 frames so that
	// every host block size sees the same input at the same frame.
	constexpr uint32_t g_barFrames = 172 * 512;

	// Programs one track of the current pattern in grid mode, the way a user does: select the track,
	// press RECORD, toggle step keys, press RECORD again. The pattern is not empty on a fresh machine,
	// so the steps are read back from the step LEDs and only the ones that differ are toggled; then
	// the LEDs must show exactly the wanted steps, or the edit did not reach the firmware.
	// On the Machinedrum a trig key alone plays a track without selecting it; FUNCTION + trig key
	// selects it, and its track LED must then be the only one lit, so the edit lands on that track.
	// _verifyOnly checks the steps without changing them.
	void programTrack(Runner& _run, md::Hardware& _hw, const md::PanelControl _select, const uint16_t _steps,
		const bool _verifyOnly = false)
	{
		const bool mm = _run.model() == md::MachineModel::Monomachine;
		const auto lit = [&](const uint32_t _i)
		{
			const auto p = _hw.getFrontPanelSnapshot();
			return mm ? p.getMonomachineStepLedColor(_i) != md::FrontPanel::LedColor::Off : p.getStepLed(_i);
		};
		if(mm)
		{
			_run.tap(_select);
		}
		else
		{
			_run.tapWith(md::PanelControl::Function, _select);
			const auto track = static_cast<uint32_t>(_select) - static_cast<uint32_t>(md::PanelControl::Trigger1);
			const auto panel = _hw.getFrontPanelSnapshot();
			for(uint32_t i = 0; i < 16; ++i)
				if(panel.getDrumLed(i) != (i == track))
					throw std::runtime_error("MD: FUNCTION + trig " + std::to_string(track + 1) + " did not select that track");
		}
		_run.tap(md::PanelControl::Record);
		// Selecting a Machinedrum track with its trig key also plays it, and its LED stays lit for a
		// moment; let that pass before reading the steps.
		_run.advance(md::g_samplerate / 2);
		if(!mm && !_hw.getFrontPanelSnapshot().getModeLed(md::FrontPanel::ModeLed::Record))
			throw std::runtime_error("RECORD did not enter grid mode");
		for(uint32_t i = 0; i < 16 && !_verifyOnly; ++i)
			if(lit(i) != ((_steps >> i) & 1))
				_run.tap(static_cast<md::PanelControl>(static_cast<uint32_t>(md::PanelControl::Trigger1) + i));
		std::optional<uint32_t> wrong;
		for(uint32_t i = 0; i < 16 && !wrong; ++i)
			if(lit(i) != ((_steps >> i) & 1))
				wrong = i;
		_run.tap(md::PanelControl::Record);	// leave grid mode, also when failing
		if(wrong)
		{
			const auto i = *wrong;
			throw std::runtime_error(std::string(mm ? "MM" : "MD") + (_verifyOnly ? ": after save and restore, step " : ": grid edit did not set step ") + std::to_string(i + 1)
					+ " of the track selected by panel control " + std::to_string(static_cast<int>(_select)));
		}
	}

	constexpr uint16_t stepMask(const std::initializer_list<uint32_t> _steps)
	{
		uint16_t mask = 0;
		for(const auto s : _steps)
			mask |= static_cast<uint16_t>(1u << s);
		return mask;
	}

	struct TrackSteps
	{
		md::PanelControl select;
		uint16_t steps;
	};

	// Machinedrum: kick, snare (the companion) and closed hat. Monomachine: a bass line on track 1
	// and stabs on tracks 2 and 3.
	const std::vector<TrackSteps> g_machinedrumPattern =
	{
		{md::PanelControl::Trigger1, stepMask({0, 4, 8, 12})},
		{md::PanelControl::Trigger2, stepMask({4, 12})},
		{md::PanelControl::Trigger9, stepMask({0, 2, 4, 6, 8, 10, 12, 14})},
	};
	const std::vector<TrackSteps> g_monomachinePattern =
	{
		{md::PanelControl::Track1, stepMask({0, 2, 4, 6, 8, 10, 12, 14})},
		{md::PanelControl::Track2, stepMask({4, 12})},
		{md::PanelControl::Track3, stepMask({4, 12})},
	};

	// Plays the programmed pattern for two bars with no input: whatever sounds comes from the sequencer.
	void playPattern(Runner& _run, std::vector<Result>& _results, const std::string& _scenario = "sequencer pattern")
	{
		// On the Monomachine the first PLAY after grid editing runs the sequencer silently; stopping
		// and starting again plays the pattern. Do that on both models so they are driven alike.
		_run.tap(md::PanelControl::Play);
		_run.advance(md::g_samplerate / 2);
		_run.tap(md::PanelControl::Stop);
		_run.tap(md::PanelControl::Stop);
		_run.tap(md::PanelControl::Play);
		_results.push_back(_run.render(_scenario, nullptr, 2 * g_barFrames));
		_run.tap(md::PanelControl::Stop);
		_run.tap(md::PanelControl::Stop);
		if(_results.back().rms() < 0.01)
			throw std::runtime_error(_results.back().model + ": the programmed pattern played nothing (rms " + std::to_string(_results.back().rms()) + ")");
	}

	// Monomachine: clear the kit, then give all six tracks each machine in turn (as poly mode
	// does) and play a rising chord, retriggered every quarter second.
	std::vector<Result> runMonomachine(md::Hardware& _hw, const uint32_t _block, const bool _capture)
	{
		Runner run(_hw, md::MachineModel::Monomachine, _block);
		run.m_capture = _capture;
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

		// The machine scenarios end on FX machines, which have no voice of their own.
		run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, 0x00, 3, 0x01, 0xf7}, md::g_samplerate / 10);	// SID-6581
		for(const uint8_t track : {1, 2})
			run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, track, 8, 0x01, 0xf7}, md::g_samplerate / 10);	// FM+ STAT
		for(const auto& t : g_monomachinePattern)
			programTrack(run, _hw, t.select, t.steps);
		playPattern(run, results);
		return results;
	}

	// The UW loop slots (ROM-33 to ROM-48) are empty on a fresh machine, so their machines would play
	// nothing. Import a short generated sample into each over SDS, a different pitch per slot, the
	// way a user fills them; this also covers the sample import and playback paths.
	void importTestSamples(md::Hardware& _hw, Runner& _run)
	{
		md::test::EmulatedBudget ready(180);
		while(_hw.isFactoryFlashInitializationExpected() && !_hw.isFactoryFlashCacheReady())
		{
			_run.advance(64);
			if(!ready.spend(64))
				throw std::runtime_error("MD factory flash initialization did not finish");
		}
		std::vector<uint8_t> stream;
		for(uint8_t slot = 32; slot < 48; ++slot)
		{
			const auto sample = md::test::sdsSample(1024, 16, 0, slot, 40 + (slot - 32) * 5);
			stream.insert(stream.end(), sample.begin(), sample.end());
		}
		auto prepared = md::prepareMidiSysexTransfer(stream, md::MachineModel::Machinedrum);
		if(!prepared || !_hw.startMidiSysexTransfer(*prepared))
			throw std::runtime_error("sample import did not start");
		md::test::EmulatedBudget transfer(600);
		for(;;)
		{
			_run.advance(64);
			const auto progress = _hw.getMidiSysexTransferProgress();
			if(progress.state == md::MidiSysexTransferState::Complete)
				break;
			if(progress.state == md::MidiSysexTransferState::Failed || progress.state == md::MidiSysexTransferState::Cancelled)
				throw std::runtime_error("sample import failed");
			if(!transfer.spend(64))
				throw std::runtime_error("sample import did not finish");
		}
		_run.advance(md::g_samplerate * 15);	// let the firmware finish writing the samples to flash
	}


	// A walk through pages, menus and knob turns, the way a user looks around: the LCD after every
	// step is hashed into one chain, so any change in what the firmware draws on those screens shows.
	// Controls a model does not have are skipped. The walk must reach enough different screens, or
	// the panel input is not getting through.
	void navigateScreens(Runner& _run, md::Hardware& _hw, std::vector<Result>& _results)
	{
		const bool mm = _run.model() == md::MachineModel::Monomachine;
		using C = md::PanelControl;
		using E = md::PanelEncoder;
		struct Step
		{
			bool encoder;
			C control;
			E knob;
			int delta;
		};
		std::vector<Step> steps =
		{
			{false, C::DataPageForward, {}, 0}, {false, C::DataPageForward, {}, 0}, {false, C::DataPageBackward, {}, 0},
			{true, {}, E::DataEntryA, 5}, {true, {}, E::DataEntryB, -3},
			{false, C::Kit, {}, 0}, {false, C::Down, {}, 0}, {false, C::Exit, {}, 0},
			{false, C::Tempo, {}, 0}, {false, C::Exit, {}, 0},
			{false, C::Scale, {}, 0}, {false, C::Right, {}, 0}, {false, C::Exit, {}, 0},
			{false, C::PatternSong, {}, 0}, {false, C::PatternSong, {}, 0},
			{false, C::Up, {}, 0}, {false, C::Down, {}, 0}, {false, C::Left, {}, 0}, {false, C::Right, {}, 0},
			{false, C::Exit, {}, 0},
		};
		if(!mm)
			for(int i = 0; i < 3; ++i)
				steps.push_back({false, C::SynthesisEffectsRouting, {}, 0});

		Fnv chain;
		std::set<uint64_t> screens;
		const auto lcdHash = [&]
		{
			const auto panel = _hw.getFrontPanelSnapshot();
			Fnv h;
			for(uint32_t half = 0; half < 2; ++half)
				for(uint32_t page = 0; page < 8; ++page)
					for(uint32_t col = 0; col < 64; ++col)
						h.byte(panel.getLcdVram(half, page, col));
			return h.value;
		};
		for(const auto& step : steps)
		{
			if(step.encoder)
			{
				const auto command = md::panelEncoderCommand(_run.model(), step.knob);
				if(!command)
					continue;
				for(int i = 0; i < std::abs(step.delta); ++i)
				{
					if(!_hw.trySendPanelEvent(*command, static_cast<uint8_t>(step.delta > 0 ? 0x01 : 0xff)))
						throw std::runtime_error("encoder event rejected");
					_run.advance(2048);
				}
			}
			else
			{
				if(!md::panelPacket(_run.model(), step.control))
					continue;
				_run.tap(step.control);
			}
			_run.advance(md::g_samplerate / 4);
			const auto screen = lcdHash();
			screens.insert(screen);
			for(int b = 0; b < 8; ++b)
				chain.byte(static_cast<uint8_t>(screen >> (8 * b)));
		}
		auto r = _run.render("screen navigation", nullptr, 20 * 512);
		for(int b = 0; b < 8; ++b)
			chain.byte(static_cast<uint8_t>(r.lcd >> (8 * b)));
		r.lcd = chain.value;
		// Measured: 11 distinct screens on the Monomachine, 13 on the Machinedrum; a dead panel shows one.
		if(screens.size() < 6)
			throw std::runtime_error(r.model + ": the navigation walk reached only " + std::to_string(screens.size())
				+ " different screens; panel input is not getting through");
		_results.push_back(std::move(r));
	}

	// Machinedrum: each machine on the first track (BD, note 36) with the companion on the second,
	// trigged every quarter second
	// with alternating velocities.
	std::vector<Result> runMachinedrum(md::Hardware& _hw, const uint32_t _block, const bool _capture)
	{
		Runner run(_hw, md::MachineModel::Machinedrum, _block);
		run.m_capture = _capture;
		std::vector<Result> results;
		results.push_back(run.render("idle", nullptr));
		importTestSamples(_hw, run);

		// A companion: TRX-SD on the second track, with delay and reverb sends up, played with every
		// hit. Control machines (CTR-*) act on other tracks and on the master effects, so without a
		// second sounding track they have nothing to act on.
		run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, 0x01, 17, 0x00, 0xf7}, md::g_samplerate / 10);
		for(const uint8_t send : {3, 4})	// ROUTING page: DEL, REV
		{
			const auto cc = md::automation::encodeParameterChange(md::MachineModel::Machinedrum,
				{md::automation::machinedrum::Routing, 1, send, 80}, 0);
			if(!cc)
				throw std::runtime_error("no CC for the companion's sends");
			run.controlChange((*cc)[0], (*cc)[1], (*cc)[2]);
		}
		run.advance(md::g_samplerate / 10);

		{
			for(const auto& m : g_machinedrumMachines)
			{
				const bool uw = m.id >= 128;
				run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, 0x00,
					static_cast<uint8_t>((uw ? m.id - 128 : m.id) & 0x7f), static_cast<uint8_t>(uw ? 1 : 0), 0xf7},
					md::g_samplerate / 10);
				results.push_back(run.render(machineScenario(m), [&](const uint32_t _frame)
				{
					if(_frame % g_retriggerFrames)
						return;
					run.note(0, 36, (_frame / g_retriggerFrames) % 2 ? 64 : 127);
					run.note(0, 38, 100);	// the companion
				}));
			}
		}

		run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, 0x00, 16, 0x00, 0xf7}, md::g_samplerate / 10);	// TRX-BD
		run.sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, 0x08, 22, 0x00, 0xf7}, md::g_samplerate / 10);	// TRX-CH
		for(const auto& t : g_machinedrumPattern)
			programTrack(run, _hw, t.select, t.steps);
		playPattern(run, results);
		return results;
	}

	// Host-side queue and MIDI loss counters after a model's scenarios; all must stay zero.
	struct Counters
	{
		std::string model;
		uint64_t outputOverflow = 0, inputUnderflow = 0, inputOverflow = 0, midiRxOverflow = 0, scheduledMidiOverflow = 0;
		bool zero() const { return !outputOverflow && !inputUnderflow && !inputOverflow && !midiRxOverflow && !scheduledMidiOverflow; }
	};

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
	bool runModel(const md::MachineModel _model, const uint32_t _block, std::vector<Result>& _results,
		const bool _capture = false, std::vector<Counters>* _counters = nullptr)
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

		auto results = mm ? runMonomachine(*hardware, _block, _capture) : runMachinedrum(*hardware, _block, _capture);

		// Queue and MIDI loss on this machine and on the restored one below, summed.
		Counters counters{mm ? "MM" : "MD"};
		const auto addCounters = [&counters](const md::Hardware& _hw)
		{
			counters.outputOverflow += _hw.hostAudioOverflowCount();
			counters.inputUnderflow += _hw.hostAudioInputUnderflowCount();
			counters.inputOverflow += _hw.hostAudioInputOverflowCount();
			counters.midiRxOverflow += _hw.midiRxOverflowCount();
			counters.scheduledMidiOverflow += _hw.scheduledMidiOverflowCount();
		};
		addCounters(*hardware);

		// Save and reload, as a project does: the patch RAM (kits and patterns) goes through the
		// state encoder, the flash is carried over, and a fresh machine boots from both. The
		// pattern programmed above must still be there and play.
		{
			const auto patchRam = hardware->copyPatchRam();
			const auto flash = hardware->copyFlashData();
			std::vector<uint8_t> state, restoredPatchRam;
			if(!md::encodeState(state, patchRam, _model, synthLib::StateTypeGlobal)
				|| !md::decodeState(restoredPatchRam, state, _model, synthLib::StateTypeGlobal)
				|| restoredPatchRam != patchRam)
				throw std::runtime_error(std::string(mm ? "MM" : "MD") + ": patch RAM did not survive the state encoder");
			hardware.reset();
			auto restored = std::make_unique<md::Hardware>(rom, path, _model, restoredPatchRam,
				std::shared_ptr<md::FrontPanelPublisher>{}, flash);
			Runner run(*restored, _model, _block);
			run.m_capture = _capture;
			run.advance(md::g_samplerate * 20);
			if(!restored->isAudioReady())
				throw std::runtime_error(std::string(mm ? "MM" : "MD") + ": restored machine did not boot");
			for(const auto& t : mm ? g_monomachinePattern : g_machinedrumPattern)
				programTrack(run, *restored, t.select, t.steps, true);
			playPattern(run, results, "restored pattern");
			navigateScreens(run, *restored, results);
			addCounters(*restored);
		}
		if(_counters)
			_counters->push_back(counters);
		_results.insert(_results.end(), results.begin(), results.end());
		return true;
	}


	// Runs both models, each on its own thread (separate machine instances are independent, which
	// the block-size test checks), and returns their results in a fixed order: Monomachine, then
	// Machinedrum. Returns false when neither model's firmware is available; rethrows a failure.
	bool runModels(const uint32_t _block, std::vector<Result>& _results, const bool _capture = false,
		std::vector<Counters>* _counters = nullptr)
	{
		struct PerModel
		{
			md::MachineModel model;
			std::vector<Result> results;
			std::vector<Counters> counters;
			bool ran = false;
			std::string error;
		};
		std::array<PerModel, 2> models{{{md::MachineModel::Monomachine, {}, {}, false, {}}, {md::MachineModel::Machinedrum, {}, {}, false, {}}}};
		std::vector<std::thread> threads;
		for(auto& m : models)
		{
			threads.emplace_back([&m, _block, _capture, _counters]
			{
				try
				{
					m.ran = runModel(m.model, _block, m.results, _capture, _counters ? &m.counters : nullptr);
				}
				catch(const std::exception& _e)
				{
					m.error = _e.what();
				}
			});
		}
		for(auto& t : threads)
			t.join();
		for(const auto& m : models)
			if(!m.error.empty())
				throw std::runtime_error(m.error);
		bool ran = false;
		for(auto& m : models)
		{
			ran |= m.ran;
			_results.insert(_results.end(), m.results.begin(), m.results.end());
			if(_counters)
				_counters->insert(_counters->end(), m.counters.begin(), m.counters.end());
		}
		return ran;
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

	// Mean step at host block edges over mean step elsewhere, pooled over every audible scenario.
	// Block edges are arbitrary points in the emulated signal, so a clean render gives about 1.
	double edgeRatio(const std::vector<Result>& _results)
	{
		double edge = 0, other = 0;
		uint64_t edges = 0, others = 0;
		for(const auto& r : _results)
		{
			edge += r.edgeStep; edges += r.edgeCount;
			other += r.otherStep; others += r.otherCount;
		}
		return edges && others && other > 0 ? (edge / double(edges)) / (other / double(others)) : 0.0;
	}

	// The same ratio from captured mono audio, optionally with the frame before every block edge
	// zeroed, as a host-queue shortfall would leave it.
	double capturedEdgeRatio(const std::vector<Result>& _results, const uint32_t _block, const bool _zeroEdgeFrames)
	{
		double edge = 0, other = 0;
		uint64_t edges = 0, others = 0;
		for(const auto& r : _results)
		{
			auto x = r.mono;
			if(_zeroEdgeFrames)
				for(size_t i = _block - 1; i < x.size(); i += _block)
					x[i] = 0.0f;
			for(size_t i = 1; i < x.size(); ++i)
			{
				const double step = std::abs(double(x[i]) - double(x[i - 1]));
				if(i % _block == 0) { edge += step; ++edges; }
				else { other += step; ++others; }
			}
		}
		return edges && others && other > 0 ? (edge / double(edges)) / (other / double(others)) : 0.0;
	}

	// Renders every scenario at the reference block size and at small host buffers, each on its own
	// thread, the way several plug-in instances run in one host. Requires:
	// - the 512-frame run to match the references exactly, although other instances run beside it;
	// - no host audio or MIDI queue to overflow or underflow at any buffer size;
	// - block edges not to stand out in the audio at the small sizes (a dropped, zeroed or clicking
	//   frame per buffer would). Exact output is not comparable across buffer sizes: the processors
	//   interleave differently and the renders drift apart, like two takes on real hardware.
	// The edge check first proves it can fail, on its own capture with a frame zeroed per buffer.
	int checkBufferSizes(const std::vector<mdGolden::Entry>& _references)
	{
		struct Run
		{
			uint32_t block;
			std::vector<Result> results;
			std::vector<Counters> counters;
			bool ranAny = false;
			std::string error;
		};
		std::vector<Run> runs{{g_maxBlock, {}, {}, false, {}}, {128, {}, {}, false, {}}, {32, {}, {}, false, {}}};
		std::vector<std::thread> threads;
		for(auto& run : runs)
		{
			threads.emplace_back([&run]
			{
				try
				{
					run.ranAny = runModels(run.block, run.results, run.block != g_maxBlock, &run.counters);
				}
				catch(const std::exception& _e)
				{
					run.error = _e.what();
				}
			});
		}
		for(auto& t : threads)
			t.join();

		// Skip only when no firmware was available; a run that threw is a failure, not a skip.
		if(std::none_of(runs.begin(), runs.end(), [](const Run& _r) { return _r.ranAny || !_r.error.empty(); }))
			return 77;

		int failures = 0;
		for(const auto& run : runs)
		{
			const auto label = "block " + std::to_string(run.block);
			if(!run.error.empty())
			{
				std::cerr << "FAIL " << label << ": " << run.error << '\n';
				++failures;
				continue;
			}
			for(const auto& c : run.counters)
			{
				if(!c.zero())
				{
					std::cerr << "FAIL " << label << ' ' << c.model << ": host queues lost data (output overflow " << c.outputOverflow
						<< ", input underflow " << c.inputUnderflow << ", input overflow " << c.inputOverflow
						<< ", MIDI receive overflow " << c.midiRxOverflow << ", scheduled MIDI overflow " << c.scheduledMidiOverflow << ")\n";
					++failures;
				}
			}
			if(run.block == g_maxBlock)
			{
				const auto problems = compare(run.results, _references);
				for(const auto& p : problems)
					std::cerr << "FAIL " << label << " beside other instances: " << p << '\n';
				failures += static_cast<int>(problems.size());
				continue;
			}
			const auto ratio = edgeRatio(run.results);
			const auto recomputed = capturedEdgeRatio(run.results, run.block, false);
			const auto glitched = capturedEdgeRatio(run.results, run.block, true);
			std::cout << label << ": edge step ratio " << ratio << " (with a frame zeroed per buffer: " << glitched << ")\n";
			if(std::abs(recomputed - ratio) > 1e-3 * ratio)
			{
				std::cerr << "FAIL " << label << ": edge statistic disagrees with its own capture (" << ratio << " vs " << recomputed << ")\n";
				++failures;
			}
			if(glitched < g_maxEdgeRatio)
			{
				std::cerr << "FAIL " << label << ": self-check: a zeroed frame per buffer gave ratio " << glitched
					<< ", which would pass; the edge check cannot detect glitches\n";
				++failures;
			}
			if(ratio < g_minEdgeRatio || ratio > g_maxEdgeRatio)
			{
				std::cerr << "FAIL " << label << ": block edges stand out in the audio (step ratio " << ratio << ", expected "
					<< g_minEdgeRatio << " to " << g_maxEdgeRatio << "): something happens at every host buffer boundary\n";
				++failures;
			}
		}
		if(failures)
		{
			std::cerr << failures << " problem(s) across host buffer sizes\n";
			return 1;
		}
		std::cout << "mdBlockSizeTest: " << runs[0].results.size() << " scenarios at 512, 128 and 32 frames on parallel instances: "
			"512 matches the references, no queue lost data, no buffer-edge artefacts\n";
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
	const bool bufferSizes = mode == "--buffer-sizes";
	uint32_t block = sensitivity ? g_sensitivityBlock : g_maxBlock;
	if(const char* const b = std::getenv("MD_GOLDEN_BLOCK"))
		block = static_cast<uint32_t>(std::atoi(b));
	if(block == 0 || block > g_maxBlock || g_retriggerFrames % block || g_scenarioFrames % block)
	{
		std::cerr << "MD_GOLDEN_BLOCK must divide " << g_retriggerFrames << " and be at most " << g_maxBlock << '\n';
		return 2;
	}
	if(!mode.empty() && !update && !sensitivity && !bufferSizes)
	{
		std::cerr << "usage: mdGoldenOutputTest [--update FILE | --sensitivity | --buffer-sizes]\n";
		return 2;
	}
	try
	{
		if(bufferSizes)
			return checkBufferSizes(std::vector<mdGolden::Entry>(std::begin(mdGolden::g_entries), std::end(mdGolden::g_entries)));

		std::vector<Result> results;
		if(!runModels(block, results))
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
