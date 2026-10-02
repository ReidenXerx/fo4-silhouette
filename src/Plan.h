#pragma once

#include "Catalog.h"

// The values the bridge writes, worked out without the game, so the offline tests check exactly what
// a body will hold.

namespace SH
{
	using Morphs = std::vector<std::pair<std::string, float>>;

	// SetNippleRand / SetGenitalRand (S-24). They change the bodies the plugin gives and tops up;
	// BodyGen's own rolls come from the files and always carry their ranges.
	struct VarietySwitches
	{
		bool nipples{ true };
		bool genitals{ true };
	};

	// A value in [low, high], the same every time for this person and this morph, and spread across
	// people. BodyGen draws uniformly in the same range, but anew each time it rolls.
	[[nodiscard]] float Draw(std::uint32_t a_seed, std::string_view a_morph, float a_low, float a_high);

	// Everything a preset gives one person, marker last, as BodyGen writes a template: the preset's
	// values, each variety range drawn in place of the preset's own value (S-21), nothing that is never
	// part of a body (S-16, S-29), and zeroes dropped -- LooksMenu stores 0 as "no entry".
	//
	// a_keep: what the person holds now. A variety value they already have, inside its range, is kept:
	// giving the same preset again (Refresh, Reapply) leaves the variety BodyGen rolled for them.
	[[nodiscard]] Morphs BodyFor(const Catalog& a_catalog, const Preset& a_preset, std::uint32_t a_seed, VarietySwitches a_switches,
		const std::unordered_map<std::string, float>* a_keep = nullptr);

	// S-79 (0.3.3), the window's Me tab: the presets offered for the player, "name<TAB>kind" joined by "|" as
	// the NPC picker lists them (y the player's own BodySlide presets, S-76; p the random pool; o the rest) --
	// the catalog's menu, so presets read in game are there, not only those the player script was built with.
	[[nodiscard]] std::string MenuList(const Catalog& a_catalog, bool a_female);

	// The player's body for a preset, as the player script writes one: the preset's values and its marker, no
	// variety drawn (the player is never rolled).
	[[nodiscard]] Morphs PlayerBody(const Catalog& a_catalog, const Preset& a_preset);

	// The variety an existing body lacks (S-44): a drawn value for every enabled range whose morph is not
	// in a_present -- the morphs her OWN layer holds, not other mods' keyed ones. Nothing else.
	[[nodiscard]] Morphs TopUp(const Catalog& a_catalog, bool a_female, std::uint32_t a_seed, VarietySwitches a_switches,
		const std::vector<std::string>& a_present);

	// What this build wants of a body it touches up: the marker and stamp, what the heal zeroes and which
	// ranges the top-up draws. Once a body is touched with a key, only a build that wants something new
	// of it (another heal, another range) touches it again (S-44).
	[[nodiscard]] std::uint32_t TouchKey(const Catalog& a_catalog, std::string_view a_marker, std::uint32_t a_stamp, bool a_female,
		VarietySwitches a_switches);

	// The floors of a set that apply (heavy-only floors under heavy clothes, S-42), without the marker.
	// A morph named twice keeps its highest floor.
	[[nodiscard]] Morphs RefitFloors(const RefitSet& a_set, bool a_heavy);

	// The refit marker's value (S-40): which set, which floors, and whether heavy -- a hash that fits a
	// float exactly, ODD for light clothes and EVEN for heavy. Another mod may read the parity (S-50);
	// a build that changes a set's floors gives a new value, so a probe sees the refit is stale.
	[[nodiscard]] float RefitMarker(const RefitSet& a_set, bool a_heavy);

	// A marker and the stamp it was written with, as one number, so the co-save can tell whether the
	// body it knows is still the body the actor has.
	[[nodiscard]] std::uint32_t BodyHash(std::string_view a_marker, std::uint32_t a_stamp);
}
