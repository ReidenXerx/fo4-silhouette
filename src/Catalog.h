#pragma once

// The catalog the generator writes (S-19): every preset that fits this install, the compiled rules,
// ORefit's sets, and -- from manifests/<stamp>.json -- what every marker any build wrote means. No game
// types in here: the offline tests load the same code.

namespace SH
{
	// Markers Silhouette writes that are not a preset's (the generator reserves both names).
	inline constexpr std::string_view kBlacklistMarker = "Silhouette_Blacklisted";  // S-23, unkeyed
	inline constexpr std::string_view kRefitMarker = "Silhouette_Refit";            // S-40, under the refit keyword
	inline constexpr std::string_view kChoiceMarker = "Silhouette_Chosen";          // S-51, unkeyed: who chose the body

	enum class MarkerKind : std::int32_t
	{
		kNone = 0,
		kBody = 1,    // unkeyed: a preset's marker, or the blacklist marker
		kRefit = 2,   // under Silhouette's refit keyword
		kChoice = 3,  // unkeyed: the source of a chosen body (picker, API), beside its marker
	};

	[[nodiscard]] MarkerKind KindOf(std::string_view a_morph);

	// A form named by the plugin that defines it and its id without the load-order byte, as the
	// generator reads it from the files. The game side resolves it through TESDataHandler.
	struct FormRef
	{
		std::string   plugin;
		std::uint32_t id{ 0 };

		[[nodiscard]] bool Is(std::string_view a_plugin, std::uint32_t a_id) const;
	};

	struct Preset
	{
		std::string                                name;
		bool                                       female{ false };
		std::string                                marker;
		std::vector<std::pair<std::string, float>> values;
		bool                                       random{ false };  // in the random pool
		bool                                       menu{ false };    // offered by the pickers
		bool                                       zeroed{ false };
		std::string                                fit;     // "full" or "partial"
		std::string                                family;  // the preset's declared body family
		bool                                       installed{ false };  // S-76: the player's own, read in game
	};

	// S-76: the BodySlide slider set a body was built with -- each morph slider's default and inversion --
	// so a player's own preset becomes morph values exactly as the generator's base_body.resolve() makes them.
	struct BodySliderSet
	{
		std::string name;
		struct Slider
		{
			std::string name;
			float       defaultValue{ 0.0F };
			bool        invert{ false };
		};
		std::vector<Slider> sliders;
	};

	// One floor of a refit (S-40): while dressed she has at least this value, under Silhouette's own
	// keyword. heavyOnly: only under heavy clothes (S-42).
	struct RefitFloor
	{
		std::string morph;
		float       value{ 0.0F };
		bool        heavyOnly{ false };
	};

	struct RefitSet
	{
		std::string             name;
		bool                    female{ false };
		std::vector<RefitFloor> floors;
	};

	struct NameRule
	{
		std::string              name;
		bool                     female{ false };
		std::vector<std::string> presets;
	};

	struct FactionRule
	{
		FormRef                  faction;
		std::string              editorID;  // what the config said, for the log
		bool                     female{ false };
		std::vector<std::string> presets;
		bool                     pool{ false };  // one of Silhouette's own faction pools (S-72): MCM can switch it off (S-73)
	};

	// S-86: a race another mod adds whose NPCs wear the body of one sex whatever the game's sex flag says
	// (Servitron: robots are flagged male and wear CBBE parts), and whose bodies the plugin draws itself --
	// BodyGen's race lines never reach NPCs made from templates. presets repeat as the random line does.
	struct RacePool
	{
		std::string              race;  // editor id
		bool                     female{ true };
		std::vector<std::string> presets;
		std::vector<std::string> without;  // S-87: morphs never written on this race (Servitron: no breast sliders)
	};

	// refitOutfitPresetsFemale / refitOutfitPresetsMale: an outfit, by its in-game name, that brings
	// its own refit set. Names, as OBody's users write them.
	struct OutfitRefit
	{
		std::string outfit;
		bool        female{ false };
		std::string refitSet;
	};

	// A morph BodyGen rolls per NPC (S-17, S-21): `Morph@low:high` in every template of that sex. A
	// body the plugin gives draws it the same way, from the reference id.
	struct VarietyRange
	{
		std::string morph;
		float       low{ 0.0F };
		float       high{ 0.0F };
		std::string group;  // "nipples" or "genitals": what SetNippleRand / SetGenitalRand switch
	};

	// What a marker of some build means: the preset, and the morphs that template wrote.
	struct ManifestEntry
	{
		std::string              preset;
		bool                     female{ false };
		std::vector<std::string> morphs;
	};

	class Catalog
	{
	public:
		int           schema{ 0 };
		std::string   build;
		std::uint32_t stamp{ 0 };
		std::string   mode;
		std::string   rulesHash;  // the BodyGen lines and the runtime rules, as the templates header states it

		std::vector<Preset>       presets;
		std::string               playerDefault[2];  // [0] male, [1] female
		std::vector<std::string>  states[2];         // runtime states (S-16)
		std::vector<std::string>  neverInBody[2];    // never written into a body: states and the shaft (S-29)
		std::vector<VarietyRange> variety[2];
		std::optional<BodySliderSet> sliderSets[2];  // S-76, [0] male, [1] female; absent in an older catalog

