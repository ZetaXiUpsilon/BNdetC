#include "isa.h"

#include <assert.h>

int main(void)
{
    const uint32_t valid[] = {
        0x01230000u, 0x11230000u, 0x21230000u, 0x3100ffffu,
        0x4120ffffu, 0x51230000u, 0x61230000u, 0x71200000u,
        0x8000ffffu, 0x80b10000u, 0x91200000u, 0xa020ffffu,
        0xb0230000u, 0xc0000000u, 0xe0200000u, 0xf1000000u
    };
    const uint32_t invalid[] = {
        0x80020000u, 0x80a10001u, 0x81000000u, 0x80100001u,
        0xc0000001u, 0x90010000u, 0x70100001u, 0xf0110000u,
        0xd0000000u, 0xdfffffffu
    };
    Instruction decoded;
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
        assert(decode_instruction(valid[i], &decoded));
        assert(encode_instruction(decoded) == valid[i]);
    }
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(!decode_instruction(invalid[i], &decoded));
    assert(signed_word(0x7fff) == 32767);
    assert(signed_word(0x8000) == -32768);
    assert(signed_word(0xffff) == -1);
    puts("PASS: 16 legal formats and 10 rejected encodings");
    return 0;
}
