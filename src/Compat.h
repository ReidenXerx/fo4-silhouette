#pragma once

// S-75: the few calls that differ between the classic OG library (alandtse's CommonLibF4) and
// CommonLibF4RD, the one that runs on OG, NG and AE. Everything else is the same API.

namespace SH::Compat
{
	// The NPC record's sex. Fallout 4's own SEX enum: 0 male, 1 female.
	inline bool Female(RE::TESNPC* a_npc)
	{
#ifdef SH_RUNTIME_DATABASE
		return a_npc && a_npc->GetSex() == 1;
#else
		return a_npc && a_npc->GetSex() == RE::SEX::kFemale;
#endif
	}

	// A name for the log and the notifications. CommonLibF4RD has no GetDisplayFullName, so the RD
	// build names a reference by its base record, as OBody names NPCs for its rules: a reference renamed
	// at run time reads under its record's name there.
	inline std::string DisplayName(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return {};
		}
#ifdef SH_RUNTIME_DATABASE
		auto* base = a_ref->GetObjectReference();
		return base ? std::string{ RE::TESFullName::GetFullName(*base) } : std::string{};
#else
		const char* name = a_ref->GetDisplayFullName();
		return name ? std::string{ name } : std::string{};
#endif
	}

	// The biped slots an item fills, as bits (bit i = slot 30 + i).
	inline std::uint32_t FilledSlots(const RE::BGSBipedObjectForm* a_form)
	{
#ifdef SH_RUNTIME_DATABASE
		return static_cast<std::uint32_t>(a_form->GetFilledSlots().underlying());
#else
		return a_form->GetFilledSlots();
#endif
	}

	// The next co-save record. CommonLibF4RD's wrapper logs a warning every time this returns false -- which is
	// how F4SE says the list has ended, on every load (measured 2026-09-26: "failed to get next record info"
	// after all 39 records read). Its wrapper is a cast onto F4SE's own interface, so the call goes there.
	inline bool NextRecordInfo(const F4SE::SerializationInterface* a_intfc, std::uint32_t& a_type, std::uint32_t& a_version, std::uint32_t& a_length)
	{
#ifdef SH_RUNTIME_DATABASE
		const auto& raw = reinterpret_cast<const F4SE::detail::F4SESerializationInterface&>(*a_intfc);
		return raw.GetNextRecordInfo(&a_type, &a_version, &a_length);
#else
		return a_intfc->GetNextRecordInfo(a_type, a_version, a_length);
#endif
	}

	inline void SetUniqueID(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_id)
	{
		// CommonLibF4RD hands the interface out const and keeps SetUniqueID non-const.
		const_cast<F4SE::SerializationInterface*>(a_intfc)->SetUniqueID(a_id);
	}
}
