/*
 * dkult.c — Fase 2a: registry bootstrap of DK Ultimate (Smash Remix +EXTRA).
 *
 * DKUlt is a Donkey Kong clone that inherits DK's data/models/specials and
 * only overrides action parameters (animations + moveset scripts) and a
 * couple of specials (see extra_characters/DKUlt/main.asm + DKUltSpecial.asm).
 * Reusing DK's FTData is therefore valid and keeps the package minimal.
 *
 * This mod only registers the FighterDescriptor "data layer" into the
 * character registry — the CSS/spawn wiring (making the fighter selectable
 * and instantiable in battle) is a later Fase 2 step. Registering fkind
 * >= nFTKindEnumCount (27) is the hook points (ftmanager.c / ftmain.c) use
 * port_fighter_* accessors with safe NULL fallbacks.
 *
 * Values sourced from extra_characters/DKUlt/config.yaml + main.asm:
 *   base_character: DONKEY   -> ft_data from DK
 *   num_costumes: 5
 *   default costumes (main.asm set_default_costumes): 0,1,2,3,2,3,4 (port 0..6)
 *   results.name "DK ULT", name_x 30, name_scale 1, wins_x 180
 *   announcer_fgm "FFF0" (UltDKAnnouncer) -> left 0 here (real FGM id TBD);
 *     results_name == NULL would disable results drawing, so set the name.
 */
#include "mod.h"

#include <stddef.h> /* NULL */

#include <ft/fighter.h>  /* FTData etc. (already in merge include set) */

/* port/fighter_registry.h is on the mod include path (${PARENT_DIR}/port). */
#include "fighter_registry.h"

#define DKULT_FKIND 27 /* == nFTKindEnumCount (first free synth slot) */

/* Main.asm: Character.set_default_costumes(DKULT, 0,1,2,3,2,3,4) — indexed by
 * player port (0..6, 7 slots; last entries repeat for ports >4). */
static const unsigned char sDKUltDefaultCostumes[7] = { 0, 1, 2, 3, 2, 3, 4 };

static void DKUltRegister(void)
{
    FighterDescriptor desc;

    /* CharacterEngine: start from a full clone of DK's registry row so the
     * clone inherits entry statuses, specials, scale, costumes, AI, results
     * geometry, etc. Any un-overridden field stays identical to DK. */
    if (port_fighter_clone_from(nFTKindDonkey, &desc) != 0) {
        mod_log("[DKUlt] FATAL: port_fighter_clone_from(DONKEY) failed\n");
        return;
    }

    /* Override only what DKUlt actually changes vs. its DK base. */
    desc.scale               = 1.0F;      /* keep DK's 1.0 scale from clone */

    /* CSS presentation: DK is the odd vanilla fighter at 1.5 spotlight; give
     * the clone the "normal" scale the engine uses for everyone else. */
    desc.css_spotlight_scale = 1.0F;

    /* Costumes (num_costumes = 5, from config.yaml). */
    desc.costume_count           = 5;
    desc.default_costumes        = sDKUltDefaultCostumes;
    desc.default_costumes_count  = 7;

    /* Results screen ("DK ULT", from config.yaml). announce FGM left 0 ->
     * accessor reports 0 (no overridden announce voice for now). */
    desc.results_name       = "DK ULT";
    desc.results_name_lx    = 30.0F;
    desc.results_name_scale = 1.0F;
    desc.results_wins_lx    = 180.0F;
    desc.results_announce_fgm = 0;
    desc.results_emblem_valid = 0;

    port_fighter_register(DKULT_FKIND, &desc);

    /* Verify the write landed. */
    if (port_fighter_descriptor(DKULT_FKIND) != NULL &&
        port_fighter_data(DKULT_FKIND) == port_fighter_data(nFTKindDonkey)) {
        mod_log("[DKUlt] registered fkind=%d (clone of DK) costumes=%d "
                "name=\"%s\"\n",
                DKULT_FKIND, desc.costume_count, desc.results_name);
    } else {
        mod_log("[DKUlt] ERROR: registration verify failed\n");
    }
}

MOD_INIT() {
    DKUltRegister();
}

MOD_EXIT() {
    /* No engine resources held; the registry row is managed by the registry,
     * not the mod's lifetime. */
    mod_log("[DKUlt] exit OK\n");
}
