/*
 * perfectshield.c — Melee-style Perfect Shield.
 *
 * Fifth gameplay mod of the Smash Remix -> BattleShip port (Fase 1).
 * Remix source: src/PerfectShield.asm by halofactory. Three behaviors:
 *
 *   1. No shield stun: getting hit while the shield is still coming up
 *      (action == GuardOn / "ShieldOn") skips the GuardSetOff pushback
 *      entirely and keeps shielding seamlessly.
 *   2. Projectiles are reflected if the shield went up within the last
 *      ~2 frames (status_total_tics < 2).
 *   3. Thrown items likewise.
 *
 * Port approach — instead of Remix's raw struct pokes:
 *
 *   - Detour ftCommonGuardSetOffSetStatus (the exact function Remix's first
 *     patch calls away, RAM 0x80149108). During the perfect window we zero
 *     hitlag/shield_damage/damage_queue, play the expanding-circle flash and
 *     return WITHOUT changing status, so the fighter keeps shielding.
 *   - Detour ftMainUpdateShieldStatWeapon / ftMainUpdateShieldStatItem.
 *     During the reflect window we do the reflection OURSELVES (Option B) and
 *     skip the vanilla shield bookkeeping: record the victim, transfer
 *     ownership to the shielder, flip the projectile velocity geometrically
 *     (away from the fighter, independent of facing), scale damage by the
 *     vanilla WEAPON/ITEM_REFLECT_* constants, and rotate the model.
 *
 * Why not reuse ftMainUpdateReflectorStat{Weapon,Item}? (v1 did.)
 *   - It dereferences fp->special_coll, which is NULL outside special
 *     statuses (ftmanager.c:557) — guarded upstream only by fp->is_reflect.
 *   - Worse, it leaves fp->reflect_lr set for ftMainProcParams to follow up
 *     via `switch (fp->special_coll->kind)` (ftmain.c:4077-4079) => NULL
 *     deref on a normal shielder.
 *   - The eventual velocity flip happens in wpMainReflectorSetLR, which only
 *     inverts when `vel_air.x * fp->lr < 0` — i.e. it assumes the reflector
 *     faces the incoming projectile. Shielding with your back to it leaves
 *     the projectile unflipped but already recorded as interacted => it flies
 *     straight through the fighter (the "ghost projectile" bug).
 * Option B fixes all three by doing the bookkeeping inline with a
 * geometric, facing-independent flip.
 */
#include "mod.h"

#include <stddef.h> /* NULL */

#include <ft/fighter.h>
#include <wp/weapon.h>
#include <it/item.h>

typedef int (*CVarGetIntegerFn)(const char* name, int defaultValue);

#define PS_CVAR            "mods.perfectshield.enabled"
#define PS_REFLECT_TICS    2 /* Remix: slti t1, t1, 2 */

typedef void (*GuardSetOffFn)(GObj *fighter_gobj);
typedef void (*ShieldStatWeaponFn)(WPStruct *wp, WPAttackColl *wp_attack_coll, s32 attack_id, FTStruct *fp, GObj *weapon_gobj, GObj *fighter_gobj, f32 angle, Vec3f *dir);
typedef void (*ShieldStatItemFn)(ITStruct *ip, ITAttackColl *it_attack_coll, s32 attack_id, FTStruct *fp, GObj *item_gobj, GObj *fighter_gobj, f32 angle, Vec3f *vec);

static CVarGetIntegerFn   gCVarGetInteger       = NULL;
static GuardSetOffFn      gOrigGuardSetOff      = NULL;
static ShieldStatWeaponFn gOrigShieldStatWeapon = NULL;
static ShieldStatItemFn   gOrigShieldStatItem   = NULL;
static int                sLoggedFirst          = 0;

static sb32 PSIsEnabled(void) {
    return (gCVarGetInteger != NULL) && (gCVarGetInteger(PS_CVAR, 0) != 0);
}

/* Perfect window for reflection: shield came up this frame or last. */
static sb32 PSIsReflectWindow(FTStruct *fp) {
    return (fp->status_id == nFTCommonStatusGuardOn) && (fp->status_total_tics < PS_REFLECT_TICS);
}

