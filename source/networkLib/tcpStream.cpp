#include "tcpStream.h"

#include <cstdint>

#include "exception.h"
#include "ptypes/pinet.h"

#ifndef _WIN32
#	include <poll.h>
#endif

namespace networkLib
{
	TcpStream::TcpStream(ptypes::ipstream* _stream)	: m_stream(_stream)
	{
	}

	TcpStream::~TcpStream()
	{
		TcpStream::close();
		delete m_stream;
		m_stream = nullptr;
	}

	void TcpStream::close()
	{
		if(!m_stream)
			return;
		m_stream->close();
	}

	bool TcpStream::isValid() const
	{
		return m_stream && m_stream->get_active();
	}

	bool TcpStream::canWrite() const
	{
		if (!isValid())
			return false;
#ifdef _WIN32
		// select, not WSAPoll: that one needs _WIN32_WINNT >= 0x0600 (winsock2.h comes with pinet.h)
		fd_set writable;
		FD_ZERO(&writable);
		FD_SET(static_cast<SOCKET>(m_stream->get_handle()), &writable);
		timeval now{0, 0};
		return ::select(0, nullptr, &writable, nullptr, &now) == 1;
#else
		pollfd p{m_stream->get_handle(), POLLOUT, 0};
		return ::poll(&p, 1, 0) == 1 && (p.revents & POLLOUT);
#endif
	}

	bool TcpStream::flush()
	{
		if (!isValid())
			return false;

		try
		{
			m_stream->flush();
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
		return true;
	}

	bool TcpStream::read(void* _buf, const uint32_t _byteSize)
	{
		if (!isValid())
			throw NetException(ConnectionClosed, "Couldn't read");

		try
		{
			const auto numRead = static_cast<uint32_t>(m_stream->read(_buf, static_cast<int>(_byteSize)));
			if(numRead == _byteSize)
				return true;
			throw NetException(ConnectionClosed, "Couldn't read");
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
	}

	bool TcpStream::write(const void* _buf, const uint32_t _byteSize)
	{
		if(!isValid())
			throw NetException(ConnectionClosed, "Couldn't write");

		try
		{
			const auto numWritten = static_cast<uint32_t>(m_stream->write(_buf, static_cast<int>(_byteSize)));
			if(_byteSize == numWritten)
				return true;
			throw NetException(ConnectionClosed, "Couldn't write");
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
	}
}
