/* OS CSPRNG behind trezor-crypto's rand.h contract. The upstream rand.c is a
 * weak placeholder and must never be linked into a wallet. */
#include <rand.h>

#include <stdlib.h>

#ifdef _WIN32

#include <windows.h>

#include <bcrypt.h>

void random_buffer(uint8_t* buf, size_t len)
{
    if (BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG)
        != 0)
        abort();
}

#else

#include <sys/random.h>

void random_buffer(uint8_t* buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = getrandom(buf + off, len - off, 0);
        if (n < 0)
            abort();
        off += (size_t)n;
    }
}

#endif

uint32_t random32(void)
{
    uint32_t v;
    random_buffer((uint8_t*)&v, sizeof v);
    return v;
}
