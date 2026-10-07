#pragma once

#include "Director.h"

// Everything that touches the game: loading the catalog, reading actors into plain facts, and the
// pump that turns what the event sinks saw into Director calls. Reads happen on the main thread
// only -- in the pump, which the bridge calls as a native that is NOT callable from tasklets, and in
// the other natives marked the same way.

namespace SH::Game
{
	[[nodiscard]] Director& TheDirector();

	// kGameDataReady: the catalog, every manifest, and the forms the catalog names.
	void Load();

	// Which sex Load found no body for that Silhouette supports, as one line for the player's screen -- given
	// once a launch, "" after that and when both bodies are fine.
	[[nodiscard]] std::string TakeBodyWarning();

	// From the event sinks, on whatever thread the game sends them: queued, nothing read yet.
	void NoteLoaded(std::uint32_t a_ref);
	void NoteEquip(std::uint32_t a_ref, std::uint32_t a_item, bool a_equipped);
	// The view caster's picks, as reference HANDLES (0: none): what the player would activate, and the
	// dialogue pick (written to the log when a Pick finds nobody).
	void NoteCrosshair(std::uint32_t a_activate, std::uint32_t a_dialogue);

	// Main thread: everything queued since the last pump.
	void Pump();

	// Main thread. The form id of the NPC under the crosshair -- the one the player could talk to, so
	// within activation reach -- or 0 for none, the player, or anyone Silhouette leaves alone.
	// a_recentSeconds > 0 also accepts the last NPC aimed at within that many seconds of the crosshair
	// leaving them: a menu opening takes the crosshair off them. A 0 says why in the log.
	[[nodiscard]] std::uint32_t CrosshairActor(float a_recentSeconds);

	// Main thread: an actor by form id, or null.
	[[nodiscard]] RE::Actor*    ActorFor(std::uint32_t a_ref);
	[[nodiscard]] bool          IsFemale(RE::Actor* a_actor);  // the BODY's sex (S-86); LooksMenu keeps morphs under the flag's
	[[nodiscard]] std::uint32_t BaseOf(RE::Actor* a_actor);
	[[nodiscard]] std::string   NameOf(RE::Actor* a_actor);  // the name the player sees
	// The player, or a character-creation dummy LooksMenu clones onto the player (S-13).
	[[nodiscard]] bool NeverShaped(RE::Actor* a_actor);
	// The editor id of the NPC record's race, as BodyGen matches a line ("" when it has none).
	[[nodiscard]] std::string RaceOf(RE::Actor* a_actor);

	// The director's log lines, into ours.
	void FlushLog();

	// A load or a new game is starting, on the main thread: what was queued, and what was decided about
	// items, belongs to the save being left.
	void ForgetInbox();

	// Silhouette's scripts asked for the protocol (the bridge's Connect, or API.Loaded): they are there,
	// whether or not the bridge then polls.
	void NoteAsked();

	// Main thread: what the game shows of them now, told to the director (as their 3D loading would). A
	// request about someone not seen yet this session is then decided knowing who they are.
	void See(RE::Actor* a_actor);

	// A save finished loading: the bridge should poll within a minute. A thread of ours says so in the
	// log once when it does not -- the one symptom of a missing or disabled Silhouette.esp that nothing
	// else would report.
	void NoteGameLoaded();

	// Main thread, after every load, a failed one included (the load that started already forgot
	// everyone): the bridge's first poll reads every actor the game is simulating, and so do its polls
	// for 30 seconds after it -- after a load in a running game the game reports none of them (S-43).
	void ArmSweep();
}
