#include "mdKeyboardControl.h"

#include <algorithm>
#include <array>

namespace mdJucePlugin
{
	namespace
	{
		using Key = Rml::Input::KeyIdentifier;
		struct KeyboardButton
		{
			Key key;
			md::PanelControl control;
		};

		// Keep this as the one computer-keyboard binding table for both editors.
		constexpr KeyboardButton g_keyboardButtons[] =
		{
			{ Key::KI_F1, md::PanelControl::Trigger1 }, { Key::KI_F2, md::PanelControl::Trigger2 },
			{ Key::KI_F3, md::PanelControl::Trigger3 }, { Key::KI_F4, md::PanelControl::Trigger4 },
			{ Key::KI_F5, md::PanelControl::Trigger5 }, { Key::KI_F6, md::PanelControl::Trigger6 },
			{ Key::KI_F7, md::PanelControl::Trigger7 }, { Key::KI_F8, md::PanelControl::Trigger8 },
			{ Key::KI_1, md::PanelControl::Trigger9 }, { Key::KI_2, md::PanelControl::Trigger10 },
			{ Key::KI_3, md::PanelControl::Trigger11 }, { Key::KI_4, md::PanelControl::Trigger12 },
			{ Key::KI_5, md::PanelControl::Trigger13 }, { Key::KI_6, md::PanelControl::Trigger14 },
			{ Key::KI_7, md::PanelControl::Trigger15 }, { Key::KI_8, md::PanelControl::Trigger16 },
			{ Key::KI_UP, md::PanelControl::Up }, { Key::KI_DOWN, md::PanelControl::Down },
			{ Key::KI_LEFT, md::PanelControl::Left }, { Key::KI_RIGHT, md::PanelControl::Right },
			{ Key::KI_RETURN, md::PanelControl::Enter }, { Key::KI_BACK, md::PanelControl::Exit },
			{ Key::KI_Q, md::PanelControl::Track1 }, { Key::KI_W, md::PanelControl::Track2 },
			{ Key::KI_E, md::PanelControl::Track3 }, { Key::KI_R, md::PanelControl::Track4 },
			{ Key::KI_T, md::PanelControl::Track5 }, { Key::KI_Y, md::PanelControl::Track6 },
			{ Key::KI_SPACE, md::PanelControl::Play }, { Key::KI_END, md::PanelControl::Stop },
			{ Key::KI_HOME, md::PanelControl::Record },
			{ Key::KI_PRIOR, md::PanelControl::DataPageBackward }, { Key::KI_NEXT, md::PanelControl::DataPageForward },
			{ Key::KI_F9, md::PanelControl::BankA }, { Key::KI_F10, md::PanelControl::BankB },
			{ Key::KI_F11, md::PanelControl::BankC }, { Key::KI_F12, md::PanelControl::BankD },
			{ Key::KI_OEM_3, md::PanelControl::BankGroup },
			{ Key::KI_K, md::PanelControl::Kit }, { Key::KI_P, md::PanelControl::PatternSong },
			{ Key::KI_U, md::PanelControl::Tempo }, { Key::KI_I, md::PanelControl::Scale },
			{ Key::KI_O, md::PanelControl::SynthesisEffectsRouting },
			{ Key::KI_B, md::PanelControl::TrigSelect }, { Key::KI_N, md::PanelControl::ClassicExtended },
			{ Key::KI_M, md::PanelControl::SongEnable },
		};

		constexpr Key g_keyboardEncoders[] =
		{
			Key::KI_A, Key::KI_S, Key::KI_D, Key::KI_F,
			Key::KI_Z, Key::KI_X, Key::KI_C, Key::KI_V,
			Key::KI_G, // LEVEL
			Key::KI_H, // Machinedrum SOUND wheel
		};
		constexpr double g_keyReleaseGraceMilliseconds = 50.0;

		md::PanelEncoder encoderAt(size_t _index)
		{
			if(_index < std::size(g_keyboardEncoders) - 2)
				return static_cast<md::PanelEncoder>(
					static_cast<unsigned>(md::PanelEncoder::DataEntryA) + _index);
			return _index == std::size(g_keyboardEncoders) - 2
				? md::PanelEncoder::Level : md::PanelEncoder::SoundSelection;
		}
	}

	bool PanelKeyboardControl::handleKey(const int _key, const bool _down,
		const bool _shift, const bool _control, const double _nowMilliseconds,
		PanelKeyboardActions& _actions)
	{
		if(_control != m_functionHeld)
			setFunctionHeld(_control, _shift, _actions);
		if(_key == static_cast<int>(Key::KI_UNKNOWN))
			return false;

		const auto key = static_cast<Key>(_key);
		const auto pending = std::find_if(m_pendingReleases.begin(), m_pendingReleases.end(),
			[key](const auto& _item) { return _item.first == key; });
		if(!_down)
		{
			const bool pressTurn = key == Key::KI_OEM_4 || key == Key::KI_OEM_6;
			if(pressTurn || std::find(m_heldKeys.begin(), m_heldKeys.end(), key) != m_heldKeys.end())
			{
				if(pending == m_pendingReleases.end())
					m_pendingReleases.emplace_back(key, _nowMilliseconds + g_keyReleaseGraceMilliseconds);
				return true;
			}
		}
		else if(pending != m_pendingReleases.end())
		{
			m_pendingReleases.erase(pending);
		}
		return handleKeyWithoutModifiers(key, _down, _shift, _actions);
	}

