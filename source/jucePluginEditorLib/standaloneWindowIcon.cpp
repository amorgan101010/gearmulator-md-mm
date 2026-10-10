#include "standaloneWindowIcon.h"

#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace jucePluginEditorLib
{
	void applyStandaloneWindowIcon(juce::Component& _component)
	{
#if JUCE_LINUX || JUCE_BSD
		if(!juce::JUCEApplicationBase::isStandaloneApp())
			return;

		auto* peer = _component.getPeer();
		if(!peer)
			return;

		const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("icon.argb");
		juce::MemoryBlock bytes;
		if(!file.existsAsFile() || !file.loadFileAsData(bytes) || bytes.getSize() < 12)
			return;

		// A format 32 property is an array of longs, also where a long has 64 bits.
		std::vector<unsigned long> words(bytes.getSize() / 4);
		const auto* b = static_cast<const uint8_t*>(bytes.getData());
		for(size_t i = 0; i < words.size(); ++i, b += 4)
			words[i] = juce::ByteOrder::littleEndianInt(b);

		auto* display = juce::XWindowSystem::getInstance()->getDisplay();
		if(!display)
			return;

		// JUCE's own setIcon takes one image, which a taskbar would scale down to
		// its 16 or 24 pixels. This sets every size, small drawings included.
		auto* x11 = juce::X11Symbols::getInstance();
		const auto window = static_cast<::Window>(reinterpret_cast<uintptr_t>(peer->getNativeHandle()));
		juce::XWindowSystemUtilities::ScopedXLock lock;
		x11->xChangeProperty(display, window, x11->xInternAtom(display, "_NET_WM_ICON", False), XA_CARDINAL,
			32, PropModeReplace, reinterpret_cast<const unsigned char*>(words.data()), static_cast<int>(words.size()));
		x11->xFlush(display);
#else
		juce::ignoreUnused(_component);
#endif
	}
}
