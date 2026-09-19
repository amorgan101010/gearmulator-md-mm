#include "mdGamepad.h"

#include <iterator>

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
		bool initialised = false;
		SDL_Gamepad* pad = nullptr;

		void openFirst()
		{
			int count = 0;
			SDL_JoystickID* const ids = SDL_GetGamepads(&count);
			if(ids && count > 0)
				pad = SDL_OpenGamepad(ids[0]);
			SDL_free(ids);
			if(pad && SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO))
				SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
		}
	};

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
		if(m_impl->pad)
			SDL_CloseGamepad(m_impl->pad);
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

		if(m_impl->pad && !SDL_GamepadConnected(m_impl->pad))
		{
			SDL_CloseGamepad(m_impl->pad);
			m_impl->pad = nullptr;
		}
		if(!m_impl->pad)
			m_impl->openFirst();

		auto* const pad = m_impl->pad;
		if(!pad)
			return state;

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
		return state;
	}
#else
	struct Gamepad::Impl {};

	Gamepad::Gamepad() : m_impl(std::make_unique<Impl>()) {}
	Gamepad::~Gamepad() = default;
	bool Gamepad::isAvailable() const { return false; }
	Gamepad::State Gamepad::poll() { return {}; }
#endif
}
