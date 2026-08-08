/*
 * mod_overlay.h — on-screen text overlay bridge for TCC mods.
 *
 * The bridge gives mods persistent, keyed, positioned text entries that
 * the engine paints every frame on top of the game (and menu) — the
 * building block for overlay-style mods (Input Display, Combo Meter,
 * VsStats, Player Tags...).
 *
 * Mod-facing C API (implemented in mod_overlay.cpp, exported from the
 * engine binary; mods resolve them through the TCC link against
 * BattleShip.def, no dllexport needed on the mod side):
 *
 *   // Creates or updates the entry `key`. rgba is 0xRRGGBBAA.
 *   // Coordinates are pixels relative to the game window's top-left
 *   // (the work area under the title bar), so layouts hold at any
 *   // window size/resolution.
 *   void mod_overlay_text(const char* key, float x, float y,
 *                         unsigned int rgba, int shadow, const char* text);
 *   void mod_overlay_remove(const char* key);
 *   void mod_overlay_clear(void);   // removes every entry (call in MOD_EXIT)
 *
 * Entries persist until removed: a mod typically updates them from a
 * GamePostUpdateEvent listener and clears them in MOD_EXIT.
 */
#pragma once

namespace ssb64::mods {

/* Installs the overlay render hook (detour on Ship::GameOverlay::Draw,
 * which libultraship calls once per presented frame from Gui::DrawGame).
 * Must run after HookManager::Init and before mods load. Returns false
 * (and logs) if the hook could not be installed; mods can still call the
 * API safely in that case, entries just never paint. */
bool ModOverlayInit();

} // namespace ssb64::mods