/* Horizontal flip that sends the projectile away from the shielder,
 * independent of which way the fighter faces (fixes "ghost projectile"):
 * side = -1 if the projectile is left of the fighter, +1 if right. */
static void PSFlipWeaponVel(WPStruct *wp, GObj *weapon_gobj, GObj *fighter_gobj) {
    f32 rel = DObjGetStruct(weapon_gobj)->translate.vec.f.x -
              DObjGetStruct(fighter_gobj)->translate.vec.f.x;
    f32 side = (rel >= 0.0F) ? +1.0F : -1.0F;
    wp->physics.vel_air.x = (wp->physics.vel_air.x < 0.0F) ? -wp->physics.vel_air.x : wp->physics.vel_air.x;
    wp->physics.vel_air.x *= side;
}

static void PSFlipItemVel(ITStruct *ip, GObj *item_gobj, GObj *fighter_gobj) {
    f32 rel = DObjGetStruct(item_gobj)->translate.vec.f.x -
              DObjGetStruct(fighter_gobj)->translate.vec.f.x;
    f32 side = (rel >= 0.0F) ? +1.0F : -1.0F;
    ip->physics.vel_air.x = (ip->physics.vel_air.x < 0.0F) ? -ip->physics.vel_air.x : ip->physics.vel_air.x;
    ip->physics.vel_air.x *= side;
}

static void PSPlayFlash(GObj *fighter_gobj, FTStruct *fp) {
    /* Expanding circle (same family as the ledge-grab swirl Remix plays),
     * anchored at the shield joint (nFTPartsJointYRotN, same joint the shield
     * bubble attaches to in efManagerShieldMakeEffect) so it appears on the
     * bubble instead of at the fighter origin. */
    ftParamMakeEffect(fighter_gobj, nEFKindFlashMiddle, nFTPartsJointYRotN, NULL, NULL, fp->lr, 0, 0);
}

/* --- 1. Skip shield stun during GuardOn --------------------------------- */

static void HookedGuardSetOff(GObj *fighter_gobj) {
    FTStruct *fp = ftGetStruct(fighter_gobj);

    if (PSIsEnabled() && (fp->status_id == nFTCommonStatusGuardOn))
    {
        /* Perfect shield: cancel the pushback, keep shielding (Remix zeroes
         * hitstun / shield damage / incoming damage and stays in ShieldOn). */
        fp->hitlag_tics = 0;
        fp->shield_damage = 0;
        fp->damage_queue = 0;

        PSPlayFlash(fighter_gobj, fp);

        if (!sLoggedFirst) {
            mod_log("[PerfectShield] stun skipped on port %u\n", fp->player);
            sLoggedFirst = 1;
        }
        return;
    }

    gOrigGuardSetOff(fighter_gobj);
}

/* --- 2. Reflect projectiles --------------------------------------------- */

static void HookedShieldStatWeapon(WPStruct *wp, WPAttackColl *wp_attack_coll, s32 attack_id, FTStruct *fp, GObj *weapon_gobj, GObj *fighter_gobj, f32 angle, Vec3f *dir)
{
    if (PSIsEnabled() && PSIsReflectWindow(fp))
    {
        /* Option B: manual reflection (see header). Record victim, transfer
         * ownership to the shielder, flip velocity geometrically, scale
         * damage and rotate the model. */
        f32 vel_before = wp->physics.vel_air.x;
        wpProcessUpdateHitInteractStats(wp, wp_attack_coll, fighter_gobj, nGMHitTypeReflect, 0);

        wp->owner_gobj = fighter_gobj;
        wp->team = fp->team;
        wp->player = fp->player;
        wp->player_num = fp->player_num;
        wp->handicap = fp->handicap;
        wp->display_mode = fp->display_mode;
#if defined(REGION_US)
        wp->attack_coll.stat_flags = fp->stat_flags;
        wp->attack_coll.stat_count = fp->stat_count;
#endif

        PSFlipWeaponVel(wp, weapon_gobj, fighter_gobj);
        wpMainReflectorRotateWeaponModel(weapon_gobj);

        if (!(wp->is_static_damage))
        {
            wp->attack_coll.damage = (wp->attack_coll.damage * WEAPON_REFLECT_MUL_DEFAULT) + WEAPON_REFLECT_ADD_DEFAULT;
            if (wp->attack_coll.damage > WEAPON_REFLECT_TIME_DEFAULT)
            {
                wp->attack_coll.damage = WEAPON_REFLECT_TIME_DEFAULT;
            }
        }

        PSPlayFlash(fighter_gobj, fp);

        if (!sLoggedFirst) {
            mod_log("[PerfectShield] reflected projectile on port %u (vel %.1f -> %.1f)\n",
                    fp->player, vel_before, wp->physics.vel_air.x);
            sLoggedFirst = 1;
        }
        return;
    }

    gOrigShieldStatWeapon(wp, wp_attack_coll, attack_id, fp, weapon_gobj, fighter_gobj, angle, dir);
}

