#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "juce_audio_devices/juce_audio_devices.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace baseLib
{
	class ChunkReader;
	class BinaryStream;
}

namespace juce
{
	class String;
}

namespace pluginLib
{
	class Processor;
	class MidiOutputSink
	{
	public:
		virtual ~MidiOutputSink() = default;
		virtual juce::String getIdentifier() const = 0;
		virtual void start() = 0;
		virtual void stop() = 0;
		virtual bool isRunning() const = 0;
		virtual void sendMessageNow(const juce::MidiMessage& _message) = 0;
	};

	// Fixed-capacity asynchronous output shared by physical MIDI and its controlled
	// tests. Realtime producers only try the queue lock and never wait; lifecycle
	// changes stop and join the sole consumer before replacing its sink.
	class MidiOutputDispatcher
	{
	public:
		static constexpr size_t Capacity = 128;

		MidiOutputDispatcher() = default;
		~MidiOutputDispatcher();
		MidiOutputDispatcher(const MidiOutputDispatcher&) = delete;
		MidiOutputDispatcher(MidiOutputDispatcher&&) = delete;
		MidiOutputDispatcher& operator=(const MidiOutputDispatcher&) = delete;
		MidiOutputDispatcher& operator=(MidiOutputDispatcher&&) = delete;

		using Clock = std::chrono::steady_clock;

		void send(juce::MidiMessage&& _message);
		void send(const juce::MidiMessage& _message);
		bool trySend(juce::MidiMessage&& _message);
		// Queue a message to leave at `_due` rather than as soon as possible.
		// Due times never go backwards: an earlier one is sent after the
		// message ahead of it, so the order is always the order queued.
		void sendAt(juce::MidiMessage&& _message, Clock::time_point _due);
		bool trySendAt(juce::MidiMessage&& _message, Clock::time_point _due);
		bool setOutput(std::unique_ptr<MidiOutputSink> _output);
		void close();
		bool isValid() const;
		juce::String getOutputId() const;

	private:
		void senderThread();
		bool outputQueueFull() const { return m_count == Capacity; }
		void push(juce::MidiMessage&& _message, Clock::time_point _due);
		juce::MidiMessage pop();
		void clear();

		std::unique_ptr<MidiOutputSink> m_output;
		std::array<juce::MidiMessage, Capacity> m_messages;
		std::array<Clock::time_point, Capacity> m_due;
		Clock::time_point m_lastDue{};
		size_t m_read = 0;
		size_t m_write = 0;
		size_t m_count = 0;
		mutable std::mutex m_mutex;
		std::mutex m_configurationMutex;
		std::condition_variable m_condition;
		bool m_stopping = false;
		std::unique_ptr<std::thread> m_thread;
	};

	class MidiPorts : juce::MidiInputCallback
	{
	public:
		MidiPorts(Processor& _processor);
		MidiPorts(MidiPorts&&) = delete;
		MidiPorts(const MidiPorts&) = delete;

		~MidiPorts() override;

		MidiPorts& operator = (const MidiPorts&) = delete;
		MidiPorts& operator = (MidiPorts&&) = delete;

		auto& getProcessor() const { return m_processor; }

		juce::MidiInput* getMidiInput() const;

		bool setMidiOutput(const juce::String& _out);
		bool setMidiInput(const juce::String& _in);

		juce::String getInputId() const;
		juce::String getOutputId() const;

		void saveChunkData(baseLib::BinaryStream& _binaryStream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

		void send(juce::MidiMessage&& _message);
		void send(const juce::MidiMessage& _message);

		// The unit's own MIDI OUT (source Device) leaves at its place in the
		// audio block when timing is on (see beginBlock); anything else, now.
		void send(const synthLib::SMidiEvent& _message);
		bool trySend(const synthLib::SMidiEvent& _message);

		// Audio thread, at the top of every block: where on the wall clock this
		// block starts. MIDI OUT used to go out when its block was finished, so
		// the MD's clock left in lumps (7.7 to 31 ms apart around 19.7) and
		// anything following it heard the tempo wobble. A delay-locked loop (as
		// JACK's) turns the uneven callback times into a steady block clock, and
		// each event is sent at that block time + its sample offset + a fixed
		// latency long enough that its moment is never already past.
		// GEARMULATOR_MIDI_OUT_TIMED=0 sends as before.
		void beginBlock(int _numSamples, double _sampleRate);

		// MIDI IN, the same way round: a message arriving on the app's own MIDI
		// In port used to go in at the start of whichever block came next, so a
		// unit following an external clock heard each tick up to a block early
		// or late (MM against MD: +-4 ms, -6 to +8). Each message is now stamped
		// on arrival and handed to the unit at its own sample, `m_latency` later,
		// from beginBlock. GEARMULATOR_MIDI_IN_TIMED=0 goes back to the old way.

		void close();

		static juce::MidiMessage toJuceMidiMessage(const synthLib::SMidiEvent& _e);

		bool isMidiOutValid() const;

		// Standalone only: open public "MIDI In" / "MIDI Out" ports on the app's
		// own MIDI client, so other programs can be cabled to it by a stable name
		// (ALSA: "Gearmulator MM:MIDI In"). MIDI Out carries the physical output
		// while no output device is selected. Call once the processor can take
		// MIDI; later calls do nothing.
		void openVirtualPorts();

		// the MIDI devices to offer in this app's port menus: everything except
		// its own virtual ports, which would feed it its own output
		juce::Array<juce::MidiDeviceInfo> getAvailableInputs() const;
		juce::Array<juce::MidiDeviceInfo> getAvailableOutputs() const;

	private:
		juce::Array<juce::MidiDeviceInfo> withoutOwnPorts(juce::Array<juce::MidiDeviceInfo> _devices) const;
		bool dueFor(const synthLib::SMidiEvent& _e, MidiOutputDispatcher::Clock::time_point& _due) const;
		void deliverTimedInput();

		// delay-locked loop state (audio thread only)
		bool m_timed = true;
		bool m_inTimed = true;
		std::mutex m_inLock;
		std::deque<std::pair<double, juce::MidiMessage>> m_inPending;   // (due, message)
		std::atomic<bool> m_clockValid{false};   // read by the MIDI thread too
		double m_blockStart = 0.0;      // seconds, steady clock, this block
		double m_secondsPerSample = 0.0;
		int m_blockSamples = 0;
		std::atomic<double> m_latency{0.0};     // seconds added to every due time

	    void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override;

		Processor& m_processor;

		MidiOutputDispatcher m_midiOutput;
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::unique_ptr<juce::AudioDeviceManager> m_deviceManager;
		std::unique_ptr<juce::MidiInput> m_virtualInput;
		std::unique_ptr<juce::MidiOutput> m_virtualOutput;
		bool m_virtualPortsOpened = false;
	};
}
