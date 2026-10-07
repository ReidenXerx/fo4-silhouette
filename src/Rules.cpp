#include "Rules.h"

namespace SH
{
	namespace
	{
		bool Listed(const std::vector<FormRef>& a_list, const FormRef& a_form)
		{
			return std::ranges::any_of(a_list, [&](const FormRef& r) { return r.Is(a_form.plugin, a_form.id); });
		}

		// BodyGen matches a form-id line on the NPC record or any template up its chain.
		bool AnyListed(const std::vector<FormRef>& a_list, const std::vector<FormRef>& a_forms)
		{
			return std::ranges::any_of(a_forms, [&](const FormRef& f) { return Listed(a_list, f); });
		}

		bool ListedName(const std::vector<std::string>& a_list, std::string_view a_name)
		{
			return std::ranges::any_of(a_list, [&](const std::string& s) { return IEquals(s, a_name); });
		}

		// One of several presets, always the same one for the same person: OBody draws at random and
		// keeps the draw in its registry; this draws from the reference id, so the draw needs no
		// memory to be the same next time -- the co-save still records it (S-25). Back to random draws
		// again (S-60): the salt its presses left moves the draw, and salt 0 is exactly the draw by id.
		std::string Pick(const std::vector<std::string>& a_presets, std::uint32_t a_seed, std::uint32_t a_salt)
		{
			if (a_presets.empty()) {
				return {};
			}
			// splitmix32-style mix, so neighbouring ids do not all land on the same entry
			std::uint32_t x = a_seed + 0x9E3779B9u * (a_salt + 1);
			x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
			x = (x ^ (x >> 13)) * 0xC2B2AE35u;
			x ^= x >> 16;
			return a_presets[x % a_presets.size()];
		}
	}

	std::string_view TierName(Tier a_tier)
	{
		switch (a_tier) {
		case Tier::kNone:
			return "none"sv;
		case Tier::kNameBlacklist:
			return "blacklisted by name"sv;
		case Tier::kName:
			return "npc (by name)"sv;
		case Tier::kFaction:
			return "faction"sv;
		}
		return "?"sv;
	}

	Verdict Decide(const Catalog& a_catalog, const ActorFacts& a_actor, bool a_factionPools)
	{
		const int sex = a_actor.female ? 1 : 0;

		// Races Silhouette does not distribute to are not ours at all (S-11 distributeRaces).
		if (!ListedName(a_catalog.races, a_actor.race)) {
			return { Tier::kNone, {}, {}, std::format("race {} is not distributed", a_actor.race) };
		}

		// 1. per-NPC blacklist -- by form id BodyGen already keeps them bare; by name is ours.
		if (AnyListed(a_catalog.blacklistedNpcsFormID, a_actor.bases)) {
			return { Tier::kNone, {}, {}, "blacklisted by form id (BodyGen keeps them bare)", true };
		}
		if (!a_actor.baseName.empty() && ListedName(a_catalog.blacklistedNpcNames, a_actor.baseName)) {
			return { Tier::kNameBlacklist, {}, {}, std::format("\"{}\" is blacklisted by name", a_actor.baseName), true };
		}

		// 2. per-NPC preset -- by form id BodyGen already did it; by name is ours.
		if (AnyListed(a_catalog.npcFormIDRules[sex], a_actor.bases)) {
			return { Tier::kNone, {}, {}, "has a per-NPC preset by form id (BodyGen's)", false, true };
		}
		if (!a_actor.baseName.empty()) {
			for (const auto& rule : a_catalog.nameRules) {
				if (rule.female == a_actor.female && IEquals(rule.name, a_actor.baseName)) {
					auto preset = Pick(rule.presets, a_actor.seed, a_actor.salt);
					return { Tier::kName, preset, rule.presets, std::format("npc rule \"{}\" -> {}", rule.name, preset) };
				}
			}
		}

		// 3. plugin and race blacklists outrank a faction: BodyGen keeps those bare.
		if (ListedName(a_catalog.blacklistedPlugins[sex], a_actor.originPlugin)) {
			return { Tier::kNone, {}, {}, std::format("plugin {} is blacklisted (BodyGen keeps them bare)", a_actor.originPlugin), true };
		}
		if (ListedName(a_catalog.blacklistedRaces[sex], a_actor.race)) {
			return { Tier::kNone, {}, {}, std::format("race {} is blacklisted (BodyGen keeps them bare)", a_actor.race), true };
		}

		// 4. faction -- the first rule, in the config's order, whose faction the NPC record carries.
		for (const auto& rule : a_catalog.factionRules) {
			if (rule.pool && !a_factionPools) {
				continue;  // switched off in MCM (S-73): the faction is drawn from the random pool
			}
			if (rule.female == a_actor.female && Listed(a_actor.factions, rule.faction)) {
				auto preset = Pick(rule.presets, a_actor.seed, a_actor.salt);
				return { Tier::kFaction, preset, rule.presets, std::format("faction {} -> {}", rule.editorID, preset) };
			}
		}

		// 4b. S-86: a race BodyGen cannot reach draws from its own pool, as a faction's rule does.
		for (const auto& pool : a_catalog.racePools) {
			if (pool.female == a_actor.female && IEquals(pool.race, a_actor.race)) {
				auto preset = Pick(pool.presets, a_actor.seed, a_actor.salt);
				return { Tier::kFaction, preset, pool.presets, std::format("race {} draws from its own pool (S-86) -> {}", pool.race, preset) };
			}
		}

		// 5. plugin, race, random: BodyGen's.
		return { Tier::kNone, {}, {}, "no runtime rule applies (BodyGen's roll stands)", false, true };
	}
}
