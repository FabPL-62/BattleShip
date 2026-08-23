/*
 * ledgetrump.c — Ledge Trump (Smash 4 / Ultimate style ledge mechanics).
 *
 * Fourth gameplay mod of the Smash Remix -> BattleShip port (Fase 1).
 * Remix source: src/LedgeTrump.asm by halofactory, which hooks the
 * "ledge occupied" rejection inside the fighter map-collision routine
 * (patch at RAM 0x800DE66C, inside mpCommonRunFighterSpecialCollisions,
 * 0x800DE45C): when another fighter holds the same cliff, vanilla refuses
 * the grab (ledge hogging). With the toggle on, the hanging player is
 * instead ejected — tumble + pushed away from the ledge + lag frames —
 * and the incoming grab proceeds.
 *
 * Port approach: full-function detour of mpCommonRunFighterSpecialCollisions
 * (exported in BattleShip.def). The body is reimplemented verbatim except
 * for the occupied-ledge branch: if the holder is in CliffCatch/CliffWait,
 * trump them and let the grab continue.
 *
 * Trump implementation uses engine primitives instead of raw struct pokes:
 *   - cliffcatch_wait = FTCOMMON_CLIFF_CATCH_WAIT (same as the canonical
 *     "hit while ledge-hanging" path, ftCommonDamageGotoDamageStatus)
 *   - ftMainSetStatus(DamageAir3) — clears is_cliff_hold and runs the full
 *     status-change machinery (ftmain.c clears is_cliff_hold on every change)
 *   - hitstun 30 frames + push velocity in vel_damage_air (the knockback
 *     carrier; decays 1.7/frame, so a 50-unit push lasts ~29 frames —
 *     matching Remix's LAG_FRAMES(30) nicely)
 */
#include "mod.h"

#include <stddef.h> /* NULL */

#include <ft/fighter.h>                     /* FTStruct, ftGetStruct */
#include <wp/weapon.h>
#include <it/item.h>
#include <ft/ftcommon/ftcommonfunctions.h>  /* same include set as mpcommon.c */

typedef int (*CVarGetIntegerFn)(const char* name, int defaultValue);
typedef sb32 (*RunSpecialCollisionsFn)(MPCollData *coll_data, GObj *fighter_gobj, u32 flags);
typedef sb32 (*ProcMapFn)(GObj*);

#define TRUMP_CVAR        "mods.ledgetrump.enabled"
#define TRUMP_PUSH_VEL    50.0f /* Remix PUSH_X/Y_VELOCITY (0x42480000 = 50.0f) */
#define TRUMP_LAG_FRAMES  30    /* Remix LAG_FRAMES */

static CVarGetIntegerFn   gCVarGetInteger   = NULL;
static RunSpecialCollisionsFn gOrigRunSpecialCollisions = NULL;
static int                sLoggedFirst      = 0;

/* Data symbols not exported in BattleShip.def -> PDB-resolved pointers.
 * - gGCCommonLinks: fighter GObj link lists (see port.cpp accessor comment).
 * - sMPCommonProcPass: file-static pass-floor filter of mpcommon.c; the
 *   engine assigns it right before each Pass/PassCliff collision run, so we
 *   dereference it at call time to stay faithful to vanilla. */
static GObj ***sGCCommonLinks = NULL;
static ProcMapFn *sMPCommonProcPassRef = NULL;

/* Eject the ledge holder, Smash 4 style: tumble away from the wall. */
static void TrumpLedgeHolder(GObj *holder_gobj) {
    FTStruct *fp = ftGetStruct(holder_gobj);

    fp->cliffcatch_wait = FTCOMMON_CLIFF_CATCH_WAIT;

    /* Full status-change machinery; also clears is_cliff_hold (ftmain.c). */
    ftMainSetStatus(holder_gobj, nFTCommonStatusDamageAir3, 0.0F, 1.0F, FTSTATUS_PRESERVE_DAMAGEPLAYER);

    fp->status_vars.common.damage.hitstun_tics = TRUMP_LAG_FRAMES;
    fp->is_hitstun = TRUE;

    /* Push: horizontal away from the ledge (lr is facing direction),
     * vertical up. vel_damage_air decays ~1.7/frame during air damage. */
    fp->physics.vel_damage_air.y = TRUMP_PUSH_VEL;
    fp->physics.vel_damage_air.x = -TRUMP_PUSH_VEL * (f32)fp->lr;

    if (!sLoggedFirst) {
        mod_log("[LedgeTrump] trumped port %u (lr=%d)\n", fp->player, fp->lr);
        sLoggedFirst = 1;
    }
}

