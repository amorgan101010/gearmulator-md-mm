#include "mdNativePanelKeylight.h"

#include "mdKeyboardControl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#define GM_KEYLIGHT_POSIX 1
#endif

namespace mdJucePlugin
{
	namespace
	{
		using Key = Rml::Input::KeyIdentifier;

		struct CandidateKey
		{
			Key key;
			const char* keysym;
		};

		// Every key of the 75% keyboard the helper knows (emu/keylight.py LAYOUT) that a
		// panel can receive as a key event, in the order of the keyboard. Modifiers are
		// not events of their own here: see keylightKeys().
		constexpr CandidateKey g_candidates[] =
		{
			{ Key::KI_ESCAPE, "Escape" },
			{ Key::KI_F1, "F1" }, { Key::KI_F2, "F2" }, { Key::KI_F3, "F3" }, { Key::KI_F4, "F4" },
			{ Key::KI_F5, "F5" }, { Key::KI_F6, "F6" }, { Key::KI_F7, "F7" }, { Key::KI_F8, "F8" },
			{ Key::KI_F9, "F9" }, { Key::KI_F10, "F10" }, { Key::KI_F11, "F11" }, { Key::KI_F12, "F12" },
			{ Key::KI_SNAPSHOT, "Print" }, { Key::KI_DELETE, "Delete" },
			{ Key::KI_OEM_3, "grave" },
			{ Key::KI_1, "1" }, { Key::KI_2, "2" }, { Key::KI_3, "3" }, { Key::KI_4, "4" },
			{ Key::KI_5, "5" }, { Key::KI_6, "6" }, { Key::KI_7, "7" }, { Key::KI_8, "8" },
			{ Key::KI_9, "9" }, { Key::KI_0, "0" },
			{ Key::KI_OEM_MINUS, "minus" }, { Key::KI_OEM_PLUS, "equal" },
			{ Key::KI_BACK, "BackSpace" }, { Key::KI_PRIOR, "Prior" },
			{ Key::KI_TAB, "Tab" },
			{ Key::KI_Q, "q" }, { Key::KI_W, "w" }, { Key::KI_E, "e" }, { Key::KI_R, "r" },
			{ Key::KI_T, "t" }, { Key::KI_Y, "y" }, { Key::KI_U, "u" }, { Key::KI_I, "i" },
			{ Key::KI_O, "o" }, { Key::KI_P, "p" },
			{ Key::KI_OEM_4, "bracketleft" }, { Key::KI_OEM_6, "bracketright" },
			{ Key::KI_OEM_5, "backslash" }, { Key::KI_NEXT, "Next" },
			{ Key::KI_CAPITAL, "Caps_Lock" },
			{ Key::KI_A, "a" }, { Key::KI_S, "s" }, { Key::KI_D, "d" }, { Key::KI_F, "f" },
			{ Key::KI_G, "g" }, { Key::KI_H, "h" }, { Key::KI_J, "j" }, { Key::KI_K, "k" },
			{ Key::KI_L, "l" },
			{ Key::KI_OEM_1, "semicolon" }, { Key::KI_OEM_7, "apostrophe" },
			{ Key::KI_RETURN, "Return" }, { Key::KI_HOME, "Home" },
			{ Key::KI_Z, "z" }, { Key::KI_X, "x" }, { Key::KI_C, "c" }, { Key::KI_V, "v" },
			{ Key::KI_B, "b" }, { Key::KI_N, "n" }, { Key::KI_M, "m" },
			{ Key::KI_OEM_COMMA, "comma" }, { Key::KI_OEM_PERIOD, "period" },
			{ Key::KI_OEM_2, "slash" },
			{ Key::KI_UP, "Up" }, { Key::KI_END, "End" },
			{ Key::KI_SPACE, "space" },
			{ Key::KI_LEFT, "Left" }, { Key::KI_DOWN, "Down" }, { Key::KI_RIGHT, "Right" },
		};

		const char* groupOf(const md::PanelControl _control)
		{
			using C = md::PanelControl;
			switch(_control)
			{
			case C::Trigger1: case C::Trigger2: case C::Trigger3: case C::Trigger4:
			case C::Trigger5: case C::Trigger6: case C::Trigger7: case C::Trigger8:
			case C::Trigger9: case C::Trigger10: case C::Trigger11: case C::Trigger12:
			case C::Trigger13: case C::Trigger14: case C::Trigger15: case C::Trigger16:
				return "steps";
			case C::Track1: case C::Track2: case C::Track3:
			case C::Track4: case C::Track5: case C::Track6:
				return "tracks";
			case C::Play: case C::Stop: case C::Record:
				return "transport";
			case C::Up: case C::Down: case C::Left: case C::Right:
			case C::Enter: case C::Exit:
			case C::DataPageForward: case C::DataPageBackward:
				return "nav";
			case C::BankGroup: case C::BankA: case C::BankB: case C::BankC: case C::BankD:
				return "banks";
			case C::Tempo: case C::Kit: case C::Scale: case C::PatternSong:
			case C::SynthesisEffectsRouting: case C::TrigSelect: case C::SongEnable:
			case C::ClassicExtended:
				return "menus";
			case C::Function:
				return "mod";
			}
			return nullptr; // A control with no group is a bug a test must catch.
		}

