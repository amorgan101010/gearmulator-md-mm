#include "mdNativePanelKeylight.h"

#include "mdKeyboardControl.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace
{
	using mdJucePlugin::KeylightKey;
	using Map = std::map<std::string, std::string>;

	Map toMap(const std::vector<KeylightKey>& keys)
	{
		Map map;
		for(const auto& key : keys)
		{
			assert(map.find(key.keysym) == map.end()); // a key is lit once
			map[key.keysym] = key.group;
		}
		return map;
	}

	void add(Map& map, const char* group, std::initializer_list<const char*> keysyms)
	{
		for(const char* keysym : keysyms)
			map[keysym] = group;
	}

	// The semantic map of the keymap sheet (the "Elektron Keymap" artifact), as the
	// executable native keymap makes it: see docs/native-panel-keylight.md in
	// Elektremu Studio for the three keys on which the sheet is stale.
	Map common()
	{
		Map map;
		add(map, "steps", { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
			"1", "2", "3", "4", "5", "6", "7", "8" });
		add(map, "banks", { "F9", "F10", "F11", "F12", "grave" });
		add(map, "knobs", { "a", "s", "d", "f", "z", "x", "c", "v", "g",
			"minus", "equal", "bracketleft", "bracketright" });
		add(map, "transport", { "space", "End", "Home" });
		add(map, "nav", { "Up", "Down", "Left", "Right", "Return", "BackSpace", "Prior", "Next" });
		add(map, "menus", { "k", "u", "i" });
		add(map, "mod", { "Shift_L", "Shift_R", "Control_L", "Control_R", "Delete" });
		return map;
	}

	Map expectedMachinedrum()
	{
		Map map = common();
		add(map, "knobs", { "h" });                    // the SOUND wheel
		add(map, "menus", { "o", "p", "n" });          // SYNTH/FX, PATTERN/SONG, CLASSIC/EXT
		return map;                                    // no track keys, TRIG SELECT or SONG ENABLE
	}

	Map expectedMonomachine()
	{
		Map map = common();
		add(map, "tracks", { "q", "w", "e", "r", "t", "y" });
		add(map, "menus", { "b", "m" });               // TRIG SELECT, SONG ENABLE
		return map;                                    // no SOUND wheel, SYNTH/FX, PATTERN/SONG or CLASSIC/EXT
	}

	void report(const char* name, const Map& got, const Map& want)
	{
		if(got == want)
			return;
		std::cerr << name << " keylight map differs:\n";
		for(const auto& item : got)
			if(!want.count(item.first) || want.at(item.first) != item.second)
				std::cerr << "  unexpected " << item.first << " -> " << item.second << '\n';
		for(const auto& item : want)
			if(!got.count(item.first))
				std::cerr << "  missing " << item.first << " -> " << item.second << '\n';
		assert(false);
	}

	void testMachinedrumMapIsExact()
	{
		report("MD", toMap(mdJucePlugin::keylightKeys(md::MachineModel::Machinedrum)),
			expectedMachinedrum());
	}

	void testMonomachineMapIsExact()
	{
		report("MM", toMap(mdJucePlugin::keylightKeys(md::MachineModel::Monomachine)),
			expectedMonomachine());
	}

	void testTheModelsDifferWhereTheirKeysDo()
	{
		const auto md = toMap(mdJucePlugin::keylightKeys(md::MachineModel::Machinedrum));
		const auto mm = toMap(mdJucePlugin::keylightKeys(md::MachineModel::Monomachine));
		assert(md.count("q") == 0 && mm.at("q") == "tracks");
		assert(md.at("h") == "knobs" && mm.count("h") == 0);
		assert(md.at("o") == "menus" && mm.count("o") == 0);
		assert(md.count("b") == 0 && mm.at("b") == "menus");
		assert(md.at("Prior") == "nav" && mm.at("Prior") == "nav");
		assert(md != mm);
	}

	void testUnmappedKeysStayDark()
	{
		for(const auto model : { md::MachineModel::Machinedrum, md::MachineModel::Monomachine })
		{
			const auto map = toMap(mdJucePlugin::keylightKeys(model));
			for(const char* dark : { "Escape", "Print", "Tab", "Caps_Lock", "backslash", "0", "9",
				"j", "l", "semicolon", "apostrophe", "comma", "period", "slash",
				"Alt_L", "Alt_R", "Super_L" })
				assert(map.count(dark) == 0);
		}
	}

	void testMessages()
	{
		const auto show = mdJucePlugin::keylightShowMessage(md::MachineModel::Monomachine);
		assert(show.rfind("{\"show\":{", 0) == 0);
		assert(show.find("\"q\":\"tracks\"") != std::string::npos);
		assert(show.find("\"h\"") == std::string::npos);
		assert(show.back() == '\n' && std::count(show.begin(), show.end(), '\n') == 1);
		assert(mdJucePlugin::keylightRestoreMessage() == "{\"restore\":true}\n");
		assert(mdJucePlugin::keylightQuitMessage() == "{\"quit\":true}\n");
	}

	// The drift guard: sweep every key identifier RmlUi has through the real
	// PanelKeyboardControl, and count those that reach the panel. That has to be the
	// lit keys, less the four modifiers that arrive as flags rather than as keys.
	class EffectActions final : public mdJucePlugin::PanelKeyboardActions
	{
	public:
		explicit EffectActions(md::MachineModel model) : m_model(model) {}
		bool effect = false;
		void keyboardPressControl(md::PanelControl control, bool, bool) override
		{
			if(md::panelPacket(m_model, control)) effect = true;
		}
		void keyboardReleaseControl(md::PanelControl) override {}
		bool keyboardEncoderAvailable(md::PanelEncoder encoder) const override
		{
			return md::panelEncoderCommand(m_model, encoder).has_value();
		}
		void keyboardSelectEncoder(std::optional<md::PanelEncoder> encoder) override
		{
			if(encoder && keyboardEncoderAvailable(*encoder)) effect = true;
		}
		void keyboardPressEncoder(md::PanelEncoder) override {}
		void keyboardReleaseEncoder(md::PanelEncoder) override {}
		void keyboardTurnEncoder(md::PanelEncoder, float) override { effect = true; }
		void keyboardClickEncoder(md::PanelEncoder) override {}
		bool keyboardHandlesMachinedrumPageKeys() const override
		{
			return m_model == md::MachineModel::Machinedrum;
		}
		void keyboardMachinedrumPageKey(int, bool) override { effect = true; }
		void keyboardReleaseAllPanelInputs() override { effect = true; }
	private:
		md::MachineModel m_model;
	};

	void testEveryKeyThatActsIsLit()
	{
		for(const auto model : { md::MachineModel::Machinedrum, md::MachineModel::Monomachine })
		{
			size_t acting = 0;
			for(int id = 1; id < 256; ++id)
			{
				mdJucePlugin::PanelKeyboardControl keyboard;
				EffectActions held(model), pressed(model);
				if(id != static_cast<int>(Rml::Input::KI_A))
					keyboard.handleKey(static_cast<int>(Rml::Input::KI_A), true, false, false, 0.0, held);
				keyboard.handleKey(id, true, false, false, 1.0, pressed);
				if(id == static_cast<int>(Rml::Input::KI_A))
					pressed.effect = held.effect || pressed.effect;
				if(pressed.effect)
					++acting;
			}
			const auto lit = mdJucePlugin::keylightKeys(model).size();
			assert(acting + 4 == lit);                 // + Shift_L/R, Control_L/R
		}
	}

	// ---- the process plumbing, against a helper that records what it is sent ----

	std::filesystem::path scratch()
	{
		static const auto dir = [] {
			auto path = std::filesystem::temp_directory_path()
				/ ("keylight-test-" + std::to_string(::getpid()));
			std::filesystem::create_directories(path);
			return path;
		}();
		return dir;
	}

	std::string writeHelper(const std::string& name, const std::string& body)
	{
		const auto path = scratch() / name;
		{
			std::ofstream out(path);
			out << "#!/bin/sh\n" << body << '\n';
		}
		std::filesystem::permissions(path, std::filesystem::perms::owner_all);
		return path.string();
	}

	std::string slurp(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	bool waitForLines(const std::filesystem::path& path, size_t lines)
	{
		for(int i = 0; i < 300; ++i)
		{
			const auto text = slurp(path);
			if(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) >= lines)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return false;
	}

	std::vector<std::string> lines(const std::filesystem::path& path)
	{
		std::vector<std::string> out;
		std::istringstream text(slurp(path));
		for(std::string line; std::getline(text, line);)
			out.push_back(line);
		return out;
	}

	void testFocusInPaintsFocusOutRestoresAndClosingQuits()
	{
		const auto log = scratch() / "record.log";
		std::filesystem::remove(log);
		const auto helper = writeHelper("record.sh", "cat >> '" + log.string() + "'");
		{
			mdJucePlugin::NativePanelKeylight light(md::MachineModel::Monomachine, helper);
			assert(light.enabled() && !light.helperRunning());
			light.setFocused(false);                  // never focused: nothing started
			assert(!light.helperRunning());
			light.setFocused(true);
			light.setFocused(true);                   // no change: nothing sent
			assert(light.helperRunning() && light.focused());
			assert(waitForLines(log, 1));
			light.setFocused(false);
			light.setFocused(false);
			assert(waitForLines(log, 2));
			light.setFocused(true);
			assert(waitForLines(log, 3));
		}                                             // closing the panel
		assert(waitForLines(log, 4));
		const auto got = lines(log);
		assert(got.size() == 4);
		assert(got[0] + "\n" == mdJucePlugin::keylightShowMessage(md::MachineModel::Monomachine));
		assert(got[1] + "\n" == mdJucePlugin::keylightRestoreMessage());
		assert(got[2] == got[0]);
		assert(got[3] + "\n" == mdJucePlugin::keylightQuitMessage());
	}

	void testEachModelSendsItsOwnMap()
	{
		for(const auto model : { md::MachineModel::Machinedrum, md::MachineModel::Monomachine })
		{
			const auto log = scratch() / "model.log";
			std::filesystem::remove(log);
			const auto helper = writeHelper("model.sh", "cat >> '" + log.string() + "'");
			{
				mdJucePlugin::NativePanelKeylight light(model, helper);
				light.setFocused(true);
				assert(waitForLines(log, 1));
			}
			assert(lines(log).front() + "\n" == mdJucePlugin::keylightShowMessage(model));
		}
	}

	void testNoHelperIsHarmless()
	{
		mdJucePlugin::NativePanelKeylight none(md::MachineModel::Machinedrum, "");
		assert(!none.enabled());
		none.setFocused(true);
		none.setFocused(false);
		mdJucePlugin::NativePanelKeylight missing(md::MachineModel::Machinedrum,
			(scratch() / "does-not-exist").string());
		missing.setFocused(true);
		assert(!missing.helperRunning());
		missing.setFocused(false);
	}

	void testDisabledByEitherVariable()
	{
		const auto log = scratch() / "disabled.log";
		const auto helper = writeHelper("disabled.sh", "cat >> '" + log.string() + "'");
		for(const char* name : { "ELEKTREMU_KEYLIGHT", "DIGIEMU_KEYLIGHT" })
		{
			std::filesystem::remove(log);
			::setenv(name, "0", 1);
			assert(mdJucePlugin::NativePanelKeylight::disabledByEnvironment());
			{
				mdJucePlugin::NativePanelKeylight light(md::MachineModel::Machinedrum, helper);
				assert(!light.enabled());
				light.setFocused(true);
				assert(!light.helperRunning());
			}
			::unsetenv(name);
			assert(!std::filesystem::exists(log));
		}
		::setenv("ELEKTREMU_KEYLIGHT", "1", 1);
		assert(!mdJucePlugin::NativePanelKeylight::disabledByEnvironment());
		::unsetenv("ELEKTREMU_KEYLIGHT");
	}

	void testHelperFromEnvironment()
	{
		const auto log = scratch() / "env.log";
		std::filesystem::remove(log);
		::setenv("ELEKTREMU_KEYLIGHT_HELPER",
			writeHelper("env.sh", "cat >> '" + log.string() + "'").c_str(), 1);
		{
			mdJucePlugin::NativePanelKeylight light(md::MachineModel::Machinedrum);
			assert(light.enabled());
			light.setFocused(true);
			assert(waitForLines(log, 1));
		}
		::unsetenv("ELEKTREMU_KEYLIGHT_HELPER");
		mdJucePlugin::NativePanelKeylight absent(md::MachineModel::Machinedrum);
		assert(!absent.enabled());
	}

	void testADeadHelperNeverHurtsThePanel()
	{
		// Exits at once: later writes meet a closed pipe and must not raise SIGPIPE.
		const auto helper = writeHelper("dies.sh", "exit 0");
		mdJucePlugin::NativePanelKeylight light(md::MachineModel::Monomachine, helper);
		light.setFocused(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		for(int i = 0; i < 5; ++i)
		{
			light.setFocused(false);
			light.setFocused(true);
		}
		light.setFocused(false);
		assert(!light.helperRunning());
	}
}

int main()
{
	testMachinedrumMapIsExact();
	testMonomachineMapIsExact();
	testTheModelsDifferWhereTheirKeysDo();
	testUnmappedKeysStayDark();
	testMessages();
	testFocusInPaintsFocusOutRestoresAndClosingQuits();
	testEachModelSendsItsOwnMap();
	testNoHelperIsHarmless();
	testDisabledByEitherVariable();
	testHelperFromEnvironment();
	testADeadHelperNeverHurtsThePanel();
	testEveryKeyThatActsIsLit();
	std::filesystem::remove_all(scratch());
	std::cout << "native panel keylight passed\n";
	return 0;
}
