// Immutable BE source assets survive scene resets; relocated copies use scene heaps.
#include "character_engine.h"
#include "resource/RelocFile.h"
#include "resource/RelocFileTable.h"
#include <ship/Context.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" size_t lbRelocGetFileSize(unsigned int id);
extern "C" void* lbRelocGetExternHeapFile(unsigned int id, void* heap);
extern "C" void* lbRelocGetStatusBufferFile(unsigned int id);
extern "C" void* portRelocGetExternFileHeap(void);
extern "C" void portRelocSetExternFileHeap(void* heap);

namespace {
std::map<unsigned int, std::shared_ptr<RelocFile>> resources;
unsigned int WordOffset(const nlohmann::json& value) {
    const auto offset = value.get<int64_t>();
    if (offset < 0 || offset > 0xFFFF) throw std::runtime_error("invalid word offset");
    return static_cast<unsigned int>(offset);
}

} // namespace

extern "C" int port_ce_probe_dkult_assets(void) {
    if (!port_ce_find_resource(PORT_CE_DKULT_MAIN)) return -1;
    if (lbRelocGetStatusBufferFile(PORT_CE_DKULT_MAIN)) return 0;
    // Called after the scene has initialized its status buffers. Keep storage
    // alive until the next scene clears those entries; never use mod memory.
    static std::vector<uint8_t> heap;
    const size_t size = lbRelocGetFileSize(PORT_CE_DKULT_MAIN);
    if (!size) return -1;
    heap.resize(size + 16);
    void* previous = portRelocGetExternFileHeap();
    void* main = lbRelocGetExternHeapFile(PORT_CE_DKULT_MAIN, heap.data());
    portRelocSetExternFileHeap(previous);
    const bool ok = main && lbRelocGetStatusBufferFile(PORT_CE_DKULT_CHARACTER);
    spdlog::info("[CharacterEngine] DKUlt scene probe: {} ({} bytes including uncached dependencies)",
                 ok ? "OK" : "FAILED", size);
    return ok ? 0 : -1;
}
namespace {
void ValidateChain(const RelocFile& file, unsigned int start, bool internal) {
    std::set<unsigned int> seen;
    size_t count = 0;
    while (start != 0xFFFF) {
        const size_t offset = start * 4;
        if (offset + 4 > file.Data.size() || !seen.insert(start).second)
            throw std::runtime_error("invalid relocation chain");
        const auto* p = file.Data.data() + offset;
        const unsigned int target = (p[2] << 8) | p[3];
        if (internal && target * 4 >= file.Data.size())
            throw std::runtime_error("internal relocation target out of bounds");
        start = (p[0] << 8) | p[1];
        ++count;
    }
    if (!internal && count != file.ExternFileIds.size())
        throw std::runtime_error("external dependency count mismatch");
}
}
std::shared_ptr<RelocFile> port_ce_find_resource(unsigned int id) {
    const auto it = resources.find(id);
    return it == resources.end() ? nullptr : it->second;
}
extern "C" int port_ce_load_character_assets(const char* name) {
    // Explicit two-file reservation for the pilot. Reload is idempotent;
    // replacing source resources while a match is running is not supported.
    if (!name || std::string(name) != "dkult") return -1;
    if (resources.count(PORT_CE_DKULT_MAIN)) return 0;
    try {
        const auto dir = Ship::Context::GetPathRelativeToAppDirectory("mods/dkult/character/");
        std::ifstream manifest(dir + "assets.json");
        if (!manifest) throw std::runtime_error("missing assets.json; run import_dkult_assets.py");
        const auto json = nlohmann::json::parse(manifest);
        if (json.at("version") != 1 || json.at("files").size() != 2)
            throw std::runtime_error("unsupported asset manifest");
        std::map<unsigned int, std::shared_ptr<RelocFile>> pending;
        for (const auto& entry : json.at("files")) {
            const auto id = WordOffset(entry.at("id"));
            if (id != PORT_CE_DKULT_MAIN && id != PORT_CE_DKULT_CHARACTER)
                throw std::runtime_error("unexpected DKUlt file id");
            const std::string filename = id == PORT_CE_DKULT_MAIN ? "main.bin" : "character.bin";
            if (entry.at("path") != filename || pending.count(id))
                throw std::runtime_error("invalid or duplicate asset entry");
            auto file = std::make_shared<RelocFile>(std::make_shared<Ship::ResourceInitData>());
            file->FileId = id;
            file->RelocInternOffset = WordOffset(entry.at("intern_words"));
            file->RelocExternOffset = WordOffset(entry.at("extern_words"));
            for (const auto& dep : entry.at("dependencies")) {
                const auto depId = WordOffset(dep);
                if (depId >= RELOC_FILE_COUNT && depId != PORT_CE_DKULT_CHARACTER)
                    throw std::runtime_error("unknown dependency");
                file->ExternFileIds.push_back(static_cast<uint16_t>(depId));
            }
            std::ifstream input(dir + filename, std::ios::binary);
            if (!input) throw std::runtime_error("missing " + filename);
            file->Data.assign(std::istreambuf_iterator<char>(input), {});
            if (file->Data.empty() || file->Data.size() % 4 || file->Data.size() != entry.at("size"))
                throw std::runtime_error("invalid asset size");
            ValidateChain(*file, file->RelocInternOffset, true);
            ValidateChain(*file, file->RelocExternOffset, false);
            pending.emplace(id, std::move(file));
        }
        resources.swap(pending);
        spdlog::info("[CharacterEngine] DKUlt: two source resources registered (IDs {}, {}); scene loading ready",
                     static_cast<unsigned int>(PORT_CE_DKULT_MAIN), static_cast<unsigned int>(PORT_CE_DKULT_CHARACTER));
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("[CharacterEngine] {}", error.what());
        return -1;
    }
}
