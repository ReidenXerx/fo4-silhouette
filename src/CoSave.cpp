#include "CoSave.h"

#include "Compat.h"
#include "Game.h"

namespace SH::CoSave
{
	namespace
	{
		constexpr std::uint32_t kPluginID = 'SLHT';
		// The records, in every version: a later version appends fields inside its items (each carries its
		// length) rather than adding records or bumping the version. A record of another type is skipped
		// here and not written back.
		constexpr std::uint32_t kRecords = 'REC1';
		// S-68: "Reset everyone" is not about one reference, so it is not an item of the records; it has a
		// record of its own, written only once it has been pressed. An older plugin skips it: it has no
		// such button, and the reset is simply not followed there.
		constexpr std::uint32_t kReset = 'RST1';

		// Records a newer Silhouette wrote, which this one cannot read: kept exactly and written back, so
		// going back to the newer version finds them. Only touched by the co-save callbacks and Revert.
		struct Newer
		{
			std::uint32_t          version{ 0 };
			std::vector<std::byte> bytes;
		};
		std::optional<Newer> g_newer;

		// Registry's rule (KeepInCoSave), with the game's answer for a created reference. Runs under the
		// director's lock: reads the game only, never the director.
		bool Keep(std::uint32_t a_ref, std::uint32_t a_base, bool a_intent)
		{
			std::optional<std::uint32_t> live;
			if (!a_intent && (a_ref >> 24) == 0xFF) {
				if (auto* actor = Game::ActorFor(a_ref)) {
					live = Game::BaseOf(actor);
				}
			}
			return KeepInCoSave(a_ref, a_base, a_intent, live);
		}

		void OnSave(const F4SE::SerializationInterface* a_intfc)
		{
			if (g_newer) {
				if (!a_intfc->OpenRecord(kRecords, g_newer->version) ||
					!a_intfc->WriteRecordData(g_newer->bytes.data(), static_cast<std::uint32_t>(g_newer->bytes.size()))) {
					logger::error("co-save: could not write back the newer version's records");
				}
				logger::warn("co-save: this save keeps the records a newer Silhouette wrote (version {}), unchanged - nothing this version decided "
							 "is remembered in it",
					g_newer->version);
				return;
			}
			const auto bytes = Game::TheDirector().SaveRecords(Keep);
			if (!a_intfc->OpenRecord(kRecords, Registry::kVersion) ||
				!a_intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
				logger::error("co-save: could not write the records - this save will not remember who Silhouette shaped");
				return;
			}
			std::uint32_t written = 0;
			if (bytes.size() >= sizeof(written)) {
				std::memcpy(&written, bytes.data(), sizeof(written));
			}
			logger::info("co-save: {} record(s) written ({} held)", written, Game::TheDirector().RecordCount());
			if (const auto reset = Game::TheDirector().SaveReset(); !reset.empty()) {
				if (!a_intfc->OpenRecord(kReset, Registry::kResetVersion) ||
					!a_intfc->WriteRecordData(reset.data(), static_cast<std::uint32_t>(reset.size()))) {
					logger::error("co-save: could not write Reset everyone - this save will not decide anyone met later again");
				}
			}
		}

		void OnLoad(const F4SE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0;
			std::uint32_t version = 0;
			std::uint32_t length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kReset) {
					std::vector<std::byte> bytes(length);
					std::string            error;
					const auto             resolve = [&](std::uint32_t a_saved) { return a_intfc->ResolveFormID(a_saved).value_or(0); };
					if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
						logger::error("co-save: Reset everyone is cut short - not followed in this save");
					} else if (Game::TheDirector().LoadReset(bytes, version, error, resolve) != Registry::Loaded::kOk) {
						logger::error("co-save: Reset everyone not followed in this save ({})", error);
					} else {
						logger::info("co-save: Reset everyone was pressed in this save: bodies from older builds are decided again when met");
					}
					continue;
				}
				if (type != kRecords) {
					logger::warn("co-save: unknown record {:08X} skipped", type);
					continue;
				}
				std::vector<std::byte> bytes(length);
				if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
					logger::error("co-save: the records are cut short - none loaded");
					continue;
				}
				std::string error;
				const auto  resolve = [&](std::uint32_t a_saved) { return a_intfc->ResolveFormID(a_saved).value_or(0); };
				switch (Game::TheDirector().LoadRecords(bytes, version, resolve, error)) {
				case Registry::Loaded::kOk:
					break;
				case Registry::Loaded::kNewer:
					logger::error("co-save: {} - a newer Silhouette made this save. Its records are kept unchanged and written back into every save "
								  "from this game; this version remembers nothing new until the newer one is installed again",
						error);
					g_newer = Newer{ version, std::move(bytes) };
					break;
				case Registry::Loaded::kRefused:
					logger::error("co-save: records not loaded ({})", error);
					break;
				}
			}
			logger::info("co-save: {} record(s) after loading", Game::TheDirector().RecordCount());
		}

		void OnRevert(const F4SE::SerializationInterface*)
		{
			Revert();
		}
	}

	void Revert()
	{
		g_newer.reset();
		Game::TheDirector().RevertRecords();
	}

	bool Register(const F4SE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) {
			return false;
		}
		Compat::SetUniqueID(a_intfc, kPluginID);
		a_intfc->SetRevertCallback(OnRevert);
		a_intfc->SetSaveCallback(OnSave);
		a_intfc->SetLoadCallback(OnLoad);
		return true;
	}
}
