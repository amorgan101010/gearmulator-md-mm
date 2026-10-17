#pragma once

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	// Puts the standalone application's icon on the window _component is in,
	// for a taskbar that draws the window's own icon and not the launcher's.
	// The icon is the file icon.argb beside the executable: every size in one
	// file, each as width, height and then the ARGB pixels, in little-endian
	// 32-bit words. Does nothing in a host, without that file, or off X11.
	void applyStandaloneWindowIcon(juce::Component& _component);
}
