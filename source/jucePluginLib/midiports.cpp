#include "midiports.h"

#include "processor.h"

#include "dsp56kBase/threadtools.h"

#include "juce_audio_devices/juce_audio_devices.h"

#include "synthLib/midiBufferParser.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace pluginLib
{
	namespace
	{
		class JuceMidiOutputSink final : public MidiOutputSink
		{
		public:
			explicit JuceMidiOutputSink(std::unique_ptr<juce::MidiOutput> _output)
				: m_output(std::move(_output)) {}

			juce::String getIdentifier() const override
			{
				return m_output->getIdentifier();
			}
			void start() override { m_output->startBackgroundThread(); }
			void stop() override
			{
				if(m_output->isBackgroundThreadRunning())
					m_output->stopBackgroundThread();
			}
			bool isRunning() const override
			{
				return m_output->isBackgroundThreadRunning();
			}
			void sendMessageNow(const juce::MidiMessage& _message) override
			{
				m_output->sendMessageNow(_message);
			}

		private:
			std::unique_ptr<juce::MidiOutput> m_output;
		};

		// The app's own "MIDI Out" port, used while no output device is selected.
		// Its identifier stays empty so the saved state still reads "no output".
		class VirtualMidiOutputSink final : public MidiOutputSink
		{
		public:
			explicit VirtualMidiOutputSink(juce::MidiOutput& _output) : m_output(_output) {}

			juce::String getIdentifier() const override { return {}; }
			void start() override { m_running = true; }
			void stop() override { m_running = false; }
			bool isRunning() const override { return m_running; }
			void sendMessageNow(const juce::MidiMessage& _message) override
			{
				m_output.sendMessageNow(_message);
			}

		private:
			juce::MidiOutput& m_output;
			bool m_running = false;
		};
	}

	MidiOutputDispatcher::~MidiOutputDispatcher()
	{
		close();
	}

	void MidiOutputDispatcher::push(juce::MidiMessage&& _message, const Clock::time_point _due)
	{
		assert(!outputQueueFull());
		// never earlier than the message ahead: the queue stays in order
		const auto due = _due < m_lastDue ? m_lastDue : _due;
		m_lastDue = due;
		m_due[m_write] = due;
		m_messages[m_write] = std::move(_message);
		m_write = (m_write + 1) % Capacity;
		++m_count;
	}

	juce::MidiMessage MidiOutputDispatcher::pop()
	{
		assert(m_count > 0);
		auto result = std::move(m_messages[m_read]);
		m_read = (m_read + 1) % Capacity;
		--m_count;
		return result;
	}

	void MidiOutputDispatcher::clear()
	{
		m_read = 0;
		m_write = 0;
		m_count = 0;
	}

	void MidiOutputDispatcher::send(juce::MidiMessage&& _message)
	{
		sendAt(std::move(_message), Clock::now());
	}

	void MidiOutputDispatcher::sendAt(juce::MidiMessage&& _message, const Clock::time_point _due)
	{
		std::unique_lock lock(m_mutex);
		if(m_output == nullptr || m_stopping)
			return;
		m_condition.wait(lock, [this]
		{
			return !outputQueueFull() || m_stopping || m_output == nullptr;
		});
		if(m_output == nullptr || m_stopping)
			return;
		push(std::move(_message), _due);
		lock.unlock();
		m_condition.notify_one();
	}

	void MidiOutputDispatcher::send(const juce::MidiMessage& _message)
	{
		auto copy = _message;
		send(std::move(copy));
	}

	bool MidiOutputDispatcher::trySend(juce::MidiMessage&& _message)
	{
		return trySendAt(std::move(_message), Clock::now());
	}

	bool MidiOutputDispatcher::trySendAt(juce::MidiMessage&& _message, const Clock::time_point _due)
	{
		std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock() || m_stopping)
			return false;
		if(m_output == nullptr)
			return true;
		if(outputQueueFull())
			return false;
		push(std::move(_message), _due);
		lock.unlock();
		m_condition.notify_one();
		return true;
	}

	bool MidiOutputDispatcher::setOutput(std::unique_ptr<MidiOutputSink> _output)
	{
		const std::lock_guard configurationLock(m_configurationMutex);
		{
			std::lock_guard lock(m_mutex);
			m_stopping = true;
		}
		m_condition.notify_all();

		if(m_thread)
		{
			m_thread->join();
			m_thread.reset();
		}

		{
			std::lock_guard lock(m_mutex);
			if(m_output != nullptr && m_output->isRunning())
				m_output->stop();
			m_output.reset();
			clear();
		}

		if(_output != nullptr)
			_output->start();
		const auto opened = _output != nullptr;
		{
			std::lock_guard lock(m_mutex);
			m_output = std::move(_output);
			m_stopping = false;
		}
		if(opened)
			m_thread = std::make_unique<std::thread>([this] { senderThread(); });
		return opened;
	}

	void MidiOutputDispatcher::close()
	{
		(void)setOutput({});
	}

	bool MidiOutputDispatcher::isValid() const
	{
		const std::lock_guard lock(m_mutex);
		return m_output != nullptr && !m_stopping;
	}

	juce::String MidiOutputDispatcher::getOutputId() const
	{
		const std::lock_guard lock(m_mutex);
		return m_output != nullptr ? m_output->getIdentifier() : juce::String();
	}

	void MidiOutputDispatcher::senderThread()
	{
		dsp56k::ThreadTools::setCurrentThreadName("MidiOutputSender");
#ifdef __linux__
		// A message waits for its due time; a normal-priority thread wakes late
		// under load, which is the jitter this exists to remove. Keep the
		// default where real-time scheduling is not allowed.
		sched_param param{};
		param.sched_priority = 60;
		pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
		for(;;)
		{
			std::unique_lock lock(m_mutex);
			m_condition.wait(lock, [this]
			{
				return m_stopping || m_count > 0;
			});
			if(m_stopping)
				return;

			const auto due = m_due[m_read];
			if(Clock::now() < due)
			{
				// a newer message is never due before this one, so only
				// stopping can end the wait early
				m_condition.wait_until(lock, due, [this] { return m_stopping; });
				continue;
			}

			auto message = pop();
			auto* const output = m_output.get();
			lock.unlock();
			m_condition.notify_all();
			if(output != nullptr)
				output->sendMessageNow(message);
		}
	}

	MidiPorts::MidiPorts(Processor& _processor) : m_processor(_processor)
	{
		const char* timed = std::getenv("GEARMULATOR_MIDI_OUT_TIMED");
		m_timed = timed == nullptr || std::strcmp(timed, "0") != 0;
		// Off unless asked for: with Gearmulator MD as the clock source it left
		// MM nearly stopped (4 kicks in 40 s against 71), 2026-09-27; cause not
		// yet found. GEARMULATOR_MIDI_IN_TIMED=1 turns it on to investigate.
		const char* inTimed = std::getenv("GEARMULATOR_MIDI_IN_TIMED");
		m_inTimed = inTimed != nullptr && std::strcmp(inTimed, "1") == 0;
	}

	namespace
	{
		double secondsNow()
		{
			return std::chrono::duration<double>(
				MidiOutputDispatcher::Clock::now().time_since_epoch()).count();
		}
	}

	void MidiPorts::beginBlock(const int _numSamples, const double _sampleRate)
	{
		if(!m_timed || _numSamples <= 0 || _sampleRate <= 0.0)
		{
			m_clockValid = false;
			return;
		}
		const double now = secondsNow();
		const double nominal = 1.0 / _sampleRate;
		// the fixed lead: two blocks plus a little, so a block computed late in
		// its callback pair still finds its events' moments in the future
		m_latency = std::max(m_latency.load(), 2.0 * _numSamples * nominal + 0.005);
		if(m_clockValid)
		{
			const double predicted = m_blockStart + m_blockSamples * m_secondsPerSample;
			const double err = now - predicted;
			if(std::abs(err) < 0.25)
			{
				// second-order DLL, bandwidth 0.5 Hz at this block period
				const double omega = 2.0 * 3.14159265358979 * 0.5 * m_blockSamples * m_secondsPerSample;
				m_blockStart = predicted + std::sqrt(2.0) * omega * err;
				m_secondsPerSample += omega * omega * err / m_blockSamples;
				m_secondsPerSample = std::clamp(m_secondsPerSample, nominal * 0.98, nominal * 1.02);
				m_blockSamples = _numSamples;
				deliverTimedInput();
				return;
			}
		}
		// first block, or a stall / device restart: start over from now
		m_clockValid = true;
		m_blockStart = now;
		m_secondsPerSample = nominal;
		m_blockSamples = _numSamples;
		deliverTimedInput();
	}

	void MidiPorts::deliverTimedInput()
	{
		// Audio thread: never wait on the MIDI thread; a busy lock just means
		// these go in with the next block.
		std::unique_lock lock(m_inLock, std::try_to_lock);
		if(!lock.owns_lock())
			return;
		const double blockEnd = m_blockStart + m_blockSamples * m_secondsPerSample;
		while(!m_inPending.empty() && m_inPending.front().first < blockEnd)
		{
			const double at = (m_inPending.front().first - m_blockStart) / m_secondsPerSample;
			const auto offset = static_cast<uint32_t>(std::clamp(at, 0.0,
				static_cast<double>(m_blockSamples - 1)));
			m_processor.handleIncomingMidiMessage(nullptr, m_inPending.front().second, offset);
			m_inPending.pop_front();
		}
	}

	bool MidiPorts::dueFor(const synthLib::SMidiEvent& _e, MidiOutputDispatcher::Clock::time_point& _due) const
	{
		if(!m_timed || !m_clockValid || _e.source != synthLib::MidiEventSource::Device)
			return false;
		const double t = m_blockStart + _e.offset * m_secondsPerSample + m_latency;
		_due = MidiOutputDispatcher::Clock::time_point(
			std::chrono::duration_cast<MidiOutputDispatcher::Clock::duration>(
				std::chrono::duration<double>(t)));
		return true;
	}

	void MidiPorts::send(const synthLib::SMidiEvent& _message)
	{
		MidiOutputDispatcher::Clock::time_point due;
		if(dueFor(_message, due))
			m_midiOutput.sendAt(toJuceMidiMessage(_message), due);
		else
			send(toJuceMidiMessage(_message));
	}

	MidiPorts::~MidiPorts()
	{
		close();
		m_deviceManager.reset();
	}

	juce::MidiInput *MidiPorts::getMidiInput() const
	{
		return m_midiInput.get();
	}

	juce::String MidiPorts::getInputId() const
	{
		return getMidiInput() != nullptr ? getMidiInput()->getIdentifier() : juce::String();
	}

	juce::String MidiPorts::getOutputId() const
	{
		return m_midiOutput.getOutputId();
	}

	void MidiPorts::saveChunkData(baseLib::BinaryStream& _binaryStream) const
	{
		baseLib::ChunkWriter cw(_binaryStream, "mpIO", 1);

		if(m_midiInput)
			_binaryStream.write(m_midiInput->getIdentifier().toStdString());
		else
			_binaryStream.write(std::string());
		_binaryStream.write(m_midiOutput.getOutputId().toStdString());
	}

	void MidiPorts::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("mpIO", 1, [&](baseLib::BinaryStream& _data, uint32_t)
		{
			const auto input = _data.readString();
			const auto output = _data.readString();

			setMidiInput(input);
			setMidiOutput(output);
		});
	}

	void MidiPorts::close()
	{
		setMidiInput({});
		if (m_virtualInput != nullptr)
		{
			m_virtualInput->stop();
			m_virtualInput.reset();
		}
		// not setMidiOutput({}): that would put the virtual output back
		m_midiOutput.close();
		m_virtualOutput.reset();
	}

	void MidiPorts::openVirtualPorts()
	{
		if (m_virtualPortsOpened || !juce::JUCEApplicationBase::isStandaloneApp())
			return;
		m_virtualPortsOpened = true;

		m_virtualInput = juce::MidiInput::createNewDevice("MIDI In", this);
		if (m_virtualInput != nullptr)
			m_virtualInput->start();

		m_virtualOutput = juce::MidiOutput::createNewDevice("MIDI Out");
		if (m_virtualOutput != nullptr && getOutputId().isEmpty())
			setMidiOutput({});
	}

	juce::Array<juce::MidiDeviceInfo> MidiPorts::getAvailableInputs() const
	{
		return withoutOwnPorts(juce::MidiInput::getAvailableDevices());
	}

	juce::Array<juce::MidiDeviceInfo> MidiPorts::getAvailableOutputs() const
	{
		return withoutOwnPorts(juce::MidiOutput::getAvailableDevices());
	}

	juce::Array<juce::MidiDeviceInfo> MidiPorts::withoutOwnPorts(juce::Array<juce::MidiDeviceInfo> _devices) const
	{
		// ALSA identifiers are "client-port"; both virtual ports share one client
		const juce::String own = m_virtualInput != nullptr ? m_virtualInput->getIdentifier()
			: m_virtualOutput != nullptr ? m_virtualOutput->getIdentifier() : juce::String();
		if (own.isEmpty())
			return _devices;
		const auto prefix = own.upToFirstOccurrenceOf("-", true, false);
		_devices.removeIf([&prefix](const juce::MidiDeviceInfo& _d)
		{
			return _d.identifier.startsWith(prefix);
		});
		return _devices;
	}

	juce::MidiMessage MidiPorts::toJuceMidiMessage(const synthLib::SMidiEvent& _e)
	{
	    if(!_e.sysex.empty())
	    {
		    assert(_e.sysex.front() == 0xf0);
		    assert(_e.sysex.back() == 0xf7);

		    return {_e.sysex.data(), static_cast<int>(_e.sysex.size()), 0.0};
	    }
	    const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(_e.a);
	    if(len == 1)
		    return {_e.a, 0.0};
	    if(len == 2)
		    return {_e.a, _e.b, 0.0};
	    return {_e.a, _e.b, _e.c, 0.0};
	}

	void MidiPorts::send(juce::MidiMessage&& _message)
	{
		m_midiOutput.send(std::move(_message));
	}

	void MidiPorts::send(const juce::MidiMessage& _message)
	{
		auto copy = _message;
		send(std::move(copy));
	}

	bool MidiPorts::trySend(const synthLib::SMidiEvent& _message)
	{
		// The realtime path is only for fixed-size channel messages. SysEx
		// conversion owns variable-size storage and belongs on send().
		if(!_message.sysex.empty())
			return false;
		MidiOutputDispatcher::Clock::time_point due;
		if(dueFor(_message, due))
			return m_midiOutput.trySendAt(toJuceMidiMessage(_message), due);
		return m_midiOutput.trySend(toJuceMidiMessage(_message));
	}

	bool MidiPorts::isMidiOutValid() const
	{
		return m_midiOutput.isValid();
	}

	bool MidiPorts::setMidiOutput(const juce::String& _out)
	{
		std::unique_ptr<juce::MidiOutput> output;
		if(!_out.isEmpty())
			output = juce::MidiOutput::openDevice(_out);
		if (output == nullptr && m_virtualOutput != nullptr)
			return m_midiOutput.setOutput(std::make_unique<VirtualMidiOutputSink>(*m_virtualOutput));
		return m_midiOutput.setOutput(output == nullptr ? nullptr
			: std::make_unique<JuceMidiOutputSink>(std::move(output)));
	}

	bool MidiPorts::setMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
			m_midiInput = nullptr;
		}

		if(_in.isEmpty())
			return false;

		if(!m_deviceManager)
			m_deviceManager.reset(new juce::AudioDeviceManager());

		if (!m_deviceManager->isMidiInputDeviceEnabled(_in))
			m_deviceManager->setMidiInputDeviceEnabled(_in, true);

		m_midiInput = juce::MidiInput::openDevice(_in, this);
		if (m_midiInput != nullptr)
		{
			m_midiInput->start();
			return true;
		}
		return false;
	}

	void MidiPorts::handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message)
	{
		// Only while blocks are running (m_clockValid): before audio starts,
		// or with timing off, a message goes straight in as it always did.
		if(!m_inTimed || !m_timed || !m_clockValid || m_latency <= 0.0)
		{
			m_processor.handleIncomingMidiMessage(_source, _message);
			return;
		}
		const double due = secondsNow() + m_latency;
		const std::lock_guard lock(m_inLock);
		if(m_inPending.size() >= 4096)          // audio stopped: don't grow for ever
			m_inPending.pop_front();
		m_inPending.emplace_back(due, _message);
	}

}
