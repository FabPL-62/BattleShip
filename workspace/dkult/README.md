# DKUlt: CharacterEngine pilot

The committed phase 2a registers fighter 27 and spawns a Donkey Kong clone.
ASSETS-A adds a source-resource registry for `main.bin` and `character.bin`.
The fighter still uses DK's FTData; animation/moveset overrides are ASSETS-B.

## Import and package (from the BattleShip directory)

```powershell
py -3 tools/import_dkult_assets.py '../SmashRemix2.0.1_+EXTRA_0.5.0/smashremix-plus-extra/extra_characters/DKUlt'
cmake --build workspace/dkult/build --config Release
Copy-Item workspace/dkult/output/* build/us/Release/mods/dkult -Recurse -Force
```

The importer reads the local Remix files without modifying them. Generated
assets under `assets/character/` are ignored by Git. Commit the importer, not
ROMs or copied assets. This pilot uses a loose mod directory, not an o2r pack.

## Resource loading

- IDs `0x7000` (main) and `0x7001` (character) fit the u16 dependency format.
- YAML offsets are byte offsets of internal/external relocation chains; the
  manifest converts them to word offsets. `0x3FFFC` maps to sentinel `0xFFFF`.
- Repeated dependencies in reqlists must be preserved: one entry per external
  chain node. `${CHARACTER}` maps to `0x7001`.
- The engine validates both resources before publishing the registry. Sources
  remain pristine BE bytes across scene resets. The normal reloc loader handles
  size calculation, dependency loading, token relocation and scene caches.
- These two assets are not standalone figatree animation files: do not force
  the animation halfswap on them. Standalone animations are a later step.
- Registration is idempotent. Restart the game after changing asset contents.
- This importer and its vanilla dependency IDs target the pinned US build.

## Verification

```powershell
py -3 -m unittest discover -s tools -p test_import_dkult_assets.py
cmake -S . -B build/us
cmake --build build/us --config Release
```

At startup expect `two source resources registered` and
`CharacterEngine source assets registered OK` in the log.

For the optional battle-time integration probe, set the following CVars in
the portable `build/us/Release/BattleShip.cfg.json` with the game closed:

```json
"dkult": { "spawnplayer": 0, "validateassets": 1 }
```

Place that object under `CVars.mods`. Start a local VS match with P1 active.
Expect `DKUlt scene probe: OK`, then repeat with a second match to exercise
scene reset/reloading. The probe uses engine-owned storage and preserves the
extern-heap cursor. It does not assign these assets to fighter FTData.
Turn `validateassets` off after testing. Check logs for relocation errors too:
the probe message confirms cache resolution, not rendered model correctness.

Next: map DKUlt action parameters into a private FTData/motion table, validate
one custom animation in combat, then extend movesets and special handlers.

Verified on 2026-09-04: six importer tests; real DKUlt import (2032-byte main,
76760-byte character); Release build; standalone TCC compilation; 3600-frame
startup smoke (exit 0); 7200-frame temporary diagnostic mod that called the
probe in fighter scenes across scene transitions (exit 0, no chain errors).
The diagnostic mod is disabled after testing. Local evidence is in
`build/probe-engine.log`. No custom model rendering or movesets are claimed.
