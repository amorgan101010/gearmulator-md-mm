#pragma once

#include "mdLib/mdtypes.h"

#include "juce_data_structures/juce_data_structures.h"

#include <array>

namespace mdJucePlugin::gamepadAxes
{
	// The controller's analog extras: the touchpad (finger sliding) and the gyro (PS button held while tilting).
	// Each of their four axes turns one knob, chosen per axis in the settings.
	enum class Axis { TouchX, TouchY, TiltRoll, TiltPitch };

	// Target values as stored in the config.
	constexpr int g_targetFocusedTop = 0;		// top knob of the focused data entry knob's column
	constexpr int g_targetFocusedBottom = 1;	// bottom knob of that column
	constexpr int g_targetKnobA = 10;			// 10..17: data entry knobs A..H
	constexpr int g_targetLevel = 20;
	constexpr int g_targetOff = 99;

	constexpr int g_targets[] = { g_targetFocusedTop, g_targetFocusedBottom,
		10, 11, 12, 13, 14, 15, 16, 17, g_targetLevel, g_targetOff };
	constexpr int g_speedPercents[] = { 25, 50, 100, 200, 400 };

	// Face buttons: 0 follows the controller's own labels when it reports them, 1 goes by position (bottom
	// button acts, like Cross), 2 by Nintendo labels (A on the right acts). X-input controllers such as an
	// 8BitDo in Xbox mode report Xbox labels whatever is printed on them, so they need 2.
	constexpr const char* g_faceLayoutKey = "gamepadFaceLayout";
	constexpr int g_faceLayouts[] = { 0, 1, 2 };

	struct AxisInfo
	{
		Axis axis;
		const char* idPrefix;		// settings page element ids
		const char* targetKey;
		const char* functionKey;
		const char* invertKey;
		const char* speedKey;
		const char* absoluteKey;
	};

	constexpr AxisInfo g_axes[] =
	{
		{ Axis::TouchX,    "btPadTouchX",    "gamepadTouchXTarget",    "gamepadTouchXFunction",    "gamepadTouchXInvert",    "gamepadTouchXSpeed",    "gamepadTouchXAbsolute" },
		{ Axis::TouchY,    "btPadTouchY",    "gamepadTouchYTarget",    "gamepadTouchYFunction",    "gamepadTouchYInvert",    "gamepadTouchYSpeed",    "gamepadTouchYAbsolute" },
		{ Axis::TiltRoll,  "btPadTiltRoll",  "gamepadTiltRollTarget",  "gamepadTiltRollFunction",  "gamepadTiltRollInvert",  "gamepadTiltRollSpeed",  "gamepadTiltRollAbsolute" },
		{ Axis::TiltPitch, "btPadTiltPitch", "gamepadTiltPitchTarget", "gamepadTiltPitchFunction", "gamepadTiltPitchInvert", "gamepadTiltPitchSpeed", "gamepadTiltPitchAbsolute" },
	};

	inline bool isTilt(const Axis _axis) { return _axis == Axis::TiltRoll || _axis == Axis::TiltPitch; }

	// Sideways axes default to the column's top knob, the others to its bottom knob.
	inline int defaultTarget(const Axis _axis)
	{
		return _axis == Axis::TouchX || _axis == Axis::TiltRoll ? g_targetFocusedTop : g_targetFocusedBottom;
	}

	// Tilting holds FUNCTION on the Machinedrum, where FUNCTION + a DATA ENTRY knob changes that parameter on
	// every track. The Monomachine manual documents no such gesture, so it turns plainly there.
	inline bool defaultFunction(const Axis _axis, const md::MachineModel _model)
	{
		return isTilt(_axis) && _model == md::MachineModel::Machinedrum;
	}

	struct Settings
	{
		int target = g_targetOff;
		bool function = false;
		bool invert = false;
		int speedPercent = 100;
		// Absolute: a touchpad position or tilt angle is a knob value (bottom-left / tilted left and down = 0).
		// Otherwise movement nudges the knob, as turning it does.
		bool absolute = true;
	};

	inline Settings read(juce::PropertiesFile& _config, const AxisInfo& _info, const md::MachineModel _model)
	{
		Settings s;
		s.target = _config.getIntValue(_info.targetKey, defaultTarget(_info.axis));
		s.function = _config.getBoolValue(_info.functionKey, defaultFunction(_info.axis, _model));
		s.invert = _config.getBoolValue(_info.invertKey, false);
		s.speedPercent = _config.getIntValue(_info.speedKey, 100);
		s.absolute = _config.getBoolValue(_info.absoluteKey, true);
		return s;
	}
}
