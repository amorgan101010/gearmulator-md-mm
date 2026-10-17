#pragma once

#include "mdLib/mdpanel.h"
#include "mdLib/mdtypes.h"
#include "RmlUi/Core/Input.h"

#include <optional>
#include <utility>
#include <vector>

namespace mdJucePlugin
{
	class PanelKeyboardActions
	{
	public:
		virtual ~PanelKeyboardActions() = default;
		virtual void keyboardPressControl(md::PanelControl _control, bool _shift,
			bool _momentaryBanks) = 0;
		virtual void keyboardReleaseControl(md::PanelControl _control) = 0;
		virtual bool keyboardEncoderAvailable(md::PanelEncoder _encoder) const = 0;
		virtual void keyboardSelectEncoder(std::optional<md::PanelEncoder> _encoder) = 0;
		virtual void keyboardPressEncoder(md::PanelEncoder _encoder) = 0;
		virtual void keyboardReleaseEncoder(md::PanelEncoder _encoder) = 0;
		virtual void keyboardTurnEncoder(md::PanelEncoder _encoder, float _direction) = 0;
		virtual void keyboardClickEncoder(md::PanelEncoder _encoder) = 0;
		virtual bool keyboardHandlesMachinedrumPageKeys() const = 0;
		virtual void keyboardMachinedrumPageKey(int _direction, bool _down) = 0;
		virtual void keyboardReleaseAllPanelInputs() = 0;
	};

	// The established MD/MM computer-keyboard map and held-key semantics, shared
	// by the plug-in editor and the hosted panel frontend.
	class PanelKeyboardControl
	{
	public:
		bool handleKey(int _key, bool _down, bool _shift, bool _control,
			double _nowMilliseconds, PanelKeyboardActions& _actions);
		void serviceReleases(double _nowMilliseconds, PanelKeyboardActions& _actions);
		void releaseFunction(PanelKeyboardActions& _actions);
		void reset(PanelKeyboardActions& _actions);
		bool functionHeld() const { return m_functionHeld; }
		std::optional<size_t> selectedEncoder() const { return m_encoder; }
		bool encoderTurned() const { return m_encoderTurned; }

	private:
		using Key = Rml::Input::KeyIdentifier;
		bool handleKeyWithoutModifiers(Key _key, bool _down, bool _shift,
			PanelKeyboardActions& _actions);
		void setFunctionHeld(bool _held, bool _shift, PanelKeyboardActions& _actions);
		void releaseAll(PanelKeyboardActions& _actions);

		std::vector<Key> m_heldKeys;
		std::vector<std::pair<Key, double>> m_pendingReleases;
		std::vector<md::PanelControl> m_heldControls;
		std::optional<size_t> m_encoder;
		bool m_encoderTurned = false;
		bool m_pressTurnHeld = false;
		bool m_functionHeld = false;
	};
}
