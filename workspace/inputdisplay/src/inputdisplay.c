/*
 * inputdisplay.c — on-screen input display overlay (Fase 0 pilot of the
 * Smash Remix → BattleShip port; Remix source: src/InputDisplay.asm).
 *
 * Shows P1's held buttons and stick values as overlay text, refreshed
 * every frame from a GamePostUpdateEvent listener. Two engine facilities
 * do the heavy lifting:
 *
 *   - mod_resolve_symbol("gSYControllerDevices"): the controller-state
 *     globals are not in BattleShip.def (data symbols aren't exported),
 *     but SymbolResolver finds them in the PDB.
 *   - mod_overlay_text(): the port's overlay bridge paints the strings
 *     every frame (see port/mods/mod_overlay.cpp).
 */
#include "mod.h"

#include "port/hooks/Events.h"

/* Engine overlay bridge (port/mods/mod_overlay.cpp). */
extern HM_IMPORT void mod_overlay_text(const char* key, float x, float y,
                                       unsigned int rgba, int shadow, const char* text);
extern HM_IMPORT void mod_overlay_remove(const char* key);
extern HM_IMPORT void mod_overlay_clear(void);

/* SYController from decomp/src/sys/controller.h, redeclared locally so
 * the mod doesn't drag PR/os.h through the amalgamation. 10 bytes:
 *   0x00 u16 button_hold;  0x02 u16 button_tap; 0x04 u16 button_update;
 *   0x06 u16 button_release; 0x08 s8 stick_x;  0x09 s8 stick_y. */
typedef struct SYController {
    unsigned short button_hold;
    unsigned short button_tap;
    unsigned short button_update;
    unsigned short button_release;
    signed char stick_x;
    signed char stick_y;
} SYController;

/* N64 button masks (decomp/include/PR/os.h: CONT_*). */
#define ID_BTN_A      0x8000
#define ID_BTN_B      0x4000
#define ID_BTN_Z      0x2000
#define ID_BTN_START  0x1000
#define ID_BTN_L      0x0020
#define ID_BTN_R      0x0010
#define ID_BTN_CUP    0x0008
#define ID_BTN_CDOWN  0x0004
#define ID_BTN_CLEFT  0x0002
#define ID_BTN_CRIGHT 0x0001

/* 0xRRGGBBAA */
#define COL_ON    0x40FF40FFu  /* held: bright green   */
#define COL_OFF   0x80808066u  /* idle: dim gray ~40%  */
#define COL_STICK 0xFFFFFFFFu

#define ROW_X 30.0f
#define ROW_Y 200.0f

typedef struct ButtonDef {
    unsigned short mask;
    const char* label;
    const char* key;
    float dx; /* advance to next slot */
} ButtonDef;

static const ButtonDef sButtons[] = {
    { ID_BTN_A,      "A",  "id.p1.a",  22.0f },
    { ID_BTN_B,      "B",  "id.p1.b",  22.0f },
    { ID_BTN_Z,      "Z",  "id.p1.z",  22.0f },
    { ID_BTN_L,      "L",  "id.p1.l",  22.0f },
    { ID_BTN_R,      "R",  "id.p1.r",  22.0f },
    { ID_BTN_START,  "S",  "id.p1.s",  34.0f },
    { ID_BTN_CUP,    "C^", "id.p1.cu", 26.0f },
    { ID_BTN_CLEFT,  "C<", "id.p1.cl", 26.0f },
    { ID_BTN_CDOWN,  "Cv", "id.p1.cd", 26.0f },
    { ID_BTN_CRIGHT, "C>", "id.p1.cr", 26.0f },
};
#define NUM_BUTTONS (sizeof(sButtons) / sizeof(sButtons[0]))

static SYController* sPads = NULL; /* gSYControllerDevices[MAXCONTROLLERS] */
static ListenerID sFrameListener;

/* Minimal signed itoa (avoids stdio in the TCC amalgamation). */
static char* id_itoa(int v, char* out) {
    char tmp[12];
    int neg = 0, n = 0, i = 0;
    unsigned int u;
    if (v < 0) {
        neg = 1;
        u = (unsigned int)(-(v + 1)) + 1u;
    } else {
        u = (unsigned int)v;
    }
    do {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0u);
    if (neg) {
        out[i++] = '-';
    }
    while (n > 0) {
        out[i++] = tmp[--n];
    }
    out[i] = '\0';
    return out;
}

static void OnFrame(IEvent* event) {
    const SYController* pad;
    unsigned int i;
    float x = ROW_X;
    char num[12];
    char buf[24];

    if (sPads == NULL) {
        return;
    }
    pad = &sPads[0]; /* P1 */

    for (i = 0; i < (unsigned int)NUM_BUTTONS; i++) {
        int held = (pad->button_hold & sButtons[i].mask) != 0;
        mod_overlay_text(sButtons[i].key, x, ROW_Y, held ? COL_ON : COL_OFF, 1, sButtons[i].label);
        x += sButtons[i].dx;
    }

    /* Stick: "(x,y)" with signed bytes. */
    {
        char* p = buf;
        *p++ = '(';
        id_itoa((int)pad->stick_x, num);
        { char* q = num; while (*q != '\0') { *p++ = *q++; } }
        *p++ = ',';
        id_itoa((int)pad->stick_y, num);
        { char* q = num; while (*q != '\0') { *p++ = *q++; } }
        *p++ = ')';
        *p = '\0';
    }
    mod_overlay_text("id.p1.stick", ROW_X, ROW_Y + 20.0f, COL_STICK, 1, buf);
}

MOD_INIT() {
    sPads = (SYController*)mod_resolve_symbol("gSYControllerDevices");
    if (sPads == NULL) {
        mod_log("[InputDisplay] FATAL: gSYControllerDevices not resolvable via PDB\n");
        return;
    }
    sFrameListener = REGISTER_LISTENER(GamePostUpdateEvent, EVENT_PRIORITY_NORMAL, OnFrame);
    mod_log("[InputDisplay] init OK (pads @ %p)\n", (void*)sPads);
}

MOD_EXIT() {
    UNREGISTER_LISTENER(GamePostUpdateEvent, sFrameListener);
    mod_overlay_clear();
    mod_log("[InputDisplay] exit OK\n");
}