		// What a key did when it was pressed, as the native panel's actions see it.
		class ProbeActions final : public PanelKeyboardActions
		{
		public:
			explicit ProbeActions(const md::MachineModel _model) : m_model(_model) {}

			void keyboardPressControl(md::PanelControl _control, bool, bool) override
			{
				if(md::panelPacket(m_model, _control))
					setGroup(groupOf(_control));
			}
			void keyboardReleaseControl(md::PanelControl) override {}
			bool keyboardEncoderAvailable(md::PanelEncoder _encoder) const override
			{
				return md::panelEncoderCommand(m_model, _encoder).has_value();
			}
			void keyboardSelectEncoder(std::optional<md::PanelEncoder> _encoder) override
			{
				if(_encoder && keyboardEncoderAvailable(*_encoder))
					setGroup("knobs");
			}
			void keyboardPressEncoder(md::PanelEncoder) override {}
			void keyboardReleaseEncoder(md::PanelEncoder) override {}
			void keyboardTurnEncoder(md::PanelEncoder, float) override { setGroup("knobs"); }
			void keyboardClickEncoder(md::PanelEncoder) override {}
			bool keyboardHandlesMachinedrumPageKeys() const override
			{
				return m_model == md::MachineModel::Machinedrum;
			}
			void keyboardMachinedrumPageKey(int, bool) override { setGroup("nav"); }
			void keyboardReleaseAllPanelInputs() override { m_releasedAll = true; }

			const char* group() const { return m_group; }
			bool releasedAll() const { return m_releasedAll; }

		private:
			void setGroup(const char* _group)
			{
				if(!m_group)
					m_group = _group;
			}
			md::MachineModel m_model;
			const char* m_group = nullptr;
			bool m_releasedAll = false;
		};

		const char* probe(const md::MachineModel _model, const Key _key)
		{
			PanelKeyboardControl keyboard;
			ProbeActions actions(_model);
			// The turn keys only act on a knob that is held, so hold one, as a player does.
			if(_key == Key::KI_OEM_MINUS || _key == Key::KI_OEM_PLUS
				|| _key == Key::KI_OEM_4 || _key == Key::KI_OEM_6)
			{
				ProbeActions held(_model);
				keyboard.handleKey(static_cast<int>(Key::KI_A), true, false, false, 0.0, held);
				keyboard.handleKey(static_cast<int>(_key), true, false, false, 1.0, actions);
				return actions.group();
			}
			keyboard.handleKey(static_cast<int>(_key), true, false, false, 0.0, actions);
			if(_key == Key::KI_DELETE && actions.releasedAll())
				return "mod";
			return actions.group();
		}

		const char* controlKeyGroup(const md::MachineModel _model)
		{
			// Ctrl is FUNCTION: held with any key, it presses the model's Function button.
			PanelKeyboardControl keyboard;
			ProbeActions actions(_model);
			keyboard.handleKey(static_cast<int>(Key::KI_UNKNOWN), true, false, true, 0.0, actions);
			return actions.group();
		}
	}

	std::vector<KeylightKey> keylightKeys(const md::MachineModel _model)
	{
		std::vector<KeylightKey> keys;
		for(const auto& candidate : g_candidates)
			if(const char* const group = probe(_model, candidate.key))
				keys.push_back({ candidate.keysym, group });
		// Shift latches whatever is pressed with it; Ctrl is Function.
		keys.push_back({ "Shift_L", "mod" });
		keys.push_back({ "Shift_R", "mod" });
		if(const char* const group = controlKeyGroup(_model))
		{
			keys.push_back({ "Control_L", group });
			keys.push_back({ "Control_R", group });
		}
		return keys;
	}

	std::string keylightShowMessage(const md::MachineModel _model)
	{
		std::string line = "{\"show\":{";
		bool first = true;
		for(const auto& key : keylightKeys(_model))
		{
			if(!first)
				line += ',';
			first = false;
			line += '"' + key.keysym + "\":\"" + key.group + '"';
		}
		return line + "}}\n";
	}

	std::string keylightRestoreMessage() { return "{\"restore\":true}\n"; }
	std::string keylightQuitMessage() { return "{\"quit\":true}\n"; }

