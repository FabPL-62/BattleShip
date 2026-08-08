/*
 * mod_overlay.cpp — on-screen text overlay bridge for TCC mods.
 *
 * Why this exists: the engine's export table (BattleShip.def) carries no
 * ImGui symbols, and the render events (EngineRenderModsEvent et al.) are
 * registered but never fired, so a TCC mod has no way to paint pixels.
 * This bridge is the minimal, reusable answer: mods manage keyed text
 * entries through three tiny C functions below; the engine paints them
 * once per frame.
 *
 * The paint hook is a funchook detour on Ship::GameOverlay::Draw (via
 * HookManager, resolved from the PDB by SymbolResolver — the symbol is
 * not in the .def either). libultraship calls GameOverlay::Draw every
 * presented frame from Gui::DrawGame, inside the ImGui frame, so the
 * detour is a reliable in-frame render point that needs ZERO changes to
 * the libultraship submodule. Entries are painted into ImGui's
 * foreground draw list: on top of the game image and the ESC menu.
 */
#include "mod_overlay.h"

#include "HookManager.h"
#include "../port_log.h"

#include <imgui.h>
#include <imgui_internal.h> /* ImGuiWindow, ImGui::GetCurrentWindow */

#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32) || defined(__CYGWIN__)
#  define MOD_BRIDGE_EXPORT extern "C" __declspec(dllexport)
#else
#  define MOD_BRIDGE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace ssb64::mods {
namespace {

struct OverlayEntry {
    float x;
    float y;
    unsigned int rgba; /* 0xRRGGBBAA */
    int shadow;
    std::string text;
};

std::mutex sOverlayMutex;
std::unordered_map<std::string, OverlayEntry> sOverlayEntries;

/* Ship::GameOverlay::Draw is a non-virtual C++ method: on MSVC x64 the
 * implicit `this` is simply the first argument of the free-function
 * signature, which is all the detour needs. */
typedef void (*GameOverlayDrawFn)(void* self);
GameOverlayDrawFn sGameOverlayDrawOrig = nullptr;

void GameOverlayDrawHook(void* self) {
    sGameOverlayDrawOrig(self);

    std::lock_guard<std::mutex> lock(sOverlayMutex);
    if (sOverlayEntries.empty()) {
        return;
    }

    /* In-frame by construction (GameOverlay::Draw runs from
     * Gui::DrawGame, inside the "Main Game" window's Begin/End). Entry
     * coordinates are relative to that window, which spans the whole
     * work area (below the title bar); the N64 framebuffer is centered
     * inside it by DrawGame AFTER this hook runs, so exact image-rect
     * anchoring isn't available here — overlay space = work area. */
    ImVec2 origin(0.0f, 0.0f);
    if (ImGuiWindow* win = ImGui::GetCurrentWindow()) {
        origin = win->Pos; /* "Main Game" window top-left */
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    for (const auto& kv : sOverlayEntries) {
        const OverlayEntry& e = kv.second;
        const unsigned int a = e.rgba & 0xFFu;
        const ImU32 col = IM_COL32((e.rgba >> 24) & 0xFFu, (e.rgba >> 16) & 0xFFu, (e.rgba >> 8) & 0xFFu, a);
        const float x = origin.x + e.x;
        const float y = origin.y + e.y;
        if (e.shadow) {
            draw->AddText(ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, a), e.text.c_str());
        }
        draw->AddText(ImVec2(x, y), col, e.text.c_str());
    }
}

} // namespace

bool ModOverlayInit() {
    /* SymbolResolver resolves straight from the PDB (SymFromName), where
     * C++ symbols may only match their undecorated form depending on
     * dbghelp's SYMOPT_UNDNAME handling — try both spellings. */
    static const char* const kDrawSymbolNames[] = {
        "Ship::GameOverlay::Draw",          /* undecorated C++ name */
        "?Draw@GameOverlay@Ship@@QEAAXXZ",  /* MSVC-decorated name   */
    };

    int rc = 1;
    unsigned int i;
    for (i = 0; i < sizeof(kDrawSymbolNames) / sizeof(kDrawSymbolNames[0]); i++) {
        rc = HookManager::InstallHook(kDrawSymbolNames[i],
                                      (void*)&GameOverlayDrawHook,
                                      (void**)&sGameOverlayDrawOrig);
        if (rc == 0) {
            port_log("[mods] overlay bridge active (hooked %s)\n", kDrawSymbolNames[i]);
            return true;
        }
    }
    port_log("[mods] overlay bridge: hook on Ship::GameOverlay::Draw failed (rc=%d)\n", rc);
    return false;
}

} // namespace ssb64::mods

/* ------------------------------------------------------------------ */
/*  Mod-facing C API (exported from the engine binary)                 */
/* ------------------------------------------------------------------ */

MOD_BRIDGE_EXPORT void mod_overlay_text(const char* key, float x, float y,
                                        unsigned int rgba, int shadow, const char* text) {
    if (key == nullptr || text == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(ssb64::mods::sOverlayMutex);
    ssb64::mods::sOverlayEntries[key] = { x, y, rgba, shadow, text };
}

MOD_BRIDGE_EXPORT void mod_overlay_remove(const char* key) {
    if (key == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(ssb64::mods::sOverlayMutex);
    ssb64::mods::sOverlayEntries.erase(key);
}

MOD_BRIDGE_EXPORT void mod_overlay_clear(void) {
    std::lock_guard<std::mutex> lock(ssb64::mods::sOverlayMutex);
    ssb64::mods::sOverlayEntries.clear();
}
