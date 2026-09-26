#include "CoSave.h"
#include "Game.h"
#include "Papyrus.h"
#include "Sinks.h"

namespace
{
	// The log file, opened by its path as it is. spdlog's own file sink takes a NARROW name, and
	// path::string() throws for a folder the ANSI code page cannot hold -- a Cyrillic, Polish or Chinese
	// user name, a localized OneDrive Documents -- and a throw at load makes F4SE disable the whole plugin
	// (wave 5, measured with F4SE's own loader). A short (8.3) name is no way round it: a name short enough
	// to be one, like "Łukasz", gets no ASCII short name (measured).
	class FileSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit FileSink(const std::filesystem::path& a_path) :
			_out(a_path, std::ios::binary | std::ios::trunc)
		{}

	protected:
		void sink_it_(const spdlog::details::log_msg& a_msg) override
		{
			spdlog::memory_buf_t text;
			formatter_->format(a_msg, text);
			_out.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

		void flush_() override { _out.flush(); }

	private:
		std::ofstream _out;
	};

	// After F4SE::Init: log_directory() is built from the save folder name, which Init fills in.
	// The previous run's log is kept as Silhouette.prev.log -- the run that follows a crash is the
	// run that would otherwise erase the only record of it (Rapport's scar). Nothing here may throw:
	// without a log the plugin still runs.
	void InitLogging()
	{
		try {
			auto path = logger::log_directory();
			if (!path) {
				return;
			}
			std::error_code ec;
			std::filesystem::create_directories(*path, ec);
			*path /= SH_PROJECT_NAME ".log"sv;
			auto previous = *path;
			previous.replace_extension(".prev.log");
			std::filesystem::remove(previous, ec);
			std::filesystem::rename(*path, previous, ec);

			auto log = std::make_shared<spdlog::logger>("global log"s, std::make_shared<FileSink>(*path));
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);
			spdlog::set_default_logger(std::move(log));
			spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
		} catch (const std::exception&) {
		}
	}

	void MessageHandler(F4SE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		switch (a_message->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			// Sent twice: data false before the data loads, true once it has. Forms exist only then.
			if (a_message->data) {
				SH::Game::Load();
				SH::Sinks::Attach();
			}
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			// Everything queued belongs to the save being left. FF-prefixed ids are allocated per
			// save: the same number over there is somebody else.
			SH::Game::ForgetInbox();
			SH::Game::TheDirector().ForgetWorld();
			break;
		case F4SE::MessagingInterface::kNewGame:
			// A new game from the main menu sends no kPreLoadGame, and has no records to keep -- nor the
			// ones a newer Silhouette left in the last save loaded. The watchdog is not armed: character
			// creation runs a long while before the bridge's quest.
			SH::Game::ForgetInbox();
			SH::Game::TheDirector().ForgetWorld();
			SH::CoSave::Revert();
			SH::Sinks::Attach();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
			// data: whether the load succeeded. A failed one leaves the game where it was -- and
			// kPreLoadGame already forgot everyone in it, so the sweep reads them again either way.
			SH::Sinks::Attach();
			SH::Game::ArmSweep();
			if (a_message->data) {
				SH::Game::NoteGameLoaded();
			}
			logger::info("after loading{}: {} record(s); {}", a_message->data ? "" : " (the load FAILED)", SH::Game::TheDirector().RecordCount(),
				SH::Sinks::Status());
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = SH_PROJECT_NAME;
	a_info->version = SH_VERSION_MAJOR * 10000 + SH_VERSION_MINOR * 100 + SH_VERSION_PATCH;

	if (a_f4se->IsEditor()) {
		return false;
	}
#ifdef SH_RUNTIME_DATABASE
	// S-75: OG's F4SE asks this; it only ever runs on 1.10.163. NG's and AE's read F4SEPlugin_Version.
	return true;
#else
	// Every address this plugin resolves is an OG 1.10.163 id (S-18). Refusing another runtime is
	// the honest failure; resolving ids that mean something else there is not.
	return a_f4se->RuntimeVersion() == F4SE::RUNTIME_1_10_163;
#endif
}

#ifdef SH_RUNTIME_DATABASE
namespace
{
	// NG's and AE's F4SE load a plugin by this record. Addresses come from Runtime Database
	// (signatures, not one executable's offsets); the layouts it claims are guarded at run time
	// (Game::LayoutProblems): a runtime whose layout differs turns the plugin off, never half on.
	constexpr F4SE::PluginVersionData MakeVersionData() noexcept
	{
		F4SE::PluginVersionData data{};
		data.pluginVersion = (SH_VERSION_MAJOR << 24) | (SH_VERSION_MINOR << 16) | (SH_VERSION_PATCH << 4);
		constexpr std::string_view name = SH_PROJECT_NAME;
		for (std::size_t i = 0; i < name.size() && i < std::size(data.name) - 1; ++i) {
			data.name[i] = name[i];
		}
		data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;
		data.structureIndependence = F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
		                             F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;
		return data;
	}
}

extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = MakeVersionData();
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
#ifdef SH_RUNTIME_DATABASE
	F4SE::Init(a_f4se);
#else
	// false: F4SE's own logger would name the file after an empty plugin name (".log").
	F4SE::Init(a_f4se, false);
#endif
	InitLogging();
	logger::info("{} v{}", SH_PROJECT_NAME, SH_VERSION_STRING);
#ifdef SH_RUNTIME_DATABASE
	{
		const auto& module = REL::Module::get();
		const auto  family = module.is_ae() ? "AE" : module.is_ng() ? "NG" : "OG";
		logger::info("runtime {} ({}), addresses through Runtime Database (S-75)", module.version().string(), family);
	}
#endif

	const auto papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(SH::Papyrus::Register)) {
		logger::critical("could not register the papyrus functions");
		return false;
	}
	if (!SH::CoSave::Register(F4SE::GetSerializationInterface())) {
		logger::error("no co-save: nothing Silhouette decides will be remembered between saves");
	}
	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		logger::critical("could not register the messaging listener");
		return false;
	}
	logger::info("loaded");
	return true;
}
