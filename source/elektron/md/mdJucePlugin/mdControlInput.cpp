#include "mdControlInput.h"

#include "juce_events/juce_events.h"

#if defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#define MD_CONTROL_INPUT 1
#endif

namespace mdJucePlugin
{
	namespace
	{
		constexpr int g_pollMilliseconds = 200;		// how often the reader checks it should stop
		constexpr size_t g_maxLine = 512;
	}

	ControlInput::ControlInput(std::string _product, Handler _handler)
		: m_product(std::move(_product)), m_handler(std::move(_handler))
	{
	}

	ControlInput::~ControlInput()
	{
		*m_alive = false;
		m_running = false;
		if(m_thread.joinable())
			m_thread.join();
#ifdef MD_CONTROL_INPUT
		if(m_socket >= 0)
		{
			::close(m_socket);
			::unlink(m_path.c_str());
		}
#endif
	}

	bool ControlInput::start()
	{
#ifdef MD_CONTROL_INPUT
		const char* runtime = std::getenv("XDG_RUNTIME_DIR");
		const std::string dir = std::string(runtime && *runtime ? runtime : "/tmp") + "/elektremu";
		m_path = dir + "/" + m_product + ".sock";

		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		if(m_path.size() >= sizeof(address.sun_path))
		{
			m_error = "path too long: " + m_path;
			return false;
		}
		std::memcpy(address.sun_path, m_path.c_str(), m_path.size() + 1);

		if(::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
		{
			m_error = "cannot create " + dir + ": " + std::strerror(errno);
			return false;
		}

		// Someone reading it already: another window of this product. Leave it to that one.
		const int probe = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if(probe >= 0)
		{
			const bool answered = ::connect(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
			::close(probe);
			if(answered)
			{
				m_error = "another window has " + m_path;
				return false;
			}
		}
		::unlink(m_path.c_str());	// a stale one, left by a window that did not close cleanly

		m_socket = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if(m_socket < 0)
		{
			m_error = std::string("socket: ") + std::strerror(errno);
			return false;
		}
		if(::bind(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
		{
			m_error = "bind " + m_path + ": " + std::strerror(errno);
			::close(m_socket);
			m_socket = -1;
			return false;
		}
		::chmod(m_path.c_str(), 0600);

		m_running = true;
		m_thread = std::thread([this] { readLoop(); });
		return true;
#else
		m_error = "not available on this platform";
		return false;
#endif
	}

	void ControlInput::readLoop()
	{
#ifdef MD_CONTROL_INPUT
		char buffer[g_maxLine];
		while(m_running)
		{
			pollfd pfd{m_socket, POLLIN, 0};
			if(::poll(&pfd, 1, g_pollMilliseconds) <= 0)
				continue;

			sockaddr_un sender{};
			socklen_t senderLength = sizeof(sender);
			const auto received = ::recvfrom(m_socket, buffer, sizeof(buffer), 0,
				reinterpret_cast<sockaddr*>(&sender), &senderLength);
			if(received <= 0)
				continue;

			std::string line(buffer, static_cast<size_t>(received));
			while(!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
				line.pop_back();

			juce::MessageManager::callAsync([this, alive = m_alive, line = std::move(line), sender, senderLength]
			{
				if(!*alive)
					return;
				const auto reply = m_handler(line);
				// An unbound sender has nothing to answer to.
				if(reply.empty() || senderLength <= static_cast<socklen_t>(sizeof(sa_family_t)))
					return;
				::sendto(m_socket, reply.data(), reply.size(), MSG_DONTWAIT,
					reinterpret_cast<const sockaddr*>(&sender), senderLength);
			});
		}
#endif
	}
}
