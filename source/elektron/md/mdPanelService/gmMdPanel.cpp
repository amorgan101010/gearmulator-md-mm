// libgm_md_panel: md::RemotePanelServer driven through the host's callbacks (gm_md_panel.h),
// with no unit of its own.

#include "gm_md_panel.h"

#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdremotepanel.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct gm_md_panel
{
	gm_md_panel_host host{};
	std::string resourceDir;
	std::unique_ptr<md::RemotePanelServer> server;
};

namespace
{
	constexpr size_t g_vramSize = 2 * 8 * 64;
	constexpr size_t g_statusSize = g_vramSize + md::FrontPanel::g_ledBankCount;

	// The panel as the unit last published it; an empty panel while it cannot be read.
	md::FrontPanel snapshotOf(const gm_md_panel_host& _host)
	{
		md::FrontPanel panel;
		uint8_t status[g_statusSize];
		size_t size = 0;
		// A publish overlapping the read is retried a few times; the page asks again soon anyway.
		for(int attempt = 0; attempt < 8; ++attempt)
		{
			if(_host.read_status(_host.user, status, sizeof(status), &size, nullptr) == GM_OK && size == g_statusSize)
			{
				panel.setRaw(status, status + g_vramSize);
				break;
			}
		}
		return panel;
	}
}

extern "C"
{
	GM_API gm_md_panel* gm_md_panel_start(const gm_md_panel_host* _host, int32_t* _boundPort)
	{
		if(!_host || _host->struct_size < sizeof(gm_md_panel_host) || !_host->resource_dir || !_host->read_status
			|| !_host->send_panel || !_host->send_sysex
			|| (_host->model != GM_MODEL_MACHINEDRUM && _host->model != GM_MODEL_MONOMACHINE))
			return nullptr;
		try
		{
			auto panel = std::make_unique<gm_md_panel>();
			panel->host = *_host;
			panel->resourceDir = _host->resource_dir;
			panel->host.resource_dir = panel->resourceDir.c_str();
			const auto model = _host->model == GM_MODEL_MONOMACHINE ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
			gm_md_panel* const p = panel.get();
			md::RemotePanelServer::Callbacks callbacks;
			callbacks.sendPanelEvent = [p](const uint8_t _command, const uint8_t _argument)
			{
				return p->host.send_panel(p->host.user, _command, _argument) != 0;
			};
			callbacks.snapshot = [p] { return snapshotOf(p->host); };
			callbacks.sendSysex = [p](const std::vector<uint8_t>& _sysex)
			{
				if(!_sysex.empty())
					(void)p->host.send_sysex(p->host.user, _sysex.data(), static_cast<uint32_t>(_sysex.size()));
			};
			callbacks.resource = [p](const std::string& _path, std::string& _data, std::string& _mime)
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
				std::ifstream file(p->resourceDir + "/" + _path, std::ios::binary);
				if(!file)
					return false;
				std::ostringstream content;
				content << file.rdbuf();
				_data = content.str();
				_mime.clear();
				return true;
			};
			// The machine selector needs the current track, which only the plug-in's controller knows:
			// the catalogue says -1 and assigning from the page is off, as before in libgm_md.
			callbacks.machineInfo = [model] { return md::machines::toJson(model, false, -1, {}); };
			callbacks.assignMachine = [](uint16_t) { return false; };
			auto server = std::make_unique<md::RemotePanelServer>(model, _host->port, std::move(callbacks));
			if(!server->start())
				return nullptr;
			if(_boundPort)
				*_boundPort = server->getPort();
			panel->server = std::move(server);
			return panel.release();
		}
		catch(...)
		{
			return nullptr;
		}
	}

	GM_API int32_t gm_md_panel_clients(const gm_md_panel* _panel)
	{
		return _panel && _panel->server ? static_cast<int32_t>(_panel->server->getClientCount()) : 0;
	}

	GM_API void gm_md_panel_feed_audio(gm_md_panel* _panel, const float* _left, const float* _right, const uint32_t _frames, const uint32_t _sampleRate)
	{
		if(_panel && _panel->server && _left && _right)
			_panel->server->feedAudio(_left, _right, _frames, _sampleRate);
	}

	GM_API void gm_md_panel_stop(gm_md_panel* _panel)
	{
		try
		{
			delete _panel;	// the server's destructor stops it and joins its threads
		}
		catch(...)
		{
		}
	}
}