	void PanelKeyboardControl::serviceReleases(const double _nowMilliseconds,
		PanelKeyboardActions& _actions)
	{
		for(auto it = m_pendingReleases.begin(); it != m_pendingReleases.end();)
		{
			if(it->second > _nowMilliseconds)
			{
				++it;
				continue;
			}
			const auto key = it->first;
			it = m_pendingReleases.erase(it);
			handleKeyWithoutModifiers(key, false, false, _actions);
			it = m_pendingReleases.begin();
		}
	}

	void PanelKeyboardControl::setFunctionHeld(const bool _held, const bool _shift,
		PanelKeyboardActions& _actions)
	{
		m_functionHeld = _held;
		if(_held)
		{
			m_heldControls.push_back(md::PanelControl::Function);
			_actions.keyboardPressControl(md::PanelControl::Function, _shift, false);
		}
		else
		{
			_actions.keyboardReleaseControl(md::PanelControl::Function);
			const auto held = std::find(m_heldControls.begin(), m_heldControls.end(), md::PanelControl::Function);
			if(held != m_heldControls.end())
				m_heldControls.erase(held);
		}
	}

	void PanelKeyboardControl::releaseFunction(PanelKeyboardActions& _actions)
	{
		if(m_functionHeld)
			setFunctionHeld(false, false, _actions);
	}

	void PanelKeyboardControl::reset(PanelKeyboardActions& _actions)
	{
		releaseAll(_actions);
	}

	void PanelKeyboardControl::releaseAll(PanelKeyboardActions& _actions)
	{
		for(auto control : m_heldControls)
			_actions.keyboardReleaseControl(control);
		m_heldControls.clear();
		m_heldKeys.clear();
		m_pendingReleases.clear();
		if(m_encoder && m_pressTurnHeld)
			_actions.keyboardReleaseEncoder(encoderAt(*m_encoder));
		m_encoder.reset();
		m_encoderTurned = false;
		m_pressTurnHeld = false;
		m_functionHeld = false;
		_actions.keyboardSelectEncoder(std::nullopt);
		_actions.keyboardReleaseAllPanelInputs();
	}

	bool PanelKeyboardControl::handleKeyWithoutModifiers(const Key _key,
		const bool _down, const bool _shift, PanelKeyboardActions& _actions)
	{
		if(_key == Key::KI_DELETE)
		{
			if(_down)
				releaseAll(_actions);
			return true;
		}

		const bool pressTurn = _key == Key::KI_OEM_4 || _key == Key::KI_OEM_6;
		const float turn = _key == Key::KI_OEM_MINUS || _key == Key::KI_OEM_4 ? -1.0f
			: _key == Key::KI_OEM_PLUS || _key == Key::KI_OEM_6 ? 1.0f : 0.0f;
		if(turn != 0.0f)
		{
			if(_down && m_encoder)
			{
				const auto encoder = encoderAt(*m_encoder);
				if(!_actions.keyboardEncoderAvailable(encoder))
					return true;
				if(pressTurn && !m_pressTurnHeld)
				{
					_actions.keyboardPressEncoder(encoder);
					m_pressTurnHeld = true;
				}
				_actions.keyboardTurnEncoder(encoder, turn);
				m_encoderTurned = true;
			}
			else if(!_down && pressTurn && m_encoder && m_pressTurnHeld)
			{
				_actions.keyboardReleaseEncoder(encoderAt(*m_encoder));
				m_pressTurnHeld = false;
			}
			return true;
		}

		const auto held = std::find(m_heldKeys.begin(), m_heldKeys.end(), _key);
		if(_down && held != m_heldKeys.end())
			return true;
		if(!_down)
		{
			if(held == m_heldKeys.end())
				return false;
			m_heldKeys.erase(held);
		}

		for(size_t i = 0; i < std::size(g_keyboardEncoders); ++i)
		{
			if(g_keyboardEncoders[i] != _key)
				continue;
			const auto encoder = encoderAt(i);
			if(_down)
			{
				m_heldKeys.push_back(_key);
				m_encoder = i;
				m_encoderTurned = false;
				_actions.keyboardSelectEncoder(encoder);
			}
			else if(m_encoder == i)
			{
				if(!m_encoderTurned && _actions.keyboardEncoderAvailable(encoder))
					_actions.keyboardClickEncoder(encoder);
				if(m_pressTurnHeld)
					_actions.keyboardReleaseEncoder(encoder);
				m_pressTurnHeld = false;
				m_encoder.reset();
				_actions.keyboardSelectEncoder(std::nullopt);
			}
			return true;
		}

		if(_actions.keyboardHandlesMachinedrumPageKeys()
			&& (_key == Key::KI_PRIOR || _key == Key::KI_NEXT))
		{
			if(_down)
				m_heldKeys.push_back(_key);
			const int direction = _key == Key::KI_NEXT ? 1 : -1;
			_actions.keyboardMachinedrumPageKey(direction, _down);
			return true;
		}

		for(const auto& button : g_keyboardButtons)
		{
			if(button.key != _key)
				continue;
			if(_down)
			{
				m_heldKeys.push_back(_key);
				m_heldControls.push_back(button.control);
				_actions.keyboardPressControl(button.control, _shift, true);
			}
			else
			{
				_actions.keyboardReleaseControl(button.control);
				const auto control = std::find(m_heldControls.begin(), m_heldControls.end(), button.control);
				if(control != m_heldControls.end())
					m_heldControls.erase(control);
			}
			return true;
		}
		return false;
	}
}
