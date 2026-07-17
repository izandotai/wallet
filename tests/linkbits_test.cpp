#ifdef _WIN32

#include <doctest/doctest.h>

#include <cstdint>

#include <windows.h>

// §3.1 hardening item 5: the exploit-mitigation link bits, audited from
// inside the running binary. Every izan executable links with the same
// global flags, so this test standing green is the CI check for the
// shipped exes too. GCC cannot emit Control Flow Guard; that gap is a
// recorded fact, not a silent one — see the comment at the end.
TEST_CASE("link bits: DEP, ASLR and high-entropy VA are on and real")
{
    const auto* base
        = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    REQUIRE(base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    REQUIRE(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto* nt
        = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    REQUIRE(nt->Signature == IMAGE_NT_SIGNATURE);
    REQUIRE(nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    const WORD dll = nt->OptionalHeader.DllCharacteristics;
    CHECK((dll & IMAGE_DLLCHARACTERISTICS_NX_COMPAT));
    CHECK((dll & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE));
    CHECK((dll & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA));

    // The ASLR bit is only as honest as the relocation table behind
    // it: a stripped .reloc leaves the image loadable at exactly one
    // address and the bit becomes marketing.
    const IMAGE_DATA_DIRECTORY& reloc
        = nt->OptionalHeader
              .DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    CHECK(reloc.Size > 0);

    // CFG (IMAGE_DLLCHARACTERISTICS_GUARD_CF) cannot be produced by
    // the GCC toolchain; its absence here is expected and documented
    // in the security page rather than papered over.
    if (dll & IMAGE_DLLCHARACTERISTICS_GUARD_CF)
        MESSAGE("CFG unexpectedly present — update the security page");
}

#endif
