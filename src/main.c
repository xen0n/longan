#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define __unused __attribute__((unused))

typedef long (*payload_fn_t)(void);

static void *payload;

static uint32_t insn_dj(uint32_t opcode, uint32_t d, uint32_t j)
{
    assert(d <= 31);
    assert(j <= 31);
    return (opcode & 0xfffffc00) | (j << 5) | d;
}

static void emit_n_insns_dj(void *buf, int n, uint32_t opcode)
{
    uint32_t *insns = (uint32_t *)buf;

    // the structure of the emitted function:
    //
    // rdtime start
    // .rept n
    // <insn>
    // .endr
    // rdtime end
    // return end - start

    *insns++ = 0x00006804;  // rdtime.d $a0, $zero

    // opcode $a1, $a1, ...
    // and hope this is some real data dependency
    uint32_t fill = insn_dj(opcode, 5, 5);
    for (int i = 0; i < n; i++)
        *insns++ = fill;

    *insns++ = 0x00006805;  // rdtime.d $a1, $zero
    *insns++ = 0x001190a4;  // sub.d $a0, $a1, $a0
    *insns++ = 0x4c000020;  // ret
}

static long measure_rdtime_delta(int n, uint32_t opcode)
{
    emit_n_insns_dj(payload, n, opcode);
    return ((payload_fn_t)payload)();
}

// 2.5GHz (cycle freq) / 100MHz (stable counter freq) = 25
// minimum granularity is 25 insns -> 1 rdtime delta
// for reliable measurement of quantization of the instruction pattern in the
// face of random fluctuations, let's make the repetition times larger than
// this still
#define N 10000

// too lazy to figure out an exact number
#define JIT_BUFFER_SIZE 131072

int main(int argc __unused, const char *const argv[] __unused)
{
    // RWX memory hehe
    payload = mmap(NULL, JIT_BUFFER_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);

    long delta;
    delta = measure_rdtime_delta(N, 0x00001000 /* clo.w */);
    printf("%d times of clo.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, 0x001c0000 /* mul.w */);
    printf("%d times of mul.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, 0x01140400 /* fabs.s */);
    printf("%d times of fabs.s: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, 0x01140800 /* fabs.d */);
    printf("%d times of fabs.d: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    munmap(payload, JIT_BUFFER_SIZE);

    return 0;
}