/* --- 3. Reflect items ---------------------------------------------------- */

static void HookedShieldStatItem(ITStruct *ip, ITAttackColl *it_attack_coll, s32 attack_id, FTStruct *fp, GObj *item_gobj, GObj *fighter_gobj, f32 angle, Vec3f *vec)
{
    if (PSIsEnabled() && PSIsReflectWindow(fp))
    {
        itProcessSetHitInteractStats(it_attack_coll, fighter_gobj, nGMHitTypeReflect, 0);

        ip->owner_gobj = fighter_gobj;
        ip->team = fp->team;
        ip->player = fp->player;
        ip->player_num = fp->player_num;
        ip->handicap = fp->handicap;
#if defined(REGION_US)
        ip->attack_coll.stat_flags = fp->stat_flags;
        ip->attack_coll.stat_count = fp->stat_count;
#endif

        PSFlipItemVel(ip, item_gobj, fighter_gobj);

        if (!(ip->is_static_damage))
        {
            ip->attack_coll.damage = (ip->attack_coll.damage * ITEM_REFLECT_MUL_DEFAULT) + ITEM_REFLECT_ADD_DEFAULT;
            if (ip->attack_coll.damage > ITEM_REFLECT_MAX_DEFAULT)
            {
                ip->attack_coll.damage = ITEM_REFLECT_MAX_DEFAULT;
            }
        }

        PSPlayFlash(fighter_gobj, fp);

        if (!sLoggedFirst) {
            mod_log("[PerfectShield] reflected item on port %u (vel %.1f)\n",
                    fp->player, ip->physics.vel_air.x);
            sLoggedFirst = 1;
        }
        return;
    }

    gOrigShieldStatItem(ip, it_attack_coll, attack_id, fp, item_gobj, fighter_gobj, angle, vec);
}

MOD_INIT() {
    gCVarGetInteger = (CVarGetIntegerFn)mod_resolve_symbol("CVarGetInteger");
    if (gCVarGetInteger == NULL) {
        mod_log("[PerfectShield] FATAL: CVarGetInteger not resolvable via PDB\n");
        return;
    }

    int rc1 = mod_install_hook("ftCommonGuardSetOffSetStatus",
                               (void*)HookedGuardSetOff,
                               (void**)&gOrigGuardSetOff);
    int rc2 = mod_install_hook("ftMainUpdateShieldStatWeapon",
                               (void*)HookedShieldStatWeapon,
                               (void**)&gOrigShieldStatWeapon);
    int rc3 = mod_install_hook("ftMainUpdateShieldStatItem",
                               (void*)HookedShieldStatItem,
                               (void**)&gOrigShieldStatItem);

    if ((rc1 == 0) && (rc2 == 0) && (rc3 == 0)) {
        mod_log("[PerfectShield] hooks installed OK (toggle '%s' = %d)\n",
                PS_CVAR, gCVarGetInteger(PS_CVAR, 0));
    } else {
        mod_log("[PerfectShield] hook install FAILED rc=%d,%d,%d\n", rc1, rc2, rc3);
    }
}

MOD_EXIT() {
    mod_log("[PerfectShield] exit OK\n");
}
