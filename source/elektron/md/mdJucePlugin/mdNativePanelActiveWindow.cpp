#include "mdNativePanelActiveWindow.h"

#include <cstdint>

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace mdJucePlugin
{
	ActiveWindowQuery::~ActiveWindowQuery()
	{
		if(m_display)
			XCloseDisplay(static_cast<Display*>(m_display));
	}

	bool ActiveWindowQuery::isTheActiveWindow(void* const _nativeWindowHandle)
	{
		const auto window = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(_nativeWindowHandle));
		if(!m_display && !m_failed)
		{
			m_display = XOpenDisplay(nullptr);
			m_failed = m_display == nullptr;
		}
		if(!m_display || window == 0)
			return true;
		auto* const display = static_cast<Display*>(m_display);
		const Atom property = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
		if(property == None)
			return true;
		Atom type = None;
		int format = 0;
		unsigned long count = 0, remaining = 0;
		unsigned char* data = nullptr;
		const int status = XGetWindowProperty(display, DefaultRootWindow(display), property,
			0, 1, False, XA_WINDOW, &type, &format, &count, &remaining, &data);
		bool result = true;
		if(status == Success && data && type == XA_WINDOW && format == 32 && count == 1)
		{
			const auto active = *reinterpret_cast<unsigned long*>(data);
			result = active == 0 || active == window;
		}
		if(data)
			XFree(data);
		return result;
	}
}
#else
namespace mdJucePlugin
{
	ActiveWindowQuery::~ActiveWindowQuery() = default;
	bool ActiveWindowQuery::isTheActiveWindow(void*) { return true; }
}
#endif
