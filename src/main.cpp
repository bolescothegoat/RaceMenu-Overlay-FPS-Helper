#include "Cull.h"

namespace
{
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "OverlayFpsHelper.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v"s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* msg)
	{
		switch (msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("msg: DataLoaded");
			Cull::Init();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Cull::Reset();  // never keep overlay shapes alive across a load
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v{};
	v.PluginVersion({ 1, 0, 0, 0 });
	v.PluginName("OverlayFpsHelper");
	v.AuthorName("Bolescothegoat");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	// no CompatibleVersions list: an Address Library plugin loads on every AE runtime (1.6.x, 1.7.x). This plugin only walks the scene graph through
	// CommonLibSSE-NG's own functions and reads class members, so it carries no engine addresses of its own.
	return v;
}();

// Skyrim SE 1.5.97 uses the old SKSE entry point instead of SKSEPlugin_Version
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "OverlayFpsHelper";
	a_info->version = REL::Version(1, 0, 0, 0).pack();
	return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();
	SKSE::Init(a_skse);
	logger::info("OverlayFpsHelper 1.0.0 loaded, runtime {}", REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
