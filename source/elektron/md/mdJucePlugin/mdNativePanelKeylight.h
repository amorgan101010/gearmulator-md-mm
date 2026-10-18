#pragma once

#include "mdLib/mdpanel.h"
#include "mdLib/mdtypes.h"

#include <string>
#include <vector>

namespace mdJucePlugin
{
	// One lit key of the computer keyboard: its X keysym ("F1", "grave", "Prior") and
	// its keymap group ("steps", "tracks", "knobs", "transport", "nav", "menus",
	// "banks", "pages", "mod"). Colours are the lighting helper's business.
	struct KeylightKey
	{
		std::string keysym;
		std::string group;
		bool operator==(const KeylightKey& _other) const
		{
			return keysym == _other.keysym && group == _other.group;
		}
	};

	// The keys that do something on a model, and in which group. It is read off
	// PanelKeyboardControl itself - each key is pressed on a fresh controller and
	// what reaches the panel is looked at - so the lights cannot drift from the
	// keys: a key that the model has no packet for stays dark.
	std::vector<KeylightKey> keylightKeys(md::MachineModel _model);

	// {"show":{"F1":"steps",...}} and {"restore":true}, one line each.
	std::string keylightShowMessage(md::MachineModel _model);
	std::string keylightRestoreMessage();
	std::string keylightQuitMessage();

	// Lights the physical keyboard while this panel's window is the active one.
	//
	// It starts a helper process (ELEKTREMU_KEYLIGHT_HELPER, run with no arguments)
	// the first time the panel is focused and talks to it over a pipe on its
	// stdin. The helper owns the keyboard; when this process ends, however it
	// ends, the pipe closes and the helper gives the keyboard its lights back.
	// With no helper, or with ELEKTREMU_KEYLIGHT=0 or DIGIEMU_KEYLIGHT=0, it does
	// nothing. A helper that fails or dies is never allowed to hurt the panel.
	class NativePanelKeylight
	{
	public:
		explicit NativePanelKeylight(md::MachineModel _model);
		NativePanelKeylight(md::MachineModel _model, std::string _helper);
		~NativePanelKeylight();
		NativePanelKeylight(const NativePanelKeylight&) = delete;
		NativePanelKeylight& operator=(const NativePanelKeylight&) = delete;

		// Report whether this panel has the focus; only changes are sent.
		void setFocused(bool _focused);
		bool enabled() const { return !m_helper.empty(); }
		bool focused() const { return m_focused; }
		bool helperRunning() const { return m_pid > 0 && m_fd >= 0; }

		static bool disabledByEnvironment();

	private:
		bool start();
		bool send(const std::string& _line);
		void stop(bool _wait);
		void reap();

		md::MachineModel m_model;
		std::string m_helper;
		bool m_focused = false;
		int m_fd = -1;
		int m_pid = -1; // a pid_t, kept out of this header so it compiles where there is none
		double m_retryAt = 0.0;
	};
}