	bool NativePanelKeylight::disabledByEnvironment()
	{
		for(const char* const name : { "ELEKTREMU_KEYLIGHT", "DIGIEMU_KEYLIGHT" })
		{
			const char* const value = std::getenv(name);
			if(value && std::strcmp(value, "0") == 0)
				return true;
		}
		return false;
	}

	NativePanelKeylight::NativePanelKeylight(const md::MachineModel _model)
		: NativePanelKeylight(_model, [] {
			const char* const helper = std::getenv("ELEKTREMU_KEYLIGHT_HELPER");
			return std::string(helper ? helper : "");
		}())
	{
	}

	NativePanelKeylight::NativePanelKeylight(const md::MachineModel _model, std::string _helper)
		: m_model(_model)
		, m_helper(disabledByEnvironment() ? std::string() : std::move(_helper))
	{
#ifndef GM_KEYLIGHT_POSIX
		m_helper.clear();
#endif
	}

	NativePanelKeylight::~NativePanelKeylight()
	{
		stop(true);
	}

#ifdef GM_KEYLIGHT_POSIX
	namespace
	{
		double nowSeconds()
		{
			return std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

	bool NativePanelKeylight::start()
	{
		if(helperRunning())
			return true;
		reap();
		if(m_pid > 0 || nowSeconds() < m_retryAt)
			return false; // a helper told to stop is still on its way out
		m_retryAt = nowSeconds() + 5.0; // a helper that cannot start is not retried in a loop
		std::signal(SIGPIPE, SIG_IGN); // a helper that died must not take the panel with it

		int fds[2];
		if(::pipe(fds) != 0) // not pipe2: macOS has none
			return false;
		for(const int fd : fds)
			::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
		posix_spawn_file_actions_t actions;
		if(::posix_spawn_file_actions_init(&actions) != 0)
		{
			::close(fds[0]);
			::close(fds[1]);
			return false;
		}
		::posix_spawn_file_actions_adddup2(&actions, fds[0], 0);
		char* argv[] = { const_cast<char*>(m_helper.c_str()), nullptr };
		pid_t pid = -1;
		const int result = ::posix_spawn(&pid, m_helper.c_str(), &actions, nullptr, argv, environ);
		::posix_spawn_file_actions_destroy(&actions);
		::close(fds[0]);
		if(result != 0)
		{
			::close(fds[1]);
			return false;
		}
		::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL) | O_NONBLOCK);
		m_fd = fds[1];
		m_pid = static_cast<int>(pid);
		return true;
	}

	bool NativePanelKeylight::send(const std::string& _line)
	{
		if(m_fd < 0)
			return false;
		const auto written = ::write(m_fd, _line.data(), _line.size());
		if(written == static_cast<ssize_t>(_line.size()))
			return true;
		// A broken or backed-up pipe: close it, which tells the helper to restore.
		stop(false);
		return false;
	}

	void NativePanelKeylight::reap()
	{
		if(m_pid <= 0)
			return;
		int status = 0;
		const pid_t done = ::waitpid(m_pid, &status, WNOHANG);
		if(done == m_pid || (done < 0 && errno == ECHILD))
		{
			m_pid = -1;
			if(m_fd >= 0)
			{
				::close(m_fd);
				m_fd = -1;
			}
		}
	}

	void NativePanelKeylight::stop(const bool _wait)
	{
		if(m_fd >= 0)
		{
			// Best effort: end of input restores too, which is what a crash gets.
			const auto quit = keylightQuitMessage();
			const auto ignored = ::write(m_fd, quit.data(), quit.size());
			(void)ignored;
			::close(m_fd);
			m_fd = -1;
		}
		// Give the helper a moment to put the lights back, but never hold the panel's exit.
		for(int i = 0; _wait && i < 150 && m_pid > 0; ++i)
		{
			reap();
			if(m_pid > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if(_wait)
			m_pid = -1;
	}

	void NativePanelKeylight::setFocused(const bool _focused)
	{
		if(m_helper.empty())
		{
			m_focused = _focused;
			return;
		}
		reap();
		const bool helperLost = _focused && !helperRunning();
		if(_focused == m_focused && !helperLost)
			return;
		m_focused = _focused;
		if(_focused)
		{
			if(start())
				send(keylightShowMessage(m_model));
		}
		else if(helperRunning())
		{
			send(keylightRestoreMessage());
		}
	}
#else
	bool NativePanelKeylight::start() { return false; }
	bool NativePanelKeylight::send(const std::string&) { return false; }
	void NativePanelKeylight::reap() {}
	void NativePanelKeylight::stop(bool) {}
	void NativePanelKeylight::setFocused(const bool _focused) { m_focused = _focused; }
#endif
}
