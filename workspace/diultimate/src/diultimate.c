/*
 * diultimate.c — DI styles (Normal / Japanese / Ultimate) + per-port
 * DI multiplier.
 *
 * Second gameplay mod of the Smash Remix -> BattleShip port (Fase 1).
 * Remix source: src/DI.asm, which patches ftCommonDamageCommonProcLagUpdate
 * (patch lands on the load of the vanilla DI range multiplier,
 * 0x801408DC inside 0x80140878):
 *
 *   - Normal (US vanilla): stick * 2.1   (FTCOMMON_DAMAGE_SMASH_DI_RANGE_MUL)
 *   - Japanese           : stick * 1.5
 *   - Ultimate           : stick * ~0.5986 (0x3F190000 — per Remix, "not
 *     accurate, just weaker SDI")
 * plus a per-port multiplier table (Remix stores it as the top half of an
 * IEEE-754 float; negative/0xFFFF = disabled). We read plain floats from
 * CVars instead; values <= 0 disable the extra multiplier for that port.
 *
 * Port approach: full-function detour of ftCommonDamageCommonProcLagUpdate.
 * Unlike HitstunMelee (which detoured a one-liner), the DI math is inline
 * code inside a larger function, so there is no smaller seam to hook — we
 * reimplement its body verbatim with the style/multiplier applied to the
 * multiplier constant. With default CVars (style=Normal, multipliers=1)
 * behavior is identical to vanilla.
 *
 * This mod includes real decomp headers (<ft/fighter.h> et al); TCC handles
 * them fine given PORT=1/_LANGUAGE_C (both are engine-provided runtime
 * defines) and merge_mod.py inlines them into dist/ at packaging time.
 */
#include "mod.h"

#include <stddef.h> /* NULL */

#include <ft/fighter.h>         /* FTStruct, ftGetStruct */
#include <it/item.h>            /* same include set as ftcommondamage.c */
#include <if/ifscreenflash.h>

/* CVar bridge (libultraship, extern "C", not in BattleShip.def -> PDB). */
typedef int (*CVarGetIntegerFn)(const char* name, int defaultValue);
typedef float (*CVarGetFloatFn)(const char* name, float defaultValue);

#define DI_STYLE_CVAR     "mods.di.style"
#define DI_MULT_P_CVAR(n) ("mods.di.mult.p" #n)

static const char* const kPortMultCvars[4] = {
    "mods.di.mult.p1",
    "mods.di.mult.p2",
    "mods.di.mult.p3",
    "mods.di.mult.p4",
};

static CVarGetIntegerFn gCVarGetInteger = NULL;
static CVarGetFloatFn   gCVarGetFloat   = NULL;
static int              sLoggedFirst    = 0;

typedef void (*ProcLagUpdateFn)(GObj *fighter_gobj);
static ProcLagUpdateFn gOrigProcLagUpdate = NULL;

/* Exact IEEE-754 bits of Remix's Ultimate constant (lui at, 0x3F19). */
static float DIUltimateMul(void) {
    union { unsigned u; float f; } c;
    c.u = 0x3F190000u;
    return c.f;
}

static float DIGetStyleMul(void) {
    int style = gCVarGetInteger(DI_STYLE_CVAR, 0);
    switch (style) {
        case 1:  return 1.5f;                            /* Japanese */
        case 2:  return DIUltimateMul();                 /* Ultimate */
        default: return FTCOMMON_DAMAGE_SMASH_DI_RANGE_MUL; /* Normal (2.1 US / 1.5 JP builds) */
    }
}

static void HookedProcLagUpdate(GObj *fighter_gobj) {
    FTStruct *fp = ftGetStruct(fighter_gobj);

    if (fp->hitlag_tics != 0)
    {
        if ((SQUARE(fp->input.pl.stick_range.x) + SQUARE(fp->input.pl.stick_range.y)) >= SQUARE(FTCOMMON_DAMAGE_SMASH_DI_RANGE_MIN))
        {
            if ((fp->tap_stick_x < FTCOMMON_DAMAGE_SMASH_DI_BUFFER_TICS_MAX) || (fp->tap_stick_y < FTCOMMON_DAMAGE_SMASH_DI_BUFFER_TICS_MAX))
            {
                Vec3f *translate = &DObjGetStruct(fighter_gobj)->translate.vec.f;
                float mul = DIGetStyleMul();

                /* Per-port multiplier; <= 0 keeps vanilla for that port. */
                if (gCVarGetFloat != NULL && fp->player < 4)
                {
                    float m = gCVarGetFloat(kPortMultCvars[fp->player], 1.0f);
                    if (m > 0.0f)
                    {
                        mul *= m;
                    }
                }

                translate->x += fp->input.pl.stick_range.x * mul;
                translate->y += fp->input.pl.stick_range.y * mul;

                if (!sLoggedFirst) {
                    mod_log("[DIUltimate] active: port %u stick (%d,%d) x %.3f\n",
                            fp->player, fp->input.pl.stick_range.x,
                            fp->input.pl.stick_range.y, mul);
                    sLoggedFirst = 1;
                }

                fp->tap_stick_x = fp->tap_stick_y = FTINPUT_STICKBUFFER_TICS_MAX;
            }
        }
    }
}

MOD_INIT() {
    gCVarGetInteger = (CVarGetIntegerFn)mod_resolve_symbol("CVarGetInteger");
    gCVarGetFloat = (CVarGetFloatFn)mod_resolve_symbol("CVarGetFloat");
    if (gCVarGetInteger == NULL || gCVarGetFloat == NULL) {
        mod_log("[DIUltimate] FATAL: CVar bridges not resolvable via PDB\n");
        return;
    }

    int rc = mod_install_hook("ftCommonDamageCommonProcLagUpdate",
                              (void*)HookedProcLagUpdate,
                              (void**)&gOrigProcLagUpdate);
    if (rc == 0) {
        mod_log("[DIUltimate] hook installed OK (style=%d p1=%.2f)\n",
                gCVarGetInteger(DI_STYLE_CVAR, 0),
                gCVarGetFloat(kPortMultCvars[0], 1.0f));
    } else {
        mod_log("[DIUltimate] hook install FAILED rc=%d\n", rc);
    }
}

MOD_EXIT() {
    mod_log("[DIUltimate] exit OK\n");
}