		// [0] male, [1] female: an installed body Silhouette can shape (Presets::MeasureBody, set at load). A sex
		// without one is left alone like a race Silhouette does not distribute to.
		bool bodySupported[2]{ true, true };

		// Tiers BodyGen already carries -- the plugin must know them to leave those NPCs alone.
		std::vector<std::string> races;  // distributeRaces, editor ids
		std::vector<FormRef>     npcFormIDRules[2];
		std::vector<FormRef>     blacklistedNpcsFormID;
		std::vector<std::string> blacklistedPlugins[2];
		std::vector<std::string> blacklistedRaces[2];

		// Tiers only the runtime can see (S-23).
		std::vector<NameRule>    nameRules;
		std::vector<std::string> blacklistedNpcNames;
		std::vector<FactionRule> factionRules;
		std::vector<RacePool>    racePools;  // S-86

		// S-86: the body sex of a race with a pool of its own, whatever an actor's sex flag says; none otherwise.
		[[nodiscard]] std::optional<bool> BodyFemaleOf(std::string_view a_race) const;
		[[nodiscard]] const RacePool*     RacePoolOf(std::string_view a_race) const;

		// ORefit (S-20 slots and order, S-40 floors, S-42 heavy clothes), OBody's keys for the lists.
		std::vector<int>         clothedSlots;  // biped slot numbers, 30..61
		std::vector<FormRef>     outfitBlacklist;
		std::vector<std::string> outfitBlacklistNames;
		std::vector<std::string> outfitBlacklistPlugins;
		std::vector<FormRef>     forceRefit;
		std::vector<std::string> forceRefitNames;
		std::vector<OutfitRefit> outfitRefits;
		std::vector<RefitSet>    refitSets;
		std::vector<std::string> heavyWords;  // S-48: a word or phrase in an item's name that makes it heavy
		std::vector<FormRef>     heavyItems;
		std::vector<std::string> heavyNames;
		std::vector<FormRef>     lightItems;
		std::vector<std::string> lightNames;

		// By name, ignoring case: names arrive through the engine's string pool, which keeps the first
		// spelling it ever saw.
		[[nodiscard]] const Preset*              Find(std::string_view a_name, bool a_female) const;
		[[nodiscard]] const Preset*              FindByMarker(std::string_view a_marker) const;
		[[nodiscard]] std::vector<const Preset*> MenuPresets(bool a_female) const;
		[[nodiscard]] const RefitSet*            FindRefit(std::string_view a_name, bool a_female) const;
		[[nodiscard]] bool                       NeverInBody(bool a_female, std::string_view a_morph) const;

		// S-48: the word of heavyWords an item's name holds as a whole word (or phrase), "" for none.
		[[nodiscard]] std::string HeavyWord(std::string_view a_itemName) const;

		// The refit set an outfit brings by its name, or "".
		[[nodiscard]] std::string OutfitRefitSet(std::string_view a_outfitName, bool a_female) const;

		// The refit for someone wearing this body: the outfit's own set if any, then
		// "<Preset>-Refit", then "Female-Refit"/"Male-Refit", then the built-in set.
		[[nodiscard]] const RefitSet* RefitFor(std::string_view a_preset, bool a_female, std::string_view a_outfitSet) const;

		// A marker of ANY build: the preset it names, from that build's manifest.
		void AddManifest(std::uint32_t a_stamp, std::unordered_map<std::string, ManifestEntry> a_markers);
		[[nodiscard]] std::optional<std::string> PresetForMarker(std::string_view a_marker, std::uint32_t a_stamp) const;
		[[nodiscard]] std::size_t                ManifestCount() const { return _manifests.size(); }

		// The morphs a body of this marker and build holds that no body may hold now (S-16, S-29): what
		// the touch-up zeroes. Empty for this build's own bodies and for a marker no manifest knows.
		[[nodiscard]] std::vector<std::string> HealFor(std::string_view a_marker, std::uint32_t a_stamp) const;

	private:
		std::unordered_map<std::uint32_t, std::unordered_map<std::string, ManifestEntry>> _manifests;
	};

	// Parses catalog.json. On failure returns nullopt and says why in a_error: a catalog that half
	// parses is refused whole, because acting on half the rules is worse than acting on none.
	[[nodiscard]] std::optional<Catalog> ParseCatalog(const nlohmann::json& a_doc, std::string& a_error);

	// manifests/<stamp>.json -> marker -> what it means.
	[[nodiscard]] std::optional<std::pair<std::uint32_t, std::unordered_map<std::string, ManifestEntry>>>
		ParseManifest(const nlohmann::json& a_doc, std::string& a_error);

	[[nodiscard]] bool IEquals(std::string_view a_lhs, std::string_view a_rhs);

	// What the header of a BodyGen file says about the run that wrote it (S-19): the generator writes
	// "... Build <hex>, marker stamp <n> (<mode>), rules <hex>." on both files, and the plugin refuses
	// files and a catalog of different runs. rules is "" for a build that does not state it.
	struct FilesHeader
	{
		std::string   build;
		std::uint32_t stamp{ 0 };
		std::string   rules;
	};

	// The first of a file's opening lines that names a build, or nullopt.
	[[nodiscard]] std::optional<FilesHeader> ParseFilesHeader(std::istream& a_in);
}
