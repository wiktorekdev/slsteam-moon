// Unit test for the drift-tolerant ReconcilePin locators
// (ReconcilePin::detail::findBuilderCallRel32 / findCtxDepotVectorOffset).
//
// The RE'd offset of the `call rel32` to the target-vector builder inside
// EvaluateConfigChanges drifts when Steam recompiles the function (e.g. on the
// beta client). The locator tolerates a small shift but must never hook a
// guessed address: a candidate must be a call whose caller then pops the
// builder's eight stack arguments (`add esp,0x20`), the exact offset wins when
// it still holds, otherwise a nearby candidate is accepted ONLY when it is the
// unique one in the window, and ambiguity or absence gives up (safe disable).
//
// The content-update context's depot vector moves whenever Steam grows the
// context struct, so its offset is decoded from EvaluateConfigChanges' own
// per-entry loop instead of being pinned.
//
// Build: g++ -std=c++20 -I src tools/test_reconcilepin_scan.cpp -o /tmp/test_reconcilepin_scan && /tmp/test_reconcilepin_scan

#include "../src/feats/reconcilepin.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
	do {                                                                     \
		if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }       \
		else         { std::printf("ok:   %s\n", msg); }                     \
	} while (0)

using ReconcilePin::detail::findBuilderCallRel32;
using ReconcilePin::detail::findCtxDepotVectorOffset;
using ReconcilePin::detail::findCtxDepotVectorOffset64;
using ReconcilePin::detail::findBuilderCallSite64;

// x86_64: `mov r11,[rbx+disp32]; mov r14,r15; shl r14,5` plus the size load
// `movsxd rdx,[rbx+disp+0x10]` (steamrt64 EvaluateConfigChanges).
static void putDepotIndex64(std::vector<uint8_t>& buf, std::size_t off,
                            uint32_t disp)
{
	const uint8_t load[] = {0x4C, 0x8B, 0x9B};
	const uint8_t index[] = {0x4D, 0x89, 0xFE, 0x49, 0xC1, 0xE6, 0x05};
	std::memcpy(buf.data() + off, load, sizeof(load));
	std::memcpy(buf.data() + off + 3, &disp, sizeof(disp));
	std::memcpy(buf.data() + off + 7, index, sizeof(index));
}

static void putDepotSize64(std::vector<uint8_t>& buf, std::size_t off,
                           uint32_t disp)
{
	buf[off] = 0x48; buf[off + 1] = 0x63; buf[off + 2] = 0x93;
	std::memcpy(buf.data() + off + 3, &disp, sizeof(disp));
}

// x86_64: `push rax; mov rdi,[rbp+d32]; call rel32; test al,al; pop rdx; pop rcx`.
static void putBuilderCall64(std::vector<uint8_t>& buf, std::size_t off,
                             std::uintptr_t base, std::uintptr_t targetAbs)
{
	const uint8_t head[] = {0x50, 0x48, 0x8B, 0xBD, 0x40, 0xFE, 0xFF, 0xFF};
	std::memcpy(buf.data() + off, head, sizeof(head));
	buf[off + 8] = 0xE8;
	const int32_t rel = static_cast<int32_t>(
	    static_cast<intptr_t>(targetAbs) -
	    static_cast<intptr_t>(base + off + 13));
	std::memcpy(buf.data() + off + 9, &rel, sizeof(rel));
	const uint8_t tail[] = {0x84, 0xC0, 0x5A, 0x59};
	std::memcpy(buf.data() + off + 13, tail, sizeof(tail));
}

// Encode an `E8 rel32` at buf[off] whose near-call target is `targetAbs`,
// given the buffer's runtime base address `base`.  `cleanup` appends the
// caller's `add esp,0x20` that pops the builder's eight stack arguments.
static void putCall(std::vector<uint8_t>& buf, std::size_t off,
                    std::uintptr_t base, std::uintptr_t targetAbs,
                    bool cleanup = true)
{
	buf[off] = 0xE8;
	const int32_t rel = static_cast<int32_t>(
	    static_cast<intptr_t>(targetAbs) -
	    static_cast<intptr_t>(base + off + 5));
	std::memcpy(buf.data() + off + 1, &rel, sizeof(rel));
	if (cleanup)
	{
		buf[off + 5] = 0x83;
		buf[off + 6] = 0xC4;
		buf[off + 7] = 0x20;
	}
}

