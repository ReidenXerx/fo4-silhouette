#pragma once

#include "Catalog.h"

// S-76: the player's own installed BodySlide presets, read at run time and offered by the pickers (never
// random). The same rules the generator applies (tools/silhouette_gen.py, tools/base_body.py), in C++, so a
// preset looks in game exactly as BodySlide builds it and as the generator would have written it:
//   read_presets   first preset of a name wins (any case); a SetSlider counts by its big (or both) value /100
//   classify/band  fit = share of the preset's sliders the body's .tri carries; full >= 95 %, partial >= 50 %
//                  only for the body's own family; outfit-tuned copies are left out
//   resolve        every morph slider of the set the body was built with: the preset's value, else the
//                  set's default, inverted where the set says so; kept where the body has it and it is not 0
//   plain_marker   Silhouette_ + the name with every run of non-alphanumerics made one '_'
// Pure: no game in it, so the offline tests drive every rule.

namespace SH::Presets
{
	struct SliderPreset
	{
		std::string                                name;
		std::vector<std::string>                   families;  // <Group>s that name a body family
		std::vector<std::pair<std::string, float>> big;       // SetSlider big (or both) values, 0..1
	};

	// Every <Preset> of one BodySlide SliderPresets file.
	[[nodiscard]] std::vector<SliderPreset> ParseXml(std::string_view a_text);

	// Every morph name a BodySlide .tri carries, as written (the generator compares them exactly). Empty for bytes
	// that are not a .tri.
	[[nodiscard]] std::unordered_set<std::string> TriMorphs(std::span<const std::byte> a_bytes);

	// "Silhouette_" + the name, each run of characters other than ASCII letters and digits made one '_', the
	// ends trimmed; "" when nothing is left (tools/silhouette_gen.py plain_marker).
	[[nodiscard]] std::string PlainMarker(std::string_view a_name);

	struct Installed
	{
		std::vector<Preset>      added;
		std::vector<std::string> notes;  // one line per preset left out, and why
	};

	// The catalog's pickers extended by the player's presets: a_files is every SliderPresets file's presets in
	// BodySlide's order (sorted paths); a_morphs[sex] the body's .tri morph names ([0] male, [1] female; empty:
	// that sex is skipped). Names and markers the catalog already has stay the catalog's.
	[[nodiscard]] Installed Resolve(const Catalog& a_catalog, const std::vector<SliderPreset>& a_files,
		const std::unordered_set<std::string> (&a_morphs)[2]);
}
