/*
 * DKUltSpawn.cpp - Fase 2a SPAWN bridge for the DKUlt CharacterEngine.
 *
 * scvsbattle.c asks "should THIS player slot load DK Ultimate instead of
 * the character they picked?" by calling port_dkult_spawn(player). The
 * answer is driven by the CVar mods.dkult.spawnplayer (-1 = disabled, the
 * player index 0..3 = that slot uses DKUlt). This lets us prove the whole
 * synthetic-fighter pipeline (registry -> battle spawn) without yet
 * touching the fixed-12 CSS grid.
 */
#include <libultraship/bridge/consolevariablebridge.h>
#include "character_engine.h"

extern "C" int port_dkult_spawn(int player)
{
    int target = CVarGetInteger("mods.dkult.spawnplayer", -1);
    if (target < 0) return 0;
    if (player == target && CVarGetInteger("mods.dkult.validateassets", 0))
        port_ce_probe_dkult_assets();
    return (player == target) ? 1 : 0;
}

/* nFTKindEnumCount == 27 (decomp/src/ft/ftdef.h): the first free synthetic
 * fkind slot, same value the dkult mod registers its FighterDescriptor into.
 * Kept in sync by hand; the mod and this bridge must agree. */
extern "C" int port_dkult_fkind(void)
{
    return 27;
}
