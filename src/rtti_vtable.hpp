// SPDX-License-Identifier: AGPL-3.0-only
//
// ELF64 RTTI vtable & interface-map resolver (x86_64).
// Scans .rodata and .data.rel.ro in loaded Steam shared libraries
// (steamclient.so, steamui.so) to locate virtual method addresses by Itanium
// ABI RTTI typeinfo name + vtable slot index (or referenced string name),
// avoiding brittle byte-signatures across Steam client updates.

#pragma once

#include <libmem/libmem.h>

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace RttiVTable
{

constexpr unsigned int NO_INDEX = (std::numeric_limits<unsigned int>::max)();

struct VFTable
{
	lm_address_t address = 0;
	std::vector<lm_address_t> functions;
	std::vector<VFTable> subclasses;
};

struct ModuleIndex
{
	bool scanned = false;
	bool valid = false;
	lm_address_t base = 0;
	lm_address_t end = 0;
	lm_address_t textStart = 0;
	lm_address_t textEnd = 0;
	lm_address_t rodataStart = 0;
	lm_address_t rodataEnd = 0;

	std::unordered_map<lm_address_t, std::string> strings;
	std::unordered_map<lm_address_t, std::string> typeInfos;
	std::unordered_map<std::string, VFTable> vftables;
	std::unordered_map<std::string, std::unordered_map<std::string, unsigned int>> mapSlots;
};

// Scan (once per module base) ELF64 RTTI typeinfos and vtables.
const ModuleIndex& indexForModule(const lm_module_t& mod);

// Resolve a virtual method by RTTI mangled class name (e.g. "12CCMInterface")
// and vtable slot index. Pass subClassIndex == NO_INDEX for primary vtable.
// Returns LM_ADDRESS_BAD if not found or outside executable .text.
lm_address_t resolveSlot(const lm_module_t& mod,
                         const char* typeName,
                         unsigned int slotIndex,
                         unsigned int subClassIndex = NO_INDEX);

// Resolve a vtable slot index inside an IClient*Map class (e.g. "14IClientUserMap")
// by scanning each map wrapper function for RIP-relative string references to fnName.
// Returns NO_INDEX if not found.
unsigned int resolveMapSlot(const lm_module_t& mod,
                            const char* mapTypeName,
                            const char* fnName);

// Release cached scan tables (strings/typeInfos/vftables) after startup hook
// resolution completes.
void clearCache();

} // namespace RttiVTable

