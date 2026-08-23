/*
 * hitstunmelee.c — Melee-style hitstun toggle.
 *
 * First gameplay mod of the Smash Remix -> BattleShip port (Fase 1).
 * Remix source: src/Hitstun.asm, which patches ftParamGetHitStun
 * (RAM 0x800EA1B0) to divide knockback by 2.5 instead of 1.875 while the
 * toggle is on — same knockback, fewer hitstun frames, as in Melee.
 *
 * Port approach: detour ftParamGetHitStun with mod_install_hook. On every
 * call, read the CVar "mods.hitstunmelee.enabled"; when set, return
 * knockback / 2.5, otherwise delegate to the original trampoline.
 *
 * The toggle lives in the engine menu (Settings > Gameplay > Smash Remix
 * Mods) as a plain CVar checkbox; this mod only consumes it, so it stays
 * a self-contained unit that degrades to vanilla behavior when absent or
 * disabled.
 *
 * CVarGetInteger is extern "C" in libultraship's bridge but is not part
 * of BattleShip.def (data/C++ symbols aren't exported there), so it is
 * resolved through the PDB once at init via mod_resolve_symbol.
 */
#include "mod.h"

#include <stddef.h> /* NULL — mods that pull no other engine header need this */

typedef int (*CVarGetIntegerFn)(const char* name, int defaultValue);
typedef float (*GetHitStunFn)(float knockback);

#define HITSTUN_CVAR   "mods.hitstunmelee.enabled"
#define MELEE_DIVISOR  2.5f

static GetHitStunFn     gOrigGetHitStun   = NULL;
static CVarGetIntegerFn gCVarGetInteger   = NULL;
static int              sLoggedVanilla    = 0;
static int              sLoggedMelee      = 0;

static float HookedGetHitStun(float knockback) {
    if (gCVarGetInteger != NULL && gCVarGetInteger(HITSTUN_CVAR, 0) != 0) {
        /* Melee style: higher divisor, so less hitstun (Remix Hitstun.asm). */
        if (!sLoggedMelee) {
            mod_log("[HitstunMelee] active: knockback %.2f -> hitstun %.2f frames\n",
                    knockback, knockback / MELEE_DIVISOR);
            sLoggedMelee = 1;
        }
        return knockback / MELEE_DIVISOR;
    }
    if (!sLoggedVanilla) {
        mod_log("[HitstunMelee] toggle off: passing through (%.2f -> %.2f frames)\n",
                knockback, knockback / 1.875f);
        sLoggedVanilla = 1;
    }
    return gOrigGetHitStun(knockback);
}

MOD_INIT() {
    gCVarGetInteger = (CVarGetIntegerFn)mod_resolve_symbol("CVarGetInteger");
    if (gCVarGetInteger == NULL) {
        mod_log("[HitstunMelee] FATAL: CVarGetInteger not resolvable via PDB\n");
        return;
    }

    int rc = mod_install_hook("ftParamGetHitStun",
                              (void*)HookedGetHitStun,
                              (void**)&gOrigGetHitStun);
    if (rc == 0) {
        mod_log("[HitstunMelee] hook installed OK (trampoline=%p, toggle '%s' = %d)\n",
                (void*)gOrigGetHitStun, HITSTUN_CVAR,
                gCVarGetInteger != NULL ? gCVarGetInteger(HITSTUN_CVAR, 0) : -1);
    } else {
        mod_log("[HitstunMelee] hook install FAILED rc=%d\n", rc);
    }
}

MOD_EXIT() {
    /* HookManager::UninstallHooksForOwner tears the detour down by owner
     * when the mod unloads; nothing else to clean up. */
    mod_log("[HitstunMelee] exit OK\n");
}
