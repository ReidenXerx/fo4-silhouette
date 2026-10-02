#include "Plan.h"

namespace SH
{
	namespace
	{
		std::uint32_t Fnv1a(std::string_view a_text, std::uint32_t a_hash = 2166136261u)
		{
			for (const unsigned char c : a_text) {
				a_hash ^= static_cast<std::uint32_t>(std::tolower(c));
				a_hash *= 16777619u;
			}
			return a_hash;
		}

		std::uint32_t Mix(std::uint32_t a_x)
		{
			a_x = (a_x ^ (a_x >> 16)) * 0x85EBCA6Bu;
			a_x = (a_x ^ (a_x >> 13)) * 0xC2B2AE35u;
			return a_x ^ (a_x >> 16);
		}

		bool On(const VarietyRange& a_range, VarietySwitches a_switches)
		{
			return a_range.group == "nipples" ? a_switches.nipples : a_switches.genitals;
		}

		const VarietyRange* RangeOf(const Catalog& a_catalog, bool a_female, std::string_view a_morph)
		{
			for (const auto& r : a_catalog.variety[a_female ? 1 : 0]) {
				if (IEquals(r.morph, a_morph)) {
					return &r;
				}
			}
			return nullptr;
		}

		const float* Held(const std::unordered_map<std::string, float>* a_keep, std::string_view a_morph)
		{
			if (!a_keep) {
				return nullptr;
			}
			for (const auto& [morph, value] : *a_keep) {
				if (IEquals(morph, a_morph)) {
					return &value;
				}
			}
			return nullptr;
		}
	}

	float Draw(std::uint32_t a_seed, std::string_view a_morph, float a_low, float a_high)
	{
		if (!(a_low < a_high)) {
			return a_low;
		}
		// 24 random bits: every one of them lands exactly in a float's mantissa.
		const auto bits = Mix(Mix(a_seed + 0x9E3779B9u) ^ Fnv1a(a_morph)) >> 8;
		const auto u = static_cast<double>(bits) / static_cast<double>(1u << 24);  // [0, 1)
		return static_cast<float>(a_low + (static_cast<double>(a_high) - a_low) * u);
	}

	Morphs BodyFor(const Catalog& a_catalog, const Preset& a_preset, std::uint32_t a_seed, VarietySwitches a_switches,
		const std::unordered_map<std::string, float>* a_keep)
	{
		const bool female = a_preset.female;
		Morphs     out;
		out.reserve(a_preset.values.size() + a_catalog.variety[female ? 1 : 0].size() + 1);

		for (const auto& [morph, value] : a_preset.values) {
			if (a_catalog.NeverInBody(female, morph)) {
				continue;
			}
			if (const auto* r = RangeOf(a_catalog, female, morph); r && On(*r, a_switches)) {
				continue;  // the range decides this morph
			}
			out.emplace_back(morph, value);
		}
		// Every enabled range, whether or not the preset names the morph: BodyGen writes the range into
		// every template of the sex (S-17, S-21), so a preset that never touched a nipple still gets one.
		for (const auto& r : a_catalog.variety[female ? 1 : 0]) {
			if (!On(r, a_switches) || a_catalog.NeverInBody(female, r.morph)) {
				continue;
			}
			const float* held = Held(a_keep, r.morph);
			const float  v = held && *held >= r.low && *held <= r.high ? *held : Draw(a_seed, r.morph, r.low, r.high);
			out.emplace_back(r.morph, v);
		}
		std::erase_if(out, [](const auto& p) { return std::abs(p.second) < 1e-6F; });
		out.emplace_back(a_preset.marker, static_cast<float>(a_catalog.stamp));
		return out;
	}

	std::string MenuList(const Catalog& a_catalog, bool a_female)
	{
		std::string out;
		for (const auto* p : a_catalog.MenuPresets(a_female)) {
			if (!out.empty()) {
				out += '|';
			}
			out += p->name;
			out += '\t';
			out += p->installed ? 'y' : p->random ? 'p' : 'o';
		}
		return out;
	}