static sb32 HookedRunSpecialCollisions(MPCollData *coll_data, GObj *fighter_gobj, u32 flags)
{
    FTStruct *this_fp = ftGetStruct(fighter_gobj);
    GObj *cliffcatch_gobj;
    FTStruct *cliffcatch_fp;
    sb32 is_ceilstop = FALSE;
    sb32 is_collide;

    if (mpProcessCheckTestLWallCollisionAdjNew(coll_data) != FALSE)
    {
        mpProcessRunLWallCollisionAdjNew(coll_data);
    }
    if (mpProcessCheckTestRWallCollisionAdjNew(coll_data) != FALSE)
    {
        mpProcessRunRWallCollisionAdjNew(coll_data);
    }
    if (mpProcessCheckTestCeilCollisionAdjNew(coll_data) != FALSE)
    {
        mpProcessRunCeilCollisionAdjNew(coll_data);

        if (coll_data->mask_stat & MAP_FLAG_CEIL)
        {
            mpProcessRunCeilEdgeAdjust(coll_data);
        }
        if ((flags & MAP_PROC_TYPE_CEILHEAVY) && (this_fp->physics.vel_air.y >= 30.0F))
        {
            coll_data->mask_curr |= MAP_FLAG_CEILHEAVY;

            is_ceilstop = TRUE;

            coll_data->is_coll_end = TRUE;
        }
    }
    is_collide = (flags & MAP_PROC_TYPE_PASS)
               ? mpProcessCheckTestFloorCollisionAdjNew(coll_data, sMPCommonProcPassRef != NULL ? *sMPCommonProcPassRef : NULL, fighter_gobj)
               : mpProcessRunFloorCollisionAdjNewNULL(coll_data);

    if (is_collide != FALSE)
    {
        if (flags & MAP_PROC_TYPE_PROJECT)
        {
            mpProcessSetCollideFloor(coll_data);

            if (coll_data->mask_stat & MAP_FLAG_FLOOR)
            {
                mpProcessRunFloorEdgeAdjust(coll_data);
            }
            else mpProcessSetCollProjectFloorID(coll_data);
        }
        else
        {
            mpProcessSetLandingFloor(coll_data);
            mpCommonSetFighterLandingParams(fighter_gobj);

            if (coll_data->mask_stat & MAP_FLAG_FLOOR)
            {
                mpProcessRunFloorEdgeAdjust(coll_data);

                coll_data->is_coll_end = TRUE;

                return TRUE;
            }
        }
    }
    else mpProcessSetCollProjectFloorID(coll_data);

    if ((flags & MAP_PROC_TYPE_CLIFF) && (this_fp->cliffcatch_wait == 0))
    {
        if ((mpProcessCheckTestLCliffCollision(coll_data) != FALSE) || (mpProcessCheckTestRCliffCollision(coll_data) != FALSE))
        {
            if (gCVarGetInteger(TRUMP_CVAR, 0) != 0)
            {
                sb32 is_occupied = FALSE;

                cliffcatch_gobj = (*sGCCommonLinks)[nGCCommonLinkIDFighter];

                while (cliffcatch_gobj != NULL)
                {
                    if (cliffcatch_gobj != fighter_gobj)
                    {
                        cliffcatch_fp = ftGetStruct(cliffcatch_gobj);

                        if ((cliffcatch_fp->is_cliff_hold) && (this_fp->coll_data.cliff_id == cliffcatch_fp->coll_data.cliff_id) && (this_fp->lr == cliffcatch_fp->lr))
                        {
                            is_occupied = TRUE;

                            break;
                        }
                    }
                    cliffcatch_gobj = cliffcatch_gobj->link_next;
                }

                /* Trump only a settled holder (Remix checks CliffCatch/CliffWait). */
                if ((is_occupied != FALSE) && ((cliffcatch_fp->status_id == nFTCommonStatusCliffCatch) || (cliffcatch_fp->status_id == nFTCommonStatusCliffWait)))
                {
                    TrumpLedgeHolder(cliffcatch_gobj);
                    /* fall through: the grab proceeds */
                }
                else if (is_occupied != FALSE)
                {
                    return is_ceilstop; /* vanilla hog: holder not trumpable yet */
                }
            }
            else
            {
                cliffcatch_gobj = (*sGCCommonLinks)[nGCCommonLinkIDFighter];

                while (cliffcatch_gobj != NULL)
                {
                    if (cliffcatch_gobj != fighter_gobj)
                    {
                        cliffcatch_fp = ftGetStruct(cliffcatch_gobj);

                        if ((cliffcatch_fp->is_cliff_hold) && (this_fp->coll_data.cliff_id == cliffcatch_fp->coll_data.cliff_id) && (this_fp->lr == cliffcatch_fp->lr))
                        {
                            return is_ceilstop;
                        }
                        else goto next_gobj;
                    }
                next_gobj:
                    cliffcatch_gobj = cliffcatch_gobj->link_next;
                }
            }

            mpCommonSetFighterLandingParams(fighter_gobj);

            coll_data->is_coll_end = TRUE;

            return TRUE;
        }
    }
    return is_ceilstop;
}

MOD_INIT() {
    gCVarGetInteger = (CVarGetIntegerFn)mod_resolve_symbol("CVarGetInteger");
    sGCCommonLinks = (GObj ***)mod_resolve_symbol("gGCCommonLinks");
    sMPCommonProcPassRef = (ProcMapFn *)mod_resolve_symbol("sMPCommonProcPass");
    if (gCVarGetInteger == NULL || sGCCommonLinks == NULL) {
        mod_log("[LedgeTrump] FATAL: CVarGetInteger=%p gGCCommonLinks=%p\n",
                (void*)gCVarGetInteger, (void*)sGCCommonLinks);
        return;
    }
    mod_log("[LedgeTrump] sMPCommonProcPass via PDB: %s\n",
            sMPCommonProcPassRef != NULL ? "resolved" : "NOT FOUND (pass floors fall back to NULL)");

    int rc = mod_install_hook("mpCommonRunFighterSpecialCollisions",
                              (void*)HookedRunSpecialCollisions,
                              (void**)&gOrigRunSpecialCollisions);
    if (rc == 0) {
        mod_log("[LedgeTrump] hook installed OK (trampoline=%p, toggle '%s' = %d)\n",
                (void*)gOrigRunSpecialCollisions, TRUMP_CVAR,
                gCVarGetInteger(TRUMP_CVAR, 0));
    } else {
        mod_log("[LedgeTrump] hook install FAILED rc=%d\n", rc);
    }
}

MOD_EXIT() {
    mod_log("[LedgeTrump] exit OK\n");
}
