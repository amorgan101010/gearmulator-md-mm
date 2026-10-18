#pragma once

namespace mdJucePlugin
{
	// Asks the window manager (EWMH _NET_ACTIVE_WINDOW, through an X connection of its own) which
	// window is the active one. JUCE can miss the focus-out of a window when two panels start
	// together and then goes on believing it is active; the window manager is the truth. This is a
	// veto only: no X display, no EWMH, or no answer means "yes". Always true off Linux.
	// Kept apart from the panel app because Xlib's macros (None, Bool, Status) break RmlUi's headers.
	class ActiveWindowQuery
	{
	public:
		ActiveWindowQuery() = default;
		~ActiveWindowQuery();
		ActiveWindowQuery(const ActiveWindowQuery&) = delete;
		ActiveWindowQuery& operator=(const ActiveWindowQuery&) = delete;

		// -> false only when the window manager names a different window as the active one.
		bool isTheActiveWindow(void* _nativeWindowHandle);

	private:
		void* m_display = nullptr; // Display*
		bool m_failed = false;
	};
}