	Morphs PlayerBody(const Catalog& a_catalog, const Preset& a_preset)
	{
		return BodyFor(a_catalog, a_preset, 0, { .nipples = false, .genitals = false });
	}

	Morphs TopUp(const Catalog& a_catalog, bool a_female, std::uint32_t a_seed, VarietySwitches a_switches,
		const std::vector<std::string>& a_present)
	{
		Morphs out;
		for (const auto& r : a_catalog.variety[a_female ? 1 : 0]) {
			if (!On(r, a_switches) || a_catalog.NeverInBody(a_female, r.morph)) {
				continue;
			}
			if (std::ranges::any_of(a_present, [&](const std::string& p) { return IEquals(p, r.morph); })) {
				continue;
			}
			const float v = Draw(a_seed, r.morph, r.low, r.high);
			if (std::abs(v) >= 1e-6F) {
				out.emplace_back(r.morph, v);
			}
		}
		return out;
	}

	Morphs RefitFloors(const RefitSet& a_set, bool a_heavy)
	{
		Morphs out;
		for (const auto& f : a_set.floors) {
			if ((f.heavyOnly && !a_heavy) || !(f.value > 0.0F)) {
				continue;
			}
			const auto it = std::ranges::find_if(out, [&](const auto& p) { return IEquals(p.first, f.morph); });
			if (it != out.end()) {
				it->second = std::max(it->second, f.value);
			} else {
				out.emplace_back(f.morph, f.value);
			}
		}
		return out;
	}

	namespace
	{
		std::uint32_t Mix(std::uint32_t a_hash, std::string_view a_text)
		{
			for (const char c : a_text) {
				a_hash ^= static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c)));
				a_hash *= 16777619u;
			}
			a_hash ^= 0xFFu;  // a separator, so "ab"+"c" and "a"+"bc" differ
			a_hash *= 16777619u;
			return a_hash;
		}

		std::uint32_t MixWord(std::uint32_t a_hash, std::uint32_t a_word)
		{
			for (int i = 0; i < 4; ++i) {
				a_hash ^= (a_word >> (8 * i)) & 0xFFu;
				a_hash *= 16777619u;
			}
			return a_hash;
		}
	}

	float RefitMarker(const RefitSet& a_set, bool a_heavy)
	{
		std::uint32_t h = Mix(2166136261u, a_set.name);
		h = MixWord(h, a_heavy ? 1u : 0u);
		for (const auto& [morph, value] : RefitFloors(a_set, a_heavy)) {
			h = Mix(h, morph);
			h = MixWord(h, std::bit_cast<std::uint32_t>(value));
		}
		const auto k = h % 8388606u;  // 1 + 1 + 2 * 8388605 = 16777212: below 2^24, exact as a float
		return static_cast<float>(1 + (a_heavy ? 1 : 0) + 2 * k);
	}

	std::uint32_t TouchKey(const Catalog& a_catalog, std::string_view a_marker, std::uint32_t a_stamp, bool a_female,
		VarietySwitches a_switches)
	{
		std::uint32_t h = Mix(2166136261u, a_marker);
		h = MixWord(h, a_stamp);
		auto heal = a_catalog.HealFor(a_marker, a_stamp);
		std::ranges::sort(heal);
		for (const auto& m : heal) {
			h = Mix(h, m);
		}
		std::vector<std::string> ranges;
		for (const auto& r : a_catalog.variety[a_female ? 1 : 0]) {
			const bool on = r.group == "nipples" ? a_switches.nipples : a_switches.genitals;
			if (on && !a_catalog.NeverInBody(a_female, r.morph)) {
				ranges.push_back(r.morph);
			}
		}
		std::ranges::sort(ranges);
		for (const auto& m : ranges) {
			h = Mix(h, m);
		}
		return h ? h : 1u;
	}

	std::uint32_t BodyHash(std::string_view a_marker, std::uint32_t a_stamp)
	{
		const auto h = Mix(Fnv1a(a_marker) ^ Mix(a_stamp));
		return h == 0 ? 1 : h;  // 0 means "none"
	}
}
