#include "mdGamepad.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#if MD_GAMEPAD_SDL3
#include <SDL3/SDL.h>
#endif

namespace mdJucePlugin
{
#if MD_GAMEPAD_SDL3
	namespace
	{
		float axis(SDL_Gamepad* const _pad, const SDL_GamepadAxis _axis)
		{
			return static_cast<float>(SDL_GetGamepadAxis(_pad, _axis)) / 32767.0f;
		}
	}

	struct Gamepad::Impl
	{
		struct Pad
		{
			SDL_Gamepad* gamepad = nullptr;
			SDL_JoystickID id = 0;
			Gamepad::State last;
		};

		bool initialised = false;
		std::vector<Pad> pads;
		SDL_JoystickID active = 0;

		void openNew()
		{
			int count = 0;
			SDL_JoystickID* const ids = SDL_GetGamepads(&count);
			for(int i = 0; ids && i < count; ++i)
			{
				const auto open = std::find_if(pads.begin(), pads.end(), [&](const Pad& _p) { return _p.id == ids[i]; });
				if(open != pads.end())
					continue;
				if(auto* const gamepad = SDL_OpenGamepad(ids[i]))
				{
					for(const auto sensor : { SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL })
						if(SDL_GamepadHasSensor(gamepad, sensor))
							SDL_SetGamepadSensorEnabled(gamepad, sensor, true);
					pads.push_back({ gamepad, ids[i], {} });
				}
			}
			SDL_free(ids);
		}

		void closeGone()
		{
			for(auto it = pads.begin(); it != pads.end();)
			{
				if(SDL_GamepadConnected(it->gamepad))
				{
					++it;
					continue;
				}
				SDL_CloseGamepad(it->gamepad);
				it = pads.erase(it);
			}
		}
	};

	namespace
	{
		Gamepad::State readPad(SDL_Gamepad* const _pad, const SDL_JoystickID _id)
		{
			auto* const pad = _pad;
			Gamepad::State state;
			static constexpr SDL_GamepadButton g_buttons[] =
			{
				SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
				SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
				SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
				SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
				SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
				SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
				SDL_GAMEPAD_BUTTON_TOUCHPAD
			};

			state.connected = true;
			state.padId = static_cast<uint32_t>(_id);
			state.nintendoLabels = SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A;
			for(size_t i = 0; i < std::size(g_buttons); ++i)
				state.buttons.set(i, SDL_GetGamepadButton(pad, g_buttons[i]));

			state.leftX = axis(pad, SDL_GAMEPAD_AXIS_LEFTX);
			state.leftY = axis(pad, SDL_GAMEPAD_AXIS_LEFTY);
			state.rightX = axis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
			state.rightY = axis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
			state.leftTrigger = axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
			state.rightTrigger = axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

			if(SDL_GetNumGamepadTouchpads(pad) > 0)
			{
				bool down = false;
				float x = 0.0f, y = 0.0f, pressure = 0.0f;
				if(SDL_GetGamepadTouchpadFinger(pad, 0, 0, &down, &x, &y, &pressure))
				{
					state.touching = down;
					state.touchX = x;
					state.touchY = y;
				}
			}

			float gyro[3] = {};
			if(SDL_GamepadSensorEnabled(pad, SDL_SENSOR_GYRO)
				&& SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro, 3))
			{
				state.gyroX = gyro[0];
				state.gyroY = gyro[1];
				state.gyroZ = gyro[2];
			}

			float accel[3] = {};
			if(SDL_GamepadSensorEnabled(pad, SDL_SENSOR_ACCEL)
				&& SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, accel, 3))
			{
				state.hasAccel = true;
				state.accelX = accel[0];
				state.accelY = accel[1];
				state.accelZ = accel[2];
			}
			return state;
		}

		// Any input that means someone is using this controller, rather than it lying on the desk.
		bool inUse(const Gamepad::State& _now, const Gamepad::State& _before)
		{
			const auto moved = [](const float _a, const float _b) { return std::abs(_a - _b) > 0.25f; };
			return _now.buttons != _before.buttons || _now.touching
				|| moved(_now.leftX, _before.leftX) || moved(_now.leftY, _before.leftY)
				|| moved(_now.rightX, _before.rightX) || moved(_now.rightY, _before.rightY)
				|| moved(_now.leftTrigger, _before.leftTrigger) || moved(_now.rightTrigger, _before.rightTrigger);
		}
	}

	Gamepad::Gamepad() : m_impl(std::make_unique<Impl>())
	{
		// Only the gamepad subsystem: never video, which would compete with JUCE for
		// the window system. There is no SDL window, so controller input must not
		// depend on SDL focus, and SDL must leave the host's signal handlers alone.
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
		m_impl->initialised = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
	}

	Gamepad::~Gamepad()
	{
		for(const auto& pad : m_impl->pads)
			SDL_CloseGamepad(pad.gamepad);
		if(m_impl->initialised)
			SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	}

	bool Gamepad::isAvailable() const
	{
		return m_impl->initialised;
	}

	Gamepad::State Gamepad::poll()
	{
		State state;
		if(!m_impl->initialised)
			return state;

		// Pumps device add/remove and input without draining SDL's event queue.
		SDL_UpdateGamepads();
		m_impl->closeGone();
		m_impl->openNew();
		if(m_impl->pads.empty())
			return state;

		// Follow whichever controller was used last.
		for(auto& pad : m_impl->pads)
		{
			const auto now = readPad(pad.gamepad, pad.id);
			if(inUse(now, pad.last))
				m_impl->active = pad.id;
			pad.last = now;
		}
		const auto active = std::find_if(m_impl->pads.begin(), m_impl->pads.end(),
			[&](const Impl::Pad& _p) { return _p.id == m_impl->active; });
		return active != m_impl->pads.end() ? active->last : m_impl->pads.front().last;
	}
#else
	struct Gamepad::Impl {};

	Gamepad::Gamepad() : m_impl(std::make_unique<Impl>()) {}
	Gamepad::~Gamepad() = default;
	bool Gamepad::isAvailable() const { return false; }
	Gamepad::State Gamepad::poll() { return {}; }
#endif
}
