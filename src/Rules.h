#pragma once

#include "Catalog.h"

// The rules only the runtime can see (S-23), in OBody's priority (S-11). No game types: the game
// side reads an actor into ActorFacts and hands it over, and the offline tests do the same by hand.

namespace SH
{
	struct ActorFacts
	{
		bool                 female{ false };
		std::string          baseName;      // the name the game shows for them, as OBody's users write it
		std::vector<FormRef> bases;         // the NPC record, then each template BodyGen would also match
		std::string          originPlugin;  // the plugin that defines the NPC record
		std::string          race;          // the race's editor id
		std::vector<FormRef> factions;      // the NPC record's own factions, as OBody reads them
		std::uint32_t        seed{ 0 };     // the reference's id: a rule with several presets picks the same one every time
		std::uint32_t        salt{ 0 };     // Back to random's presses (S-60): each draws again; 0 = the draw by id alone
	};

	enum class Tier
	{
		kNone,           // BodyGen's roll stands (or the NPC is not ours at all)
		kNameBlacklist,  // bare, and marked so BodyGen never rolls them again
		kName,           // the per-NPC preset by name
		kFaction,        // the faction's preset, or a race's own pool (S-86)
	};

	struct Verdict
	{
		Tier        tier{ Tier::kNone };
		std::string preset;               // for kName and kFaction: the draw
		std::vector<std::string> options;  // for kName and kFaction: every preset the rule lists (S-52)
		std::string why;                  // one line for the log
		bool        blacklisted{ false };  // any blacklist tier: name, form id, plugin or race (S-41)
		bool        bodyGen{ false };      // BodyGen gives them a body: their form-id line, or the roll (S-68)
	};

	// a_factionPools: Silhouette's own faction pools take part (S-72); MCM's switch can leave them out (S-73),
	// and then a faction is drawn from the random pool like everyone else. A user's faction rule applies either way.
	[[nodiscard]] Verdict Decide(const Catalog& a_catalog, const ActorFacts& a_actor, bool a_factionPools = true);

	[[nodiscard]] std::string_view TierName(Tier a_tier);
}
