// SPDX-License-Identifier: AGPL-3.0-only

#include "rtti_vtable.hpp"

#include "log.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <elf.h>
#include <mutex>
#include <vector>

namespace RttiVTable
{
namespace
{

struct ElfSection
{
	std::string name;
	lm_address_t rva = 0;
	lm_address_t size = 0;
};

std::mutex g_mutex;
std::unordered_map<lm_address_t, ModuleIndex> g_cache;

bool readElfSections(const char* path, std::vector<ElfSection>& out)
{
	if (!path || path[0] == '\0') return false;
	FILE* fp = std::fopen(path, "rb");
	if (!fp) return false;

	Elf64_Ehdr ehdr {};
	if (std::fread(&ehdr, sizeof(ehdr), 1, fp) != 1 ||
	    std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
	    ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
	    ehdr.e_shoff == 0 ||
	    ehdr.e_shnum == 0 ||
	    ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
	    ehdr.e_shstrndx >= ehdr.e_shnum)
	{
		std::fclose(fp);
		return false;
	}

	std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
	if (std::fseek(fp, static_cast<long>(ehdr.e_shoff), SEEK_SET) != 0 ||
	    std::fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum)
	{
		std::fclose(fp);
		return false;
	}

	const Elf64_Shdr& shstr = shdrs[ehdr.e_shstrndx];
	if (shstr.sh_size == 0 || shstr.sh_size > 16 * 1024 * 1024)
	{
		std::fclose(fp);
		return false;
	}

	std::string strtab(shstr.sh_size, '\0');
	if (std::fseek(fp, static_cast<long>(shstr.sh_offset), SEEK_SET) != 0 ||
	    std::fread(strtab.data(), 1, shstr.sh_size, fp) != shstr.sh_size)
	{
		std::fclose(fp);
		return false;
	}
	std::fclose(fp);

	out.clear();
	out.reserve(shdrs.size());
	for (const auto& sh : shdrs)
	{
		if (sh.sh_name >= strtab.size()) continue;
		const char* secName = strtab.c_str() + sh.sh_name;
		if (secName[0] == '\0') continue;
		out.push_back(ElfSection {
			.name = secName,
			.rva = static_cast<lm_address_t>(sh.sh_addr),
			.size = static_cast<lm_address_t>(sh.sh_size),
		});
	}
	return !out.empty();
}

const ElfSection* findSection(const std::vector<ElfSection>& sections, const char* name)
{
	for (const auto& s : sections)
	{
		if (s.name == name) return &s;
	}
	return nullptr;
}

// Resolve a pointer-sized slot in a loaded module, handling both relocated
// virtual addresses (after ld.so relocations) and unrelocated RVAs.
bool resolveModulePtr(lm_address_t rawVal,
                      const lm_module_t& mod,
                      lm_address_t& outPtr)
{
	if (rawVal == 0) return false;
	if (rawVal >= mod.base && rawVal < mod.end)
	{
		outPtr = rawVal;
		return true;
	}
	if (rawVal < mod.size)
	{
		outPtr = mod.base + rawVal;
		return true;
	}
	return false;
}

void collectStrings(const lm_module_t& mod,
                    const ElfSection& section,
                    std::unordered_map<lm_address_t, std::string>& out)
{
	const lm_address_t start = mod.base + section.rva;
	const lm_address_t end = start + section.size;
	if (start < mod.base || end > mod.end || start >= end) return;

	for (lm_address_t addr = start; addr < end;)
	{
		const char* p = reinterpret_cast<const char*>(addr);
		const lm_address_t maxLen = end - addr;
		lm_address_t len = 0;
		bool nul = false;
		while (len < maxLen)
		{
			const unsigned char c = static_cast<unsigned char>(p[len]);
			if (c == '\0')
			{
				nul = true;
				break;
			}
			if (!std::isprint(c)) break;
			++len;
		}
		addr += (len + 1);
		if (nul && len >= 4)
		{
			out.emplace(reinterpret_cast<lm_address_t>(p), std::string(p, len));
		}
	}
}

void collectTypeInfos(const lm_module_t& mod,
                      const ElfSection& section,
                      ModuleIndex& idx)
{
	const lm_address_t start = mod.base + section.rva;
	const lm_address_t end = start + section.size;
	if (start < mod.base || end > mod.end || start >= end) return;

	for (lm_address_t addr = start + sizeof(lm_address_t);
	     addr + sizeof(lm_address_t) <= end;
	     addr += sizeof(lm_address_t))
	{
		const lm_address_t rawVal = *reinterpret_cast<const lm_address_t*>(addr);
		lm_address_t ptr = 0;
		if (!resolveModulePtr(rawVal, mod, ptr)) continue;
		const auto it = idx.strings.find(ptr);
		if (it != idx.strings.end())
		{
			const lm_address_t typeInfoAddr = addr - sizeof(lm_address_t);
			idx.typeInfos.emplace(typeInfoAddr, it->second);
		}
	}
}

void populateVTableFunctions(const lm_module_t& mod,
                             const ModuleIndex& idx,
                             VFTable& vft)
{
	const lm_address_t firstFn = vft.address + sizeof(lm_address_t) * 2;
	if (firstFn < mod.base || firstFn >= mod.end) return;

	for (unsigned int i = 0; ; ++i)
	{
		const lm_address_t slotAddr = firstFn + i * sizeof(lm_address_t);
		if (slotAddr + sizeof(lm_address_t) > mod.end) break;
		const lm_address_t rawVal = *reinterpret_cast<const lm_address_t*>(slotAddr);
		if (rawVal == 0) break;

		lm_address_t fnPtr = 0;
		if (!resolveModulePtr(rawVal, mod, fnPtr)) break;
		if (idx.textStart != 0 && (fnPtr < idx.textStart || fnPtr >= idx.textEnd))
		{
			break;
		}
		vft.functions.push_back(fnPtr);
	}
}

void collectVFTables(const lm_module_t& mod,
                     const ElfSection& section,
                     ModuleIndex& idx)
{
	const lm_address_t start = mod.base + section.rva;
	const lm_address_t end = start + section.size;
	if (start < mod.base || end > mod.end || start >= end) return;

	for (lm_address_t addr = start + sizeof(lm_address_t);
	     addr + sizeof(lm_address_t) <= end;
	     addr += sizeof(lm_address_t))
	{
		const lm_address_t rawVal = *reinterpret_cast<const lm_address_t*>(addr);
		lm_address_t ptr = 0;
		if (!resolveModulePtr(rawVal, mod, ptr)) continue;

		const auto ti = idx.typeInfos.find(ptr);
		if (ti == idx.typeInfos.end()) continue;

		VFTable vft;
		vft.address = addr - sizeof(lm_address_t);
		populateVTableFunctions(mod, idx, vft);
		if (vft.functions.empty()) continue;

		auto existing = idx.vftables.find(ti->second);
		if (existing != idx.vftables.end())
		{
			existing->second.subclasses.push_back(std::move(vft));
		}
		else
		{
			idx.vftables.emplace(ti->second, std::move(vft));
		}
	}
}

ModuleIndex scanModule(const lm_module_t& mod)
{
	ModuleIndex idx;
	idx.scanned = true;
	idx.base = mod.base;
	idx.end = mod.end;

	std::vector<ElfSection> sections;
	if (!readElfSections(mod.path, sections))
	{
		if (g_pLog)
		{
			g_pLog->warn("RttiVTable: readElfSections failed for '%s'\n",
			             mod.path ? mod.path : "(null)");
		}
		return idx;
	}

	if (const auto* text = findSection(sections, ".text"))
	{
		idx.textStart = mod.base + text->rva;
		idx.textEnd = idx.textStart + text->size;
	}
	if (const auto* rodata = findSection(sections, ".rodata"))
	{
		idx.rodataStart = mod.base + rodata->rva;
		idx.rodataEnd = idx.rodataStart + rodata->size;
		collectStrings(mod, *rodata, idx.strings);
	}
	for (const auto& sec : sections)
	{
		if (sec.name.rfind(".rodata.str", 0) == 0)
		{
			collectStrings(mod, sec, idx.strings);
		}
	}
	if (const auto* dataRelRo = findSection(sections, ".data.rel.ro"))
	{
		collectTypeInfos(mod, *dataRelRo, idx);
		collectVFTables(mod, *dataRelRo, idx);
	}
	if (const auto* dataRelRoLocal = findSection(sections, ".data.rel.ro.local"))
	{
		collectVFTables(mod, *dataRelRoLocal, idx);
	}

	idx.valid = !idx.vftables.empty();
	if (g_pLog)
	{
		g_pLog->debug("RttiVTable: indexed %s (%zu strings, %zu typeInfos, %zu vftables)\n",
		              mod.name, idx.strings.size(), idx.typeInfos.size(), idx.vftables.size());
	}
	return idx;
}

ModuleIndex& getOrScanLocked(const lm_module_t& mod)
{
	auto it = g_cache.find(mod.base);
	if (it != g_cache.end() && it->second.scanned)
	{
		return it->second;
	}
	auto [inserted, _] = g_cache.emplace(mod.base, scanModule(mod));
	return inserted->second;
}

const std::unordered_map<std::string, unsigned int>& getOrBuildMapLocked(
	ModuleIndex& idx,
	const char* mapTypeName)
{
	auto existing = idx.mapSlots.find(mapTypeName);
	if (existing != idx.mapSlots.end())
	{
		return existing->second;
	}

	auto [inserted, _] = idx.mapSlots.emplace(
		mapTypeName, std::unordered_map<std::string, unsigned int>{});
	auto& fnMap = inserted->second;

	const auto vftIt = idx.vftables.find(mapTypeName);
	if (vftIt == idx.vftables.end())
	{
		return fnMap;
	}

	const auto& functions = vftIt->second.functions;
	for (unsigned int slot = 0; slot < static_cast<unsigned int>(functions.size()); ++slot)
	{
		const lm_address_t fn = functions[slot];
		if (fn < idx.textStart || fn >= idx.textEnd)
		{
			continue;
		}

		const uint8_t* code = reinterpret_cast<const uint8_t*>(fn);
		const lm_address_t avail = idx.textEnd - fn;
		const size_t maxBytes = avail < 256 ? static_cast<size_t>(avail) : 256;

		for (size_t off = 0; off + 7 <= maxBytes; ++off)
		{
			if (off + 1 < maxBytes && code[off] == 0xCC && code[off + 1] == 0xCC)
			{
				break;
			}
			// x86_64 RIP-relative LEA: REX (0x40..0x4F) + 0x8D + ModRM (mod=00, rm=101) + disp32
			if ((code[off] & 0xF0) != 0x40 || code[off + 1] != 0x8D ||
			    (code[off + 2] & 0xC7) != 0x05)
			{
				continue;
			}

			int32_t disp = 0;
			std::memcpy(&disp, code + off + 3, sizeof(disp));
			const lm_address_t target = static_cast<lm_address_t>(
				static_cast<int64_t>(fn + off + 7) + static_cast<int64_t>(disp));

			const auto strIt = idx.strings.find(target);
			if (strIt == idx.strings.end())
			{
				continue;
			}

			const std::string& rawStr = strIt->second;
			if (std::strstr(mapTypeName, rawStr.c_str()) != nullptr)
			{
				continue;
			}

			std::string key = rawStr;
			if (fnMap.find(key) != fnMap.end())
			{
				unsigned int dup = 2;
				for (;;)
				{
					std::string candidate = rawStr + std::to_string(dup);
					if (fnMap.find(candidate) == fnMap.end())
					{
						key = std::move(candidate);
						break;
					}
					++dup;
				}
			}
			fnMap.emplace(std::move(key), slot);
		}
	}

	return fnMap;
}

} // namespace

const ModuleIndex& indexForModule(const lm_module_t& mod)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return getOrScanLocked(mod);
}

