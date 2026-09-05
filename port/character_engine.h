#ifndef SSB64_CHARACTER_ENGINE_H
#define SSB64_CHARACTER_ENGINE_H
/* IDs must fit the reloc format's u16 dependency lists. */
enum { PORT_CE_DKULT_MAIN = 0x7000, PORT_CE_DKULT_CHARACTER = 0x7001 };
#ifdef __cplusplus
#include <memory>
class RelocFile;
std::shared_ptr<RelocFile> port_ce_find_resource(unsigned int id);
extern "C" {
#endif
/* Register immutable BE sources without touching scene heaps/caches.
 * Returns 0 on success (including repeat calls), -1 for invalid/missing packs.
 * Sources remain engine-owned until shutdown. Restart to replace assets. */
int port_ce_load_character_assets(const char* name);
/* Opt-in battle-time integration check. Does not change fighter FTData. */
int port_ce_probe_dkult_assets(void);
#ifdef __cplusplus
}
#endif
#endif
