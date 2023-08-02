#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#ifdef __loongarch__
#include <larchintrin.h>
#else
#error this program is only meant for usage on LoongArch
#endif

#define __unused __attribute__((unused))

typedef long (*payload_fn_t)(void);

enum measurement_mode {
    MODE_LINEAR_LATENCY,
    MODE_ISSUE_WIDTH,
};

static void *payload;

static uint32_t insn_dj(uint32_t opcode, uint32_t d, uint32_t j)
{
    assert(d <= 31);
    assert(j <= 31);
    return (opcode & 0xfffffc00) | (j << 5) | d;
}

static uint32_t *emit_linear_latency_seq(uint32_t *buf, int n, uint32_t opcode)
{
    // opcode $a1, $a1, ...
    // and hope this is some real data dependency
    uint32_t fill = insn_dj(opcode, 5, 5);
    for (int i = 0; i < n; i++)
        *buf++ = fill;
    return buf;
}

static uint32_t *emit_issue_width_seq(uint32_t *buf, int n, uint32_t opcode)
{
    for (int i = 0; i < n; i++) {
        // opcode $X, $X, ...
        // where X loops from $a1 (r5) to $t8 (r20)
        uint32_t reg = 5 + i % 16;
        *buf++ = insn_dj(opcode, reg, reg);
    }
    return buf;
}

static void emit_n_insns_dj(void *buf, int n, enum measurement_mode mode, uint32_t opcode)
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

    switch (mode) {
    case MODE_LINEAR_LATENCY:
        insns = emit_linear_latency_seq(insns, n, opcode);
        break;
    case MODE_ISSUE_WIDTH:
        insns = emit_issue_width_seq(insns, n, opcode);
        break;
    default:
        __builtin_unreachable();
    }

    *insns++ = 0x00006805;  // rdtime.d $a1, $zero
    *insns++ = 0x001190a4;  // sub.d $a0, $a1, $a0
    *insns++ = 0x4c000020;  // ret
}

static long measure_rdtime_delta(int n, enum measurement_mode mode, uint32_t opcode)
{
    emit_n_insns_dj(payload, n, mode, opcode);
    __dbar(0);
    __ibar(0);
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
    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, 0x00001000 /* clo.w */);
    printf("%d times of clo.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, 0x00001000 /* clo.w */);
    printf("%d times of independent clo.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, 0x001c0000 /* mul.w */);
    printf("%d times of mul.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, 0x001c0000 /* mul.w */);
    printf("%d times of independent mul.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, 0x00240000 /* crc.w.b.w */);
    printf("%d times of crc.w.b.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, 0x00240000 /* crc.w.b.w */);
    printf("%d times of independent crc.w.b.w: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, 0x01140400 /* fabs.s */);
    printf("%d times of fabs.s: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, 0x01140400 /* fabs.s */);
    printf("%d times of independent fabs.s: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, 0x01140800 /* fabs.d */);
    printf("%d times of fabs.d: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, 0x01140800 /* fabs.d */);
    printf("%d times of independent fabs.d: rdtime delta = %ld (~%ld cycles)\n", N, delta, delta * 25);

    munmap(payload, JIT_BUFFER_SIZE);

    return 0;
}
