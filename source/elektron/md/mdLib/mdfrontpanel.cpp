#include "mdfrontpanel.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace md
{
	FrontPanel::FrontPanel()
	{
		reset();
	}

	void FrontPanel::reset()
	{
		for (auto& half : m_lcdVram)
			for (auto& page : half)
				page.fill(0);

		m_ledBank.fill(0xff);      // active-low: all LEDs off at power-on
		m_ledBankWritten.fill(false);

		m_parseState = 0;
		m_cmd = 0;
		m_lcdXaddr = 0;
		m_lcdYaddr = 0;
		m_lcdCol = 0;
		m_tilePayload.fill(0);

		m_byteCount = 0;
		m_tileWriteCount = 0;
		m_ledCommandCount = 0;
	}

	std::optional<FrontPanel::LedBankWrite> FrontPanel::processByte(uint8_t _byte)
	{
		++m_byteCount;
		return decode(_byte);
	}

	void FrontPanel::processBytes(const uint8_t* _data, size_t _size)
	{
		for (size_t i = 0; i < _size; ++i)
			(void)processByte(_data[i]);
	}

	// Streaming decode of the host->panel command stream: skip LCD tile writes
	// (0x1x + valid column base + 8 payload bytes), decode LED banks as [cmd][arg].
	std::optional<FrontPanel::LedBankWrite> FrontPanel::decode(uint8_t _byte)
	{
		switch (m_parseState)
		{
		case 0: // idle: a tile start (0x1x), an LED command (0x20-0x25), or an ignorable byte
			if (_byte >= 0x10 && _byte <= 0x1f)
			{
				m_lcdXaddr = _byte;
				m_parseState = 1;
			}
			else if (_byte >= g_firstLedBank && _byte <= g_lastLedBank)
			{
				m_cmd = _byte;
				m_parseState = 3;
			}
			break;

		case 1: // column base after a 0x1x: valid = 0,8,..,56 -> a real tile write
			if ((_byte & 0x07) == 0 && _byte < 64)
			{
				m_lcdYaddr = _byte;
				m_lcdCol = 0;
				m_parseState = 2;
			}
			else
			{
				// not a tile after all: drop back to idle and reprocess this byte
				m_parseState = 0;
				return decode(_byte);
			}
			break;

		case 2: // 8 payload bytes = one atomically committed LCD tile
		{
			m_tilePayload[m_lcdCol] = _byte;
			if (++m_lcdCol >= 8)
			{
				const uint32_t half = (m_lcdXaddr >> 3) & 1;
				const uint32_t page = m_lcdXaddr & 0x07;
				for(uint32_t i = 0; i < m_tilePayload.size(); ++i)
				{
					const auto col = static_cast<uint32_t>(m_lcdYaddr) + i;
					if(col < 64)
						m_lcdVram[half][page][col] = m_tilePayload[i];
				}
				m_parseState = 0;
				++m_tileWriteCount;
			}
			break;
		}

		default: // case 3: LED command argument
		{
			const uint32_t idx = bankIndex(m_cmd);
			const bool changed = m_ledBank[idx] != _byte;
			m_ledBank[idx] = _byte;
			m_ledBankWritten[idx] = true;
			m_parseState = 0;
			++m_ledCommandCount;
			if(changed)
				return LedBankWrite{m_cmd, _byte};
			break;
		}
		}
		return std::nullopt;
	}

	bool FrontPanel::getLcdPixel(uint32_t _x, uint32_t _y) const
	{
		if (_x >= g_lcdWidth || _y >= g_lcdHeight)
			return false;

		const uint32_t half = (_x >> 6) & 1;   // 0..63 -> controller 0, 64..127 -> controller 1
		const uint32_t col = _x & 0x3f;
		const uint32_t page = (_y >> 3) & 7;
		const uint32_t bit = _y & 7;           // LSB = topmost pixel of the page
		return ((m_lcdVram[half][page][col] >> bit) & 1) != 0;
	}

	uint32_t FrontPanel::countLitPixels() const
	{
		uint32_t count = 0;
		for (const auto& half : m_lcdVram)
			for (const auto& page : half)
				for (const uint8_t byte : page)
				{
					uint8_t v = byte;
					while (v)
					{
						v &= static_cast<uint8_t>(v - 1);
						++count;
					}
				}
		return count;
	}

	uint8_t FrontPanel::getLcdVram(uint32_t _half, uint32_t _page, uint32_t _col) const
	{
		if (_half >= 2 || _page >= 8 || _col >= 64)
			return 0;
		return m_lcdVram[_half][_page][_col];
	}

	void FrontPanel::setRaw(const uint8_t* const _vram, const uint8_t* const _ledBanks)
	{
		reset();
		size_t at = 0;
		for(auto& half : m_lcdVram)
			for(auto& page : half)
				for(auto& column : page)
					column = _vram[at++];
		for(uint32_t i = 0; i < g_ledBankCount; ++i)
		{
			m_ledBank[i] = _ledBanks[i];
			m_ledBankWritten[i] = true;
		}
	}

	uint8_t FrontPanel::getLedBankRaw(LedBank _bank) const
	{
		return getLedBankRaw(static_cast<uint8_t>(_bank));
	}

	uint8_t FrontPanel::getLedBankRaw(uint8_t _command) const
	{
		if (_command < g_firstLedBank || _command > g_lastLedBank)
			return 0xff;
		return m_ledBank[bankIndex(_command)];
	}

	bool FrontPanel::wasLedBankWritten(LedBank _bank) const
	{
		return wasLedBankWritten(static_cast<uint8_t>(_bank));
	}

	bool FrontPanel::wasLedBankWritten(uint8_t _command) const
	{
		if (_command < g_firstLedBank || _command > g_lastLedBank)
			return false;
		return m_ledBankWritten[bankIndex(_command)];
	}

	bool FrontPanel::getStepLed(uint32_t _index) const
	{
		if (_index >= 16)
			return false;
		const uint8_t raw = m_ledBank[bankIndex(_index < 8 ? LedBank::Step0 : LedBank::Step1)];
		const uint32_t bit = _index & 7;
		return ((raw >> bit) & 1) == 0;   // active-low: 0 bit = lit
	}

	FrontPanel::LedColor FrontPanel::getMonomachineStepLedColor(uint32_t _index) const
	{
		if(_index >= 16)
			return LedColor::Off;

		// MM packs four bicolor TRIG LEDs into each bank. Within each pair the
		// even bit drives green and the odd bit drives red; both are active-low.
		const auto bank = static_cast<uint8_t>(g_firstLedBank + (_index >> 2));
		const uint8_t raw = m_ledBank[bankIndex(bank)];
		return decodeMonomachineStepLedColor(raw, _index & 3);
	}

	FrontPanel::LedColor FrontPanel::decodeMonomachineStepLedColor(
		const uint8_t _raw, const uint32_t _indexInBank)
	{
		if(_indexInBank >= 4)
			return LedColor::Off;
		const uint32_t greenBit = _indexInBank << 1;
		const bool green = ((_raw >> greenBit) & 1) == 0;
		const bool red = ((_raw >> (greenBit + 1)) & 1) == 0;
		if(green && red)
			return LedColor::Yellow;
		if(green)
			return LedColor::Green;
		if(red)
			return LedColor::Red;
		return LedColor::Off;
	}

	bool FrontPanel::getDrumLed(uint32_t _index) const
	{
		if (_index >= 16)
			return false;
		const uint8_t raw = m_ledBank[bankIndex(_index < 8 ? LedBank::Drum0 : LedBank::Drum1)];
		const uint32_t bit = _index & 7;
		return ((raw >> bit) & 1) == 0;
	}

	bool FrontPanel::getStatusLed(StatusLed _led) const
	{
		const uint8_t raw = m_ledBank[bankIndex(LedBank::Status)];
		return ((raw >> static_cast<uint8_t>(_led)) & 1) == 0;
	}

	bool FrontPanel::getModeLed(ModeLed _led) const
	{
		const uint8_t raw = m_ledBank[bankIndex(LedBank::Mode)];
		return ((raw >> static_cast<uint8_t>(_led)) & 1) == 0;
	}

	FrontPanelPublisher::FrontPanelPublisher()
	{
		publish(FrontPanel{}, 0);
	}

	void FrontPanelPublisher::publish(const FrontPanel& _panel, const uint64_t _ledSequence)
	{
		Published published;
		published.panel = _panel;
		published.ledSequence = _ledSequence;
		std::array<uint64_t, g_words> words{};
		std::memcpy(words.data(), &published, sizeof(published));
		const auto sequence = m_sequence.load(std::memory_order_relaxed);
		m_sequence.store(sequence + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		for(size_t i = 0; i < g_words; ++i)
			m_words[i].store(words[i], std::memory_order_relaxed);
		m_sequence.store(sequence + 2, std::memory_order_release);
		m_publishedLedSequence.store(_ledSequence, std::memory_order_release);
	}

	bool FrontPanelPublisher::tryReadPublished(Published& _published, uint64_t* const _sequence) const
	{
		const auto before = m_sequence.load(std::memory_order_acquire);
		if(before & 1)
			return false;
		std::array<uint64_t, g_words> words;
		for(size_t i = 0; i < g_words; ++i)
			words[i] = m_words[i].load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if(m_sequence.load(std::memory_order_relaxed) != before)
			return false;
		std::memcpy(&_published, words.data(), sizeof(_published));
		if(_sequence)
			*_sequence = before / 2;
		return true;
	}

	FrontPanelPublisher::Published FrontPanelPublisher::readPublished() const
	{
		Published published;
		for(uint32_t attempt = 0; !tryReadPublished(published); ++attempt)
		{
			if(attempt > 64)
				std::this_thread::yield();
		}
		return published;
	}

	bool FrontPanelPublisher::tryPublish(const FrontPanel& _panel)
	{
		publish(_panel, m_ledTransitionSequence.load(std::memory_order_acquire));
		return true;
	}

	bool FrontPanelPublisher::tryRead(FrontPanel& _panel) const
	{
		Published published;
		if(!tryReadPublished(published))
			return false;
		_panel = published.panel;
		return true;
	}

	FrontPanel FrontPanelPublisher::read() const
	{
		return readPublished().panel;
	}

	bool FrontPanelPublisher::tryReadPublishedState(FrontPanelPublishedState& _state, uint64_t& _generation) const
	{
		Published published;
		if(!tryReadPublished(published, &_generation))
			return false;
		_state = {published.panel, published.ledSequence};
		return true;
	}

	FrontPanelPublishedState FrontPanelPublisher::readPublishedState() const
	{
		const auto published = readPublished();
		return {published.panel, published.ledSequence};
	}

	bool FrontPanelPublisher::tryPushLedTransition(const uint8_t _command,
		const uint8_t _value, const uint64_t _emulationCycles)
	{
		const auto sequence = m_ledTransitionSequence.fetch_add(1,
			std::memory_order_relaxed) + 1;
		const auto write = m_ledTransitionWrite.load(std::memory_order_relaxed);
		const auto read = m_ledTransitionRead.load(std::memory_order_acquire);
		if(write - read >= g_ledTransitionCapacity)
		{
			m_ledTransitionDropped.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		m_ledTransitions[write % g_ledTransitionCapacity] =
			{sequence, _emulationCycles, _command, _value};
		m_ledTransitionWrite.store(write + 1, std::memory_order_release);
		return true;
	}

	size_t FrontPanelPublisher::drainLedTransitions(
		FrontPanelLedTransition* const _output, const size_t _capacity)
	{
		if(!_output || _capacity == 0)
			return 0;
		const std::lock_guard lock(m_readerMutex);

		auto read = m_ledTransitionRead.load(std::memory_order_relaxed);
		const auto write = m_ledTransitionWrite.load(std::memory_order_acquire);
		const auto count = std::min(_capacity, write - read);
		for(size_t i = 0; i < count; ++i)
			_output[i] = m_ledTransitions[(read + i) % g_ledTransitionCapacity];
		read += count;
		m_ledTransitionRead.store(read, std::memory_order_release);
		return count;
	}

	FrontPanelLedTransitionStatus FrontPanelPublisher::getLedTransitionStatus() const
	{
		return
		{
			m_ledTransitionEpoch.load(std::memory_order_acquire),
			m_ledTransitionDropped.load(std::memory_order_acquire),
			m_ledTransitionSequence.load(std::memory_order_acquire),
			m_publishedLedSequence.load(std::memory_order_acquire),
		};
	}

	void FrontPanelPublisher::reset()
	{
		const std::lock_guard lock(m_readerMutex);
		publish(FrontPanel{}, m_ledTransitionSequence.load(std::memory_order_acquire));
		m_ledTransitionRead.store(
			m_ledTransitionWrite.load(std::memory_order_acquire),
			std::memory_order_release);
		m_ledTransitionDropped.store(0, std::memory_order_release);
		m_ledTransitionEpoch.fetch_add(1, std::memory_order_acq_rel);
	}
}
