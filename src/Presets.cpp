#include "Presets.h"

#include <regex>

namespace SH::Presets
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// The five XML entities and numeric references, as a BodySlide preset file may hold them.
		std::string Decode(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] != '&') {
					out += a_text[i];
					continue;
				}
				const auto end = a_text.find(';', i);
				if (end == std::string_view::npos || end - i > 10) {
					out += a_text[i];
					continue;
				}
				const auto entity = a_text.substr(i + 1, end - i - 1);
				std::uint32_t code = 0;
				if (entity == "amp") {
					out += '&';
				} else if (entity == "lt") {
					out += '<';
				} else if (entity == "gt") {
					out += '>';
				} else if (entity == "quot") {
					out += '"';
				} else if (entity == "apos") {
					out += '\'';
				} else if (entity.size() > 1 && entity[0] == '#') {
					const bool hex = entity[1] == 'x' || entity[1] == 'X';
					const auto digits = entity.substr(hex ? 2 : 1);
					if (std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10).ec != std::errc{}) {
						out += a_text[i];
						continue;
					}
					// UTF-8, as the rest of the file is read.
					if (code < 0x80) {
						out += static_cast<char>(code);
					} else if (code < 0x800) {
						out += static_cast<char>(0xC0 | (code >> 6));
						out += static_cast<char>(0x80 | (code & 0x3F));
					} else if (code < 0x10000) {
						out += static_cast<char>(0xE0 | (code >> 12));
						out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (code & 0x3F));
					} else {
						out += static_cast<char>(0xF0 | (code >> 18));
						out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
						out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (code & 0x3F));
					}
				} else {
					out += a_text[i];
					continue;
				}
				i = end;
			}
			return out;
		}

		// An attribute of a tag's text ("Preset name=\"x\" set=\"y\""), decoded; nullopt when absent.
		std::optional<std::string> Attr(std::string_view a_tag, std::string_view a_name)
		{
			std::size_t at = 0;
			while ((at = a_tag.find(a_name, at)) != std::string_view::npos) {
				const bool startOk = at > 0 && std::isspace(static_cast<unsigned char>(a_tag[at - 1]));
				auto       p = at + a_name.size();
				while (p < a_tag.size() && std::isspace(static_cast<unsigned char>(a_tag[p]))) {
					++p;
				}
				if (startOk && p < a_tag.size() && a_tag[p] == '=') {
					++p;
					while (p < a_tag.size() && std::isspace(static_cast<unsigned char>(a_tag[p]))) {
						++p;
					}
					if (p < a_tag.size() && (a_tag[p] == '"' || a_tag[p] == '\'')) {
						const auto quote = a_tag[p];
						const auto end = a_tag.find(quote, p + 1);
						if (end != std::string_view::npos) {
							return Decode(a_tag.substr(p + 1, end - p - 1));
						}
					}
				}
				at += a_name.size();
			}
			return std::nullopt;
		}

		// tools/silhouette_gen.py NOT_A_FAMILY and CLOTHED_VARIANT.
		bool NotAFamily(const std::string& a_group)
		{
			static const std::regex re{ R"(outfit|clothing|costume|dress|corset|suit|jumpsuit|armor|armour|\bxy -|2pac)", std::regex::icase };
			return std::regex_search(a_group, re);
		}

		bool ClothedVariant(const std::string& a_name)
		{
			static const std::regex re{ R"(\(outfit\)|clothing|clothed|for outfit|\boutfit\b)", std::regex::icase };
			return std::regex_search(a_name, re);
		}

		constexpr float kFullFit = 0.95F;
		constexpr float kPartialFit = 0.50F;
	}

	std::vector<SliderPreset> ParseXml(std::string_view a_text)
	{
		std::vector<SliderPreset>                    out;
		std::optional<SliderPreset>                  current;
		std::unordered_map<std::string, std::size_t> index;  // slider -> its place in current->big
		std::size_t                                  pos = 0;
		while ((pos = a_text.find('<', pos)) != std::string_view::npos) {
			if (a_text.substr(pos, 4) == "<!--") {
				const auto end = a_text.find("-->", pos + 4);
				pos = end == std::string_view::npos ? a_text.size() : end + 3;
				continue;
			}
			const auto end = a_text.find('>', pos + 1);
			if (end == std::string_view::npos) {
				break;
			}
			auto tag = a_text.substr(pos + 1, end - pos - 1);
			pos = end + 1;
			if (tag.empty() || tag[0] == '?' || tag[0] == '!') {
				continue;
			}
			const bool closing = tag[0] == '/';
			const bool selfClosing = tag.back() == '/';
			if (closing) {
				tag.remove_prefix(1);
			}
			const auto nameEnd = tag.find_first_of(" \t\r\n/");
			const auto name = tag.substr(0, nameEnd);
			if (name == "Preset") {
				if (closing) {
					if (current && !current->name.empty()) {
						out.push_back(std::move(*current));
					}
					current.reset();
					continue;
				}
				current = SliderPreset{ Attr(tag, "name").value_or(""), {}, {} };
				index.clear();
				if (selfClosing) {
					if (!current->name.empty()) {
						out.push_back(std::move(*current));
					}
					current.reset();
				}
			} else if (current && !closing && name == "Group") {
				if (const auto g = Attr(tag, "name"); g && !g->empty() && !NotAFamily(*g)) {
					current->families.push_back(*g);
				}
			} else if (current && !closing && name == "SetSlider") {
				// BodySlide's SliderPresets.cpp: size "big" or "both" gives the big value; "small" only the
				// small one, which a Fallout 4 body is not built at; no size, nothing.
				const auto slider = Attr(tag, "name");
				const auto size = Lower(Attr(tag, "size").value_or(""));
				const auto text = Attr(tag, "value");
				if (!slider || !text || (size != "big" && size != "both")) {
					continue;
				}
				float value = 0.0F;
				const auto* first = text->data();
				if (std::from_chars(first, first + text->size(), value).ec != std::errc{} || !std::isfinite(value)) {
					continue;
				}
				value /= 100.0F;
				if (const auto it = index.find(*slider); it != index.end()) {
					current->big[it->second].second = value;  // the later value, as the generator's dict keeps it
				} else {
					index.emplace(*slider, current->big.size());
					current->big.emplace_back(*slider, value);
				}
			}
		}
		return out;
	}

	std::unordered_set<std::string> TriMorphs(std::span<const std::byte> a_bytes)
	{
		std::unordered_set<std::string> out;
		std::size_t                     o = 0;
		const auto                      need = [&](std::size_t n) { return o + n <= a_bytes.size(); };
		const auto                      u8 = [&] { return static_cast<std::uint8_t>(a_bytes[o++]); };
		const auto                      u16 = [&] {
            std::uint16_t v = 0;
            std::memcpy(&v, a_bytes.data() + o, 2);
            o += 2;
            return v;
		};
		const auto text = [&](std::size_t n) {
			std::string s(reinterpret_cast<const char*>(a_bytes.data() + o), n);
			o += n;
			return s;
		};
		if (!need(6) || std::memcmp(a_bytes.data(), "PIRT", 4) != 0) {
			return {};
		}
		o = 4;
		const auto shapes = u16();
		for (std::uint16_t s = 0; s < shapes; ++s) {
			if (!need(1)) {
				return {};
			}
			const auto nameLength = u8();
			if (!need(nameLength + 2u)) {
				return {};
			}
			(void)text(nameLength);
			const auto morphs = u16();
			for (std::uint16_t m = 0; m < morphs; ++m) {
				if (!need(1)) {
					return {};
				}
				const auto morphLength = u8();
				if (!need(morphLength + 6u)) {
					return {};
				}
				auto name = text(morphLength);
				o += 4;  // the multiplier
				const auto verts = u16();
				if (!need(static_cast<std::size_t>(verts) * 8)) {
					return {};
				}
				o += static_cast<std::size_t>(verts) * 8;
				out.insert(std::move(name));
			}
		}
		return out;
	}

	std::string PlainMarker(std::string_view a_name)
	{
		std::string safe;
		for (const char c : a_name) {
			const auto u = static_cast<unsigned char>(c);
			if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) {
				safe += c;
			} else if (safe.empty() || safe.back() != '_') {
				safe += '_';
			}
		}
		const auto first = safe.find_first_not_of('_');
		if (first == std::string::npos) {
			return {};
		}
		safe = safe.substr(first, safe.find_last_not_of('_') - first + 1);
		return std::format("Silhouette_{}", safe);
	}

	Installed Resolve(const Catalog& a_catalog, const std::vector<SliderPreset>& a_files,
		const std::unordered_set<std::string> (&a_morphs)[2])
	{
		Installed out;
		// The body family each sex is: whichever the catalog's full fits most often declare
		// (tools/silhouette_gen.py installed_family).
		std::string family[2];
		for (const int s : { 0, 1 }) {
			std::unordered_map<std::string, int> counts;
			for (const auto& p : a_catalog.presets) {
				if (p.female == (s == 1) && p.fit == "full" && !p.family.empty()) {
					++counts[p.family];
				}
			}
			int best = 0;
			for (const auto& [f, n] : counts) {
				if (n > best || (n == best && f < family[s])) {
					best = n;
					family[s] = f;
				}
			}
		}
		std::unordered_set<std::string> markers;
		for (const auto& p : a_catalog.presets) {
			markers.insert(Lower(p.marker));
		}
		std::unordered_set<std::string> seen;
		for (const auto& sp : a_files) {
			if (sp.name.empty() || !seen.insert(Lower(sp.name)).second) {
				continue;  // BodySlide keeps the first preset of a name, in any case
			}
			if (sp.big.empty() || ClothedVariant(sp.name)) {
				continue;
			}
			if (const auto lower = Lower(sp.name); lower.ends_with("-refit")) {
				continue;  // a refit set, not a body (S-26)
			}
			std::size_t female = 0;
			std::size_t male = 0;
			for (const auto& [slider, value] : sp.big) {
				female += a_morphs[1].contains(slider) ? 1 : 0;
				male += a_morphs[0].contains(slider) ? 1 : 0;
			}
			const bool isFemale = female >= male;
			const int  s = isFemale ? 1 : 0;
			const auto fit = static_cast<float>(std::max(female, male)) / static_cast<float>(sp.big.size());
			if (a_morphs[s].empty()) {
				continue;  // that sex's body was not found: nothing to fit it to
			}
			if (a_catalog.Find(sp.name, isFemale)) {
				continue;  // the catalog's own -- Silhouette's or CBBE's and BodyTalk's stock -- stays
			}
			std::string band;
			if (fit >= kFullFit) {
				band = "full";
			} else if (fit < kPartialFit) {
				out.notes.push_back(std::format("\"{}\": fits your {} body at {:.0f} %", sp.name, isFemale ? "female" : "male", fit * 100.0F));
				continue;
			} else if (!sp.families.empty() && !family[s].empty() && std::ranges::find(sp.families, family[s]) == sp.families.end()) {
				out.notes.push_back(std::format("\"{}\": made for {}, your body is {}", sp.name, sp.families.front(), family[s]));
				continue;
			} else {
				band = "partial";
			}
			const auto& set = a_catalog.sliderSets[s];
			if (!set) {
				out.notes.push_back(std::format("\"{}\": this build does not say how your {} body's sliders read", sp.name, isFemale ? "female" : "male"));
				continue;
			}
			auto marker = PlainMarker(sp.name);
			if (marker.empty() || KindOf(marker) != MarkerKind::kBody || IEquals(marker, kBlacklistMarker) ||
				!markers.insert(Lower(marker)).second) {
				out.notes.push_back(std::format("\"{}\": its name makes the marker \"{}\", which another body uses", sp.name, marker));
				continue;
			}
			Preset p;
			p.name = sp.name;
			p.female = isFemale;
			p.marker = std::move(marker);
			for (const auto& slider : set->sliders) {
				const auto it = std::ranges::find(sp.big, slider.name, &std::pair<std::string, float>::first);
				auto       v = it != sp.big.end() ? it->second : slider.defaultValue;
				if (slider.invert) {
					v = 1.0F - v;
				}
				if (v != 0.0F && a_morphs[s].contains(slider.name) && !a_catalog.NeverInBody(isFemale, slider.name)) {
					p.values.emplace_back(slider.name, v);
				}
			}
			p.random = false;
			p.menu = true;
			p.zeroed = p.values.empty();
			p.fit = band;
			p.family = sp.families.empty() ? std::string{} : sp.families.front();
			p.installed = true;
			out.added.push_back(std::move(p));
		}
		return out;
	}
}
