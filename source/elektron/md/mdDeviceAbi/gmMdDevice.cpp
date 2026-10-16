// The Gearmulator device ABI (gmDeviceAbi/gm_device.h) for the Machinedrum and Monomachine.
//
// A thin adapter: render is synthLib::Device::process, the plug-in's own device entry point
// without the host resampler, so a host using this ABI drives the emulated unit exactly as the
// plug-in does. send_midi and advance reach md::Hardware directly, as the headless tools do,
// which lets a benchmark reproduce mdmmBench call for call.

#include "gmDeviceAbi/gm_device.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdremotepanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
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

	// Remote panel. Its threads read the panel input and the front-panel publisher, which the
	// unit swaps on a deferred state restore: panelMutex orders those reads against the swap.
	std::unique_ptr<md::RemotePanelServer> remote;
	std::mutex panelMutex;
	// SysEx posted from other threads (the panel page, a host's MIDI thread), for the next render:
	// the render thread checks a flag and then only try-locks.
	std::mutex remoteSysexMutex;
	std::vector<std::vector<uint8_t>> remoteSysex;
	std::vector<std::vector<uint8_t>> remoteSysexTaken;
	std::atomic<bool> sysexPosted{false};

	bool postSysex(const uint8_t* _data, const size_t _size)
	{
		const std::lock_guard lock(remoteSysexMutex);
		if(remoteSysex.size() >= 256)
			return false;
		remoteSysex.emplace_back(_data, _data + _size);
		sysexPosted.store(true, std::memory_order_release);
		return true;
	}

	~gm_device()
	{
		remote.reset();	// joins its threads before the unit goes
	}
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

	constexpr uint32_t g_commonCapabilities = GM_CAP_MIDI_OUT | GM_CAP_ADVANCE | GM_CAP_PANEL_INPUT
		| GM_CAP_STATE | GM_CAP_POST_SYSEX | GM_CAP_REMOTE_PANEL;

	uint32_t modelCapabilities(const md::MachineModel _model)
	{
		// The Machinedrum plays its input (INP-GA, measured with the studio's gm-audio-edge). The
		// Monomachine takes input too, but no output from it has been shown (FX-THRU stays
		// silent), so it does not claim GM_CAP_AUDIO_INPUT. Only a Machinedrum state can need its
		// factory-flash baseline booted first.
		return _model == md::MachineModel::Machinedrum
			? g_commonCapabilities | GM_CAP_AUDIO_INPUT | GM_CAP_DEFERRED_RESTORE
			: g_commonCapabilities;
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
		// Minor 0 callers pass the struct without capabilities.
		if(!_device || !_info || _info->struct_size < offsetof(gm_device_info, capabilities))
			return GM_ERROR_ARGUMENT;
		_info->model = _device->model == md::MachineModel::Monomachine ? GM_MODEL_MONOMACHINE : GM_MODEL_MACHINEDRUM;
		_info->sample_rate = md::g_samplerate;
		_info->input_channels = 2;
		_info->output_channels = 6;
		if(_info->struct_size >= sizeof(gm_device_info))
			_info->capabilities = modelCapabilities(_device->model);
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

			// SysEx from the remote page, if its lock is free right now (never wait on the audio
			// thread); it goes in at frame 0 of this block.
			if(_d.sysexPosted.load(std::memory_order_acquire))
			{
				std::unique_lock lock(_d.remoteSysexMutex, std::try_to_lock);
				if(lock.owns_lock())
				{
					_d.remoteSysexTaken.swap(_d.remoteSysex);
					_d.sysexPosted.store(false, std::memory_order_relaxed);
				}
			}
			// Ahead of the block's own events (they are in frame order; these are at frame 0).
			for(auto it = _d.remoteSysexTaken.rbegin(); it != _d.remoteSysexTaken.rend(); ++it)
			{
				synthLib::SMidiEvent e(synthLib::MidiEventSource::Editor);
				e.sysex.assign(it->begin(), it->end());	// SysEx allocates by nature; the page sends it rarely
				_d.midiIn.insert(_d.midiIn.begin(), std::move(e));
			}
			_d.remoteSysexTaken.clear();

			_d.device.process(inputs, outputs, _io->frames, _d.midiIn, _d.midiOut);

			if(_d.remote && _io->outputs[0] && _io->outputs[1])
				_d.remote->feedAudio(_io->outputs[0], _io->outputs[1], _io->frames, md::g_samplerate);

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

	GM_API gm_status gm_device_remote_panel_start(gm_device* _device, const gm_remote_panel_info* _info, int32_t* _boundPort)
	{
		return guarded(_device, [&](gm_device& _d)
		{
			if(!_info || _info->struct_size < sizeof(gm_remote_panel_info) || !_info->resource_dir)
				return fail(&_d, GM_ERROR_ARGUMENT, "remote panel needs an info struct with resource_dir");
			if(_d.remote)
				return fail(&_d, GM_ERROR_REJECTED, "the remote panel is already running");
			const std::string dir = _info->resource_dir;
			md::RemotePanelServer::Callbacks callbacks;
			gm_device* const d = &_d;
			callbacks.sendPanelEvent = [d](const uint8_t _command, const uint8_t _argument)
			{
				const std::lock_guard lock(d->panelMutex);
				const auto input = d->device.getPanelInput();
				return input && input->trySend(_command, _argument, md::PanelSource::Remote);
			};
			callbacks.snapshot = [d]
			{
				const std::lock_guard lock(d->panelMutex);
				return d->device.getFrontPanelSnapshot();
			};
			callbacks.sendSysex = [d](const std::vector<uint8_t>& _sysex)
			{
				if(!_sysex.empty())
					d->postSysex(_sysex.data(), _sysex.size());
			};
			callbacks.resource = [dir](const std::string& _path, std::string& _data, std::string& _mime)
			{
				// No skins here: the page's optional skin sheet is empty.
				if(_path == "skin/remote.css")
				{
					_data.clear();
					_mime = "text/css";
					return true;
				}
				if(_path.empty() || _path.find("..") != std::string::npos || _path.rfind("skin/", 0) == 0)
					return false;
				std::ifstream file(dir + "/" + _path, std::ios::binary);
				if(!file)
					return false;
				std::ostringstream content;
				content << file.rdbuf();
				_data = content.str();
				_mime.clear();
				return true;
			};
			const auto model = _d.model;
			// The machine selector needs the current track, which the plug-in learns from its
			// controller; a headless unit does not know it, so the catalogue says -1 and assigning
			// from the page is off.
			callbacks.machineInfo = [model]
			{
				return md::machines::toJson(model, false, -1, {});
			};
			callbacks.assignMachine = [](uint16_t)
			{
				return false;
			};
			auto server = std::make_unique<md::RemotePanelServer>(_d.model, _info->port, std::move(callbacks));
			if(!server->start())
				return fail(&_d, GM_ERROR_REJECTED, "no free port for the remote panel");
			if(_boundPort)
				*_boundPort = server->getPort();
			_d.remote = std::move(server);
			return GM_OK;
		});
	}

	GM_API gm_status gm_device_post_sysex(gm_device* _device, const uint8_t* _data, const uint32_t _size)
	{
		// Thread-safe by design, so not through guarded(): it must not touch _device->error,
		// which belongs to the device's own thread.
		if(!_device || !_data || _size < 2 || _data[0] != 0xf0 || _data[_size - 1] != 0xf7)
			return GM_ERROR_ARGUMENT;
		try
		{
			return _device->postSysex(_data, _size) ? GM_OK : GM_ERROR_REJECTED;
		}
		catch(...)
		{
			return GM_ERROR_INTERNAL;
		}
	}

	GM_API void gm_device_remote_panel_stop(gm_device* _device)
	{
		if(!_device)
			return;
		try
		{
			_device->remote.reset();
		}
		catch(...)
		{
		}
	}

	GM_API int32_t gm_device_remote_panel_clients(const gm_device* _device)
	{
		return _device && _device->remote ? static_cast<int32_t>(_device->remote->getClientCount()) : 0;
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
			const std::lock_guard panelLock(_d.panelMutex);
			if(!_d.device.commitDeferredStateRestore(*prepared, generation))
				return fail(&_d, GM_ERROR_STATE, "the deferred state could not be committed");
			return GM_OK;
		});
	}

	GM_API gm_status gm_get_device_api(const uint32_t _abiMajor, const gm_device_api** _api)
	{
		static const gm_device_api api = {
			sizeof(gm_device_api),
			GM_DEVICE_ABI_VERSION,
			GM_DEVICE_ABI_MINOR,
			modelCapabilities(md::MachineModel::Machinedrum) | modelCapabilities(md::MachineModel::Monomachine),
			"libgm_md: Machinedrum, Monomachine",
			gm_device_create,
			gm_device_destroy,
			gm_device_last_error,
			gm_device_get_info,
			gm_device_prepare,
			gm_device_render,
			gm_device_send_midi,
			gm_device_is_audio_ready,
			gm_device_is_midi_ready,
			gm_device_advance,
			gm_device_send_panel,
			gm_device_get_state,
			gm_device_set_state,
			gm_device_is_restore_pending,
			gm_device_service_restore,
			gm_device_post_sysex,
			gm_device_remote_panel_start,
			gm_device_remote_panel_stop,
			gm_device_remote_panel_clients,
		};
		if(!_api)
			return GM_ERROR_ARGUMENT;
		if(_abiMajor != GM_DEVICE_ABI_VERSION)
		{
			*_api = nullptr;
			return GM_ERROR_REJECTED;
		}
		*_api = &api;
		return GM_OK;
	}
}
