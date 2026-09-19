#pragma once

#include <bitset>
#include <cstdint>
#include <memory>

namespace mdJucePlugin
{
	// Polled game controller input for the standalone panel. SDL stays behind this
	// interface so the editor only sees a plain state snapshot. Buttons use SDL's
	// positional naming: South is Cross on a DualShock 4, East is Circle.
	class Gamepad
	{
	public:
		enum class Button : uint8_t
		{
			South,
			East,
			West,
			North,
			Back,
			Guide,
			Start,
			LeftStick,
			RightStick,
			LeftShoulder,
			RightShoulder,
			DpadUp,
			DpadDown,
			DpadLeft,
			DpadRight,
			Touchpad,

			Count
		};

		struct State
		{
			bool connected = false;
			std::bitset<static_cast<size_t>(Button::Count)> buttons;
			float leftX = 0.0f, leftY = 0.0f;
			float rightX = 0.0f, rightY = 0.0f;
			float leftTrigger = 0.0f, rightTrigger = 0.0f;	// 0..1
			bool touching = false;
			float touchX = 0.0f, touchY = 0.0f;				// 0..1, first finger
			// Rotation rate in radians per second, as SDL reports it: x pitches forward/back, y yaws, z rolls
			// left/right. All zero on a controller without a gyro.
			float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;

			bool pressed(const Button _button) const { return buttons.test(static_cast<size_t>(_button)); }
		};

		Gamepad();
		~Gamepad();

		Gamepad(const Gamepad&) = delete;
		Gamepad& operator = (const Gamepad&) = delete;

		// False when SDL could not initialise its gamepad subsystem.
		bool isAvailable() const;

		// Updates controller state on the calling thread. Opens the first connected
		// controller and closes it again when it disappears.
		State poll();

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
