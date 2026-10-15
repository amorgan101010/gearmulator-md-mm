// The Gearmulator device ABI (gmDeviceAbi/gm_device.h) for the Machinedrum and Monomachine.
//
// A thin adapter: render is synthLib::Device::process, the plug-in's own device entry point
// without the host resampler, so a host using this ABI drives the emulated unit exactly as the
// plug-in does. send_midi and advance reach md::Hardware directly, as the headless tools do,
// which lets a benchmark reproduce mdmmBench call for call.

#include "gmDeviceAbi/gm_device.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

struct gm_device
{
	gm_device(const synthLib::DeviceCreateParams& _params, const md::MachineModel _model)
		: device(_params), model(_model)
	{
	}

	md::Device device;
	md::MachineModel model;
	std::string error;
	uint32_t maxFrames = 0;
	std::vector<synthLib::SMidiEvent> midiIn;
	std::vector<synthLib::SMidiEvent> midiOut;
	std::vector<float> silence;	// stands in for a single null input channel
};

namespace
{
	constexpr size_t g_midiEventReserve = 1024;

	gm_status fail(gm_device* _device, const gm_status _status, const char* _message)
	{
		if(_device)
			_device->error = _message;
		return _status;
	}

	// Runs _f, turning any C++ exception into GM_ERROR_INTERNAL: nothing unwinds into the caller.
	template<typename F> gm_status guarded(gm_device* _device, F&& _f) noexcept
	{
		if(!_device)
			return GM_ERROR_ARGUMENT;
		try
		{
			_device->error.clear();
			return _f(*_device);
		}
		catch(const std::exception& e)
		{
			try { _device->error = e.what(); } catch(...) {}
			return GM_ERROR_INTERNAL;
		}
		catch(...)
		{
			try { _device->error = "unknown exception"; } catch(...) {}
			return GM_ERROR_INTERNAL;
		}
	}

	// Length of a short message from its status byte.
	uint32_t shortMessageSize(const uint8_t _status)
	{
		if(_status < 0x80)
			return 1;
		if(_status < 0xf0)
			return (_status & 0xe0) == 0xc0 ? 2 : 3;
		switch(_status)
		{
		case 0xf1: case 0xf3: return 2;
		case 0xf2: return 3;
		default: return 1;
		}
	}

	bool toEvent(synthLib::SMidiEvent& _event, const uint8_t* _data, const uint32_t _size, const uint32_t _frame)
	{
		if(!_data || !_size)
			return false;
		_event = synthLib::SMidiEvent(synthLib::MidiEventSource::Host);
		_event.offset = _frame;
		if(_data[0] == 0xf0)
		{
			_event.sysex.assign(_data, _data + _size);
			return true;
		}
		_event.a = _data[0];
		_event.b = _size > 1 ? _data[1] : 0;
		_event.c = _size > 2 ? _data[2] : 0;
		return true;
	}
}

