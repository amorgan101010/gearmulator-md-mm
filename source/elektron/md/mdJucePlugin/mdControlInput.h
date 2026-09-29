#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace mdJucePlugin
{
	// A local control input: a program on this computer plays the panel, as the EasyControl.9 bridge in
	// ~/Documents/worlde_setup does. The keyboard holds one knob at a time; this turns any number at once.
	//
	//     $XDG_RUNTIME_DIR/elektremu/<product>.sock    (machinedrum.sock, monomachine.sock)
	//
	// A Unix datagram socket, readable and writable by this user only, with no network side (the remote panel
	// is for tablets). Each datagram is one line of text; the editor answers it on the message thread, and a
	// non-empty answer goes back to the sender's bound address. A second window of the same product finds the
	// socket answered and goes without, rather than taking it over. Standalone only: plugin instances would
	// fight over the path.
	class ControlInput
	{
	public:
		// Runs on the message thread. A non-empty result is sent back to the sender.
		using Handler = std::function<std::string(const std::string& _line)>;

		ControlInput(std::string _product, Handler _handler);
		~ControlInput();

		ControlInput(const ControlInput&) = delete;
		ControlInput& operator=(const ControlInput&) = delete;

		bool start();

		const std::string& getPath() const { return m_path; }
		const std::string& getError() const { return m_error; }

	private:
		void readLoop();

		std::string m_product;
		Handler m_handler;
		std::string m_path;
		std::string m_error;
		int m_socket = -1;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		// Cleared on destruction, on the message thread, so lines still queued there are dropped.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