// `mov edi,[ebp+8]; mov eax,esi; shl eax,5; add eax,[edi+disp]`: the
// per-entry index into the context's DepotEntry vector.
static void putDepotIndex(std::vector<uint8_t>& buf, std::size_t off,
                          uint32_t disp, bool disp32)
{
	const uint8_t head[] = {0x8B, 0x7D, 0x08, 0x89, 0xF0, 0xC1, 0xE0, 0x05, 0x03};
	std::memcpy(buf.data() + off, head, sizeof(head));
	if (disp32)
	{
		buf[off + sizeof(head)] = 0x87;
		std::memcpy(buf.data() + off + sizeof(head) + 1, &disp, sizeof(disp));
	}
	else
	{
		buf[off + sizeof(head)] = 0x47;
		buf[off + sizeof(head) + 1] = static_cast<uint8_t>(disp);
	}
}

int main()
{
	constexpr std::size_t EXP = 0x183;   // the RE'd offset
	constexpr std::size_t WIN = 0x40;
	constexpr std::uintptr_t BASE = 0;   // code buffer at address 0 (small rels)
	// Fake "executable" region so decoded targets can be validated.
	const auto execOk = [](std::uintptr_t a) { return a >= 0x10000 && a < 0x20000; };

	const std::vector<uint8_t> nop(0x300, 0x90);  // NOP fill, no stray 0xE8

	// 1. Exact RE'd offset still holds -> used directly.
	{
		auto b = nop; putCall(b, EXP, BASE, 0x18000);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == static_cast<long>(EXP),
		      "exact RE'd offset with executable target is used");
	}

	// 2. Offset drifted (byte there is not a call), one nearby valid call.
	{
		auto b = nop; b[EXP] = 0xFF; putCall(b, EXP + 9, BASE, 0x18500);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == static_cast<long>(EXP + 9),
		      "a unique drifted call in the window is recovered");
	}

	// 3. Two valid calls in the window -> ambiguous -> safe disable.
	{
		auto b = nop; b[EXP] = 0xFF;
		putCall(b, EXP + 9, BASE, 0x18500);
		putCall(b, EXP + 22, BASE, 0x18600);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == -1, "two candidate calls in the window are rejected as ambiguous");
	}

	// 4. A call whose target is not executable is not a candidate.
	{
		auto b = nop; b[EXP] = 0xFF; putCall(b, EXP + 9, BASE, 0x99999);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == -1, "an E8 whose target is not executable is not accepted");
	}

	// 5. No call at all in the window -> disabled.
	{
		auto b = nop; b[EXP] = 0xFF;
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == -1, "no call in the window leaves the fix disabled");
	}

	// 6. The exact offset wins even if another valid call sits nearby.
	{
		auto b = nop; putCall(b, EXP, BASE, 0x18000); putCall(b, EXP + 16, BASE, 0x18700);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == static_cast<long>(EXP),
		      "the exact RE'd offset wins over a nearby call");
	}

	// 7. The 2026-10 client: the builder call moved by three bytes and the
	//    next call (two stack arguments, no 8-dword cleanup) now sits inside
	//    the window too.  Only the eight-argument call is the builder.
	{
		auto b = nop; b[EXP] = 0xFF;
		putCall(b, EXP + 3, BASE, 0x18500);
		putCall(b, EXP + 0x33, BASE, 0x18600, /*cleanup=*/false);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == static_cast<long>(EXP + 3),
		      "a call without the builder's 8-argument cleanup is not a candidate");
	}

	// 8. A call at the exact offset that does not pop eight arguments is a
	//    different callee and must not be redirected.
	{
		auto b = nop; putCall(b, EXP, BASE, 0x18000, /*cleanup=*/false);
		const long r = findBuilderCallRel32(b.data(), b.size(), BASE, EXP, WIN, execOk);
		CHECK(r == -1, "the exact offset without the 8-argument cleanup is rejected");
	}

	// 9. Context depot vector: disp8 form (client before 2026-10, ctx+0x78).
	{
		auto b = nop; putDepotIndex(b, 0x25b - 5, 0x78, false);
		CHECK(findCtxDepotVectorOffset(b.data(), b.size()) == 0x78,
		      "the disp8 depot-vector index decodes the context offset");
	}

	// 10. Context depot vector: disp32 form (2026-10 client, ctx+0x90).
	{
		auto b = nop; putDepotIndex(b, 0x263 - 5, 0x90, true);
		CHECK(findCtxDepotVectorOffset(b.data(), b.size()) == 0x90,
		      "the disp32 depot-vector index decodes the context offset");
	}

	// 11. Absent or ambiguous index loads keep the context patch disabled.
	{
		CHECK(findCtxDepotVectorOffset(nop.data(), nop.size()) == -1,
		      "no depot-vector index leaves the context patch disabled");
		auto b = nop;
		putDepotIndex(b, 0x40, 0x78, false);
		putDepotIndex(b, 0x80, 0x90, true);
		CHECK(findCtxDepotVectorOffset(b.data(), b.size()) == -1,
		      "two depot-vector index loads are rejected as ambiguous");
	}

	// 12. A sequence cut off by the readable range is not decoded.
	{
		auto b = nop; putDepotIndex(b, 0x10, 0x90, true);
		CHECK(findCtxDepotVectorOffset(b.data(), 0x10 + 11) == -1,
		      "a truncated disp32 index is not decoded");
	}

	// 13. Implausible displacements are rejected.
	{
		auto b = nop; putDepotIndex(b, 0x10, 0x7A, false);
		CHECK(findCtxDepotVectorOffset(b.data(), b.size()) == -1,
		      "a misaligned context offset is rejected");
		auto c = nop; putDepotIndex(c, 0x10, 0x20000, true);
		CHECK(findCtxDepotVectorOffset(c.data(), c.size()) == -1,
		      "an out-of-range context offset is rejected");
	}

	// 14. x86_64: the depot vector decodes only with its matching size load.
	{
		auto b = nop; putDepotIndex64(b, 0x218, 0xc0); putDepotSize64(b, 0x198, 0xd0);
		CHECK(findCtxDepotVectorOffset64(b.data(), b.size()) == 0xc0,
		      "x86_64: the depot-vector load decodes the context offset");
		auto c = nop; putDepotIndex64(c, 0x218, 0xc0); putDepotSize64(c, 0x198, 0xd8);
		CHECK(findCtxDepotVectorOffset64(c.data(), c.size()) == -1,
		      "x86_64: a base load without its size load is rejected");
		auto d = nop; putDepotIndex64(d, 0x100, 0xc0); putDepotIndex64(d, 0x218, 0xc0);
		putDepotSize64(d, 0x198, 0xd0);
		CHECK(findCtxDepotVectorOffset64(d.data(), d.size()) == -1,
		      "x86_64: two depot-vector loads are rejected as ambiguous");
		auto e = nop; putDepotIndex64(e, 0x218, 0xc4); putDepotSize64(e, 0x198, 0xd4);
		CHECK(findCtxDepotVectorOffset64(e.data(), e.size()) == -1,
		      "x86_64: a misaligned context offset is rejected");
	}

	// 15. x86_64: the builder call site is found by shape, validated, unique.
	{
		const auto isCode = [](std::uintptr_t t) { return t >= 0x10000 && t < 0x20000; };
		const std::uintptr_t fnBase = 0x1000;
		auto b = nop; putBuilderCall64(b, 0x196, fnBase, 0x15000);
		CHECK(findBuilderCallSite64(b.data(), b.size(), fnBase, isCode) == 0x19e,
		      "x86_64: the builder call site is located at its E8 byte");
		auto c = nop; putBuilderCall64(c, 0x196, fnBase, 0x500);
		CHECK(findBuilderCallSite64(c.data(), c.size(), fnBase, isCode) == -1,
		      "x86_64: a call whose target is not code is rejected");
		auto d = nop; putBuilderCall64(d, 0x96, fnBase, 0x15000);
		putBuilderCall64(d, 0x196, fnBase, 0x16000);
		CHECK(findBuilderCallSite64(d.data(), d.size(), fnBase, isCode) == -1,
		      "x86_64: two matching call sites are rejected as ambiguous");
		CHECK(findBuilderCallSite64(nop.data(), nop.size(), fnBase, isCode) == -1,
		      "x86_64: no call site leaves the target-local fix disabled");
	}

	if (g_failures == 0) std::printf("\nall reconcilepin-scan checks passed\n");
	else                 std::printf("\n%d reconcilepin-scan check(s) FAILED\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