extern "C"
{
	GM_API uint32_t gm_abi_version(void)
	{
		return GM_DEVICE_ABI_VERSION;
	}

	GM_API gm_status gm_device_create(const gm_device_create_info* _info, gm_device** _device)
	{
		if(!_device)
			return GM_ERROR_ARGUMENT;
		*_device = nullptr;
		if(!_info || _info->struct_size < sizeof(gm_device_create_info) || !_info->rom || !_info->rom_size)
			return GM_ERROR_ARGUMENT;
		if(_info->model != GM_MODEL_MACHINEDRUM && _info->model != GM_MODEL_MONOMACHINE)
			return GM_ERROR_ARGUMENT;
		try
		{
			const auto model = _info->model == GM_MODEL_MONOMACHINE
				? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
			synthLib::DeviceCreateParams params;
			params.romData.assign(_info->rom, _info->rom + _info->rom_size);
			// A label only, never opened. md::RomData counts a ROM without a name as invalid, and an
			// invalid ROM sends state restore to RomLoader::findROM, which searches the disk.
			params.romName = "gm_device";
			params.customData = model == md::MachineModel::Monomachine
				? md::g_deviceCustomDataMonomachine : md::g_deviceCustomDataMachinedrum;
			// homePath stays empty: the core reads no files (no factory-flash cache, no NVRAM).
			if(!md::RomLoader::isRomForModel(params.romData, model))
				return GM_ERROR_FIRMWARE;
			auto device = std::make_unique<gm_device>(params, model);
			if(!device->device.isValid())
				return GM_ERROR_FIRMWARE;
			device->midiIn.reserve(g_midiEventReserve);
			device->midiOut.reserve(g_midiEventReserve);
			device->device.reserveMidiEventCapacity(g_midiEventReserve);
			*_device = device.release();
			return GM_OK;
		}
		catch(...)
		{
			return GM_ERROR_INTERNAL;
		}
	}

	GM_API void gm_device_destroy(gm_device* _device)
	{
		try
		{
			delete _device;
		}
		catch(...)
		{
		}
	}

	GM_API const char* gm_device_last_error(const gm_device* _device)
	{
		return _device ? _device->error.c_str() : "";
	}

	GM_API gm_status gm_device_get_info(const gm_device* _device, gm_device_info* _info)
	{
		if(!_device || !_info || _info->struct_size < sizeof(gm_device_info))
			return GM_ERROR_ARGUMENT;
		_info->model = _device->model == md::MachineModel::Monomachine ? GM_MODEL_MONOMACHINE : GM_MODEL_MACHINEDRUM;
		_info->sample_rate = md::g_samplerate;
		_info->input_channels = 2;
		_info->output_channels = 6;
		return GM_OK;
	}

	GM_API gm_status gm_device_prepare(gm_device* _device, const uint32_t _maxFrames)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_maxFrames)
				return fail(&_d, GM_ERROR_ARGUMENT, "max_frames must be positive");
			_d.maxFrames = _maxFrames;
			_d.silence.assign(_maxFrames, 0.0f);
			_d.device.getHardware().reserveBlockSize(_maxFrames);
			return GM_OK;
		});
	}

	GM_API gm_status gm_device_advance(gm_device* _device, const uint32_t _frames)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			_d.device.getHardware().advance(_frames);
			return GM_OK;
		});
	}

	GM_API gm_status gm_device_render(gm_device* _device, gm_render_io* _io)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_io || _io->struct_size < sizeof(gm_render_io) || !_io->outputs)
				return fail(&_d, GM_ERROR_ARGUMENT, "render needs an io struct with outputs");
			if(!_d.maxFrames || _io->frames > _d.maxFrames)
				return fail(&_d, GM_ERROR_ARGUMENT, "frames exceed the prepared block size");
			if(_io->midi_in_count && !_io->midi_in)
				return fail(&_d, GM_ERROR_ARGUMENT, "midi_in is null");

			synthLib::TAudioInputs inputs{};
			if(_io->inputs && (_io->inputs[0] || _io->inputs[1]))
				for(size_t ch = 0; ch < 2; ++ch)
					inputs[ch] = _io->inputs[ch] ? _io->inputs[ch] : _d.silence.data();
			synthLib::TAudioOutputs outputs{};
			for(size_t ch = 0; ch < 6; ++ch)
				outputs[ch] = _io->outputs[ch];

			_d.midiIn.clear();
			for(uint32_t i = 0; i < _io->midi_in_count; ++i)
			{
				const auto& e = _io->midi_in[i];
				if(e.frame >= _io->frames)
					return fail(&_d, GM_ERROR_ARGUMENT, "a MIDI event lies outside the block");
				if(toEvent(_d.midiIn.emplace_back(), e.data, e.size, e.frame) == false)
					_d.midiIn.pop_back();
			}

			_d.device.process(inputs, outputs, _io->frames, _d.midiIn, _d.midiOut);

			_io->midi_out_count = 0;
			_io->midi_out_data_size = 0;
			_io->midi_out_dropped = 0;
			for(const auto& e : _d.midiOut)
			{
				uint8_t shortBytes[3] = {e.a, e.b, e.c};
				const uint8_t* bytes = e.sysex.empty() ? shortBytes : e.sysex.data();
				const auto size = e.sysex.empty() ? shortMessageSize(e.a) : static_cast<uint32_t>(e.sysex.size());
				if(_io->midi_out_count >= _io->midi_out_capacity || !_io->midi_out || !_io->midi_out_data
					|| _io->midi_out_data_size + size > _io->midi_out_data_capacity)
				{
					++_io->midi_out_dropped;
					continue;
				}
				std::memcpy(_io->midi_out_data + _io->midi_out_data_size, bytes, size);
				// The unit stamps a byte with the first sample at or after its UART time, so a byte
				// sent in the block's last fraction of a frame lands on frame == frames: clamp it in.
				const uint32_t frame = std::min(e.offset, _io->frames ? _io->frames - 1 : 0u);
				_io->midi_out[_io->midi_out_count++] = {frame, size, _io->midi_out_data_size};
				_io->midi_out_data_size += size;
			}
			return GM_OK;
		});
	}

	GM_API gm_status gm_device_send_midi(gm_device* _device, const uint8_t* _data, const uint32_t _size)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			synthLib::SMidiEvent e;
			if(!toEvent(e, _data, _size, 0))
				return fail(&_d, GM_ERROR_ARGUMENT, "empty MIDI message");
			return _d.device.getHardware().sendMidi(e) ? GM_OK : fail(&_d, GM_ERROR_REJECTED, "MIDI input queue is full");
		});
	}

	GM_API gm_status gm_device_send_panel(gm_device* _device, const uint8_t _row, const uint8_t _mask)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			return _d.device.getHardware().trySendPanelEvent(_row, _mask)
				? GM_OK : fail(&_d, GM_ERROR_REJECTED, "panel input queue is full");
		});
	}

	GM_API int gm_device_is_audio_ready(const gm_device* _device)
	{
		return _device && _device->device.getHardware().isAudioReady() ? 1 : 0;
	}

	GM_API int gm_device_is_midi_ready(const gm_device* _device)
	{
		return _device && _device->device.getHardware().isFirmwareMidiReady() ? 1 : 0;
	}

	GM_API gm_status gm_device_get_state(gm_device* _device, uint8_t* _buffer, const size_t _capacity, size_t* _size)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_size)
				return fail(&_d, GM_ERROR_ARGUMENT, "size is null");
			std::vector<uint8_t> state;
			if(!_d.device.getState(state, synthLib::StateTypeGlobal))
				return fail(&_d, GM_ERROR_STATE, "the unit's state is not available yet");
			*_size = state.size();
			if(!_buffer)
				return GM_OK;
			if(_capacity < state.size())
				return fail(&_d, GM_ERROR_CAPACITY, "state buffer too small");
			std::copy(state.begin(), state.end(), _buffer);
			return GM_OK;
		});
	}

	GM_API gm_status gm_device_set_state(gm_device* _device, const uint8_t* _data, const size_t _size)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_data || !_size)
				return fail(&_d, GM_ERROR_ARGUMENT, "empty state");
			const std::vector<uint8_t> state(_data, _data + _size);
			if(!_d.device.setState(state, synthLib::StateTypeGlobal))
				return fail(&_d, GM_ERROR_STATE, _d.device.projectStateRestoreError().empty()
					? "the state could not be applied" : _d.device.projectStateRestoreError().c_str());
			return GM_OK;
		});
	}

	GM_API int gm_device_is_restore_pending(const gm_device* _device)
	{
		return _device && _device->device.hasDeferredStateRestore() ? 1 : 0;
	}

	GM_API gm_status gm_device_service_restore(gm_device* _device)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_d.device.hasDeferredStateRestore())
				return GM_OK;
			// The same steps as the plug-in processor's serviceDeferredStateRestore, minus its
			// factory-cache file: persisting that is the host's business, not the core's.
			uint64_t generation = 0;
			auto validated = _d.device.takeFinishedDeferredState(generation);
			if(!validated)
				return fail(&_d, GM_ERROR_REJECTED, "the restored unit is still preparing");
			auto prepared = md::Device::makeDeferredStateReboot(*validated);
			if(!prepared)
			{
				(void)_d.device.rejectDeferredStateRestore(generation,
					"The deferred project state failed validation; the live machine was not changed.");
				return fail(&_d, GM_ERROR_STATE, "the deferred state failed validation");
			}
			if(!_d.device.commitDeferredStateRestore(*prepared, generation))
				return fail(&_d, GM_ERROR_STATE, "the deferred state could not be committed");
			return GM_OK;
		});
	}
}