lm_address_t resolveSlot(const lm_module_t& mod,
                         const char* typeName,
                         unsigned int slotIndex,
                         unsigned int subClassIndex)
{
	if (!typeName || slotIndex == NO_INDEX || mod.base == 0 || mod.size == 0)
	{
		return LM_ADDRESS_BAD;
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	const ModuleIndex& idx = getOrScanLocked(mod);
	if (!idx.valid) return LM_ADDRESS_BAD;

	const auto vftIt = idx.vftables.find(typeName);
	if (vftIt == idx.vftables.end()) return LM_ADDRESS_BAD;

	const VFTable* vft = &vftIt->second;
	if (subClassIndex != NO_INDEX)
	{
		if (subClassIndex >= vft->subclasses.size()) return LM_ADDRESS_BAD;
		vft = &vft->subclasses[subClassIndex];
	}

	if (slotIndex >= vft->functions.size()) return LM_ADDRESS_BAD;
	return vft->functions[slotIndex];
}

unsigned int resolveMapSlot(const lm_module_t& mod,
                            const char* mapTypeName,
                            const char* fnName)
{
	if (!mapTypeName || !fnName || mod.base == 0 || mod.size == 0)
	{
		return NO_INDEX;
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	ModuleIndex& idx = getOrScanLocked(mod);
	if (!idx.valid) return NO_INDEX;

	const auto& fnMap = getOrBuildMapLocked(idx, mapTypeName);
	const auto it = fnMap.find(fnName);
	if (it == fnMap.end()) return NO_INDEX;
	return it->second;
}

void clearCache()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_cache.clear();
}

} // namespace RttiVTable

