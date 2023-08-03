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

// 2.5GHz (cycle freq) / 100MHz (stable counter freq) = 25
// minimum granularity is 25 insns -> 1 rdtime delta
// for reliable measurement of quantization of the instruction pattern in the
// face of random fluctuations, let's make the repetition times larger than
// this still
#define N 10000

// too lazy to figure out an exact number
#define JIT_BUFFER_SIZE 131072

typedef long (*payload_fn_t)(void);

enum measurement_mode {
    MODE_LINEAR_LATENCY,
    MODE_ISSUE_WIDTH,
};

enum insn_format {
    FMT_DJ,
    FMT_DJK,
    FMT_JK,
};

static void *payload;

static uint32_t insn_dj(uint32_t opcode, uint32_t d, uint32_t j)
{
    assert(d <= 31);
    assert(j <= 31);
    return (opcode & 0xfffffc00) | (j << 5) | d;
}

static uint32_t insn_djk(uint32_t opcode, uint32_t d, uint32_t j, uint32_t k)
{
    assert(d <= 31);
    assert(j <= 31);
    assert(k <= 31);
    return (opcode & 0xffff8000) | (k << 10) | (j << 5) | d;
}

static uint32_t insn_jk(uint32_t opcode, uint32_t j, uint32_t k)
{
    assert(j <= 31);
    assert(k <= 31);
    return (opcode & 0xffff801f) | (k << 10) | (j << 5);
}

static uint32_t *emit_linear_latency_seq(uint32_t *buf, int n, enum insn_format fmt, uint32_t opcode)
{
    // opcode $a1, $a1, ...
    // and hope this is some real data dependency
    uint32_t fill;
    switch (fmt) {
    case FMT_DJ:
        fill = insn_dj(opcode, 5, 5);
        break;
    case FMT_DJK:
        fill = insn_djk(opcode, 5, 5, 5);
        break;
    case FMT_JK:
        fill = insn_jk(opcode, 5, 5);
        break;
    }

    for (int i = 0; i < n; i++)
        *buf++ = fill;

    return buf;
}

static uint32_t *emit_issue_width_seq(uint32_t *buf, int n, enum insn_format fmt, uint32_t opcode)
{
    for (int i = 0; i < n; i++) {
        // opcode $X, $X, ...
        // where X loops from $a1 (r5) to $t8 (r20)
        uint32_t reg = 5 + i % 16;
        uint32_t insn;
        switch (fmt) {
        case FMT_DJ:
            insn = insn_dj(opcode, reg, reg);
            break;
        case FMT_DJK:
            insn = insn_djk(opcode, reg, reg, reg);
            break;
        case FMT_JK:
            insn = insn_jk(opcode, reg, reg);
            break;
        }
        *buf++ = insn;
    }

    return buf;
}

static void emit_n_insns(void *buf, int n, enum measurement_mode mode, enum insn_format fmt, uint32_t opcode)
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
        insns = emit_linear_latency_seq(insns, n, fmt, opcode);
        break;
    case MODE_ISSUE_WIDTH:
        insns = emit_issue_width_seq(insns, n, fmt, opcode);
        break;
    default:
        __builtin_unreachable();
    }

    *insns++ = 0x00006805;  // rdtime.d $a1, $zero
    *insns++ = 0x001190a4;  // sub.d $a0, $a1, $a0
    *insns++ = 0x4c000020;  // ret
}

static long measure_rdtime_delta(int n, enum measurement_mode mode, enum insn_format fmt, uint32_t opcode)
{
    emit_n_insns(payload, n, mode, fmt, opcode);
    __dbar(0);
    __ibar(0);
    return ((payload_fn_t)payload)();
}

static void measure_one_insn(enum insn_format fmt, uint32_t opcode, const char *mnemonic)
{
    long delta;

    delta = measure_rdtime_delta(N, MODE_LINEAR_LATENCY, fmt, opcode);
    printf("%d times of %s: rdtime delta = %ld (~%ld cycles)\n", N, mnemonic, delta, delta * 25);

    delta = measure_rdtime_delta(N, MODE_ISSUE_WIDTH, fmt, opcode);
    printf("%d times of independent %s: rdtime delta = %ld (~%ld cycles)\n", N, mnemonic, delta, delta * 25);
}


int main(int argc __unused, const char *const argv[] __unused)
{
    // RWX memory hehe
    payload = mmap(NULL, JIT_BUFFER_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);

    measure_one_insn(FMT_DJ, 0x00001000, "clo.w");
    measure_one_insn(FMT_DJ, 0x729c0800, "vclo.w");
    measure_one_insn(FMT_DJ, 0x769c0800, "xvclo.w");
    measure_one_insn(FMT_DJK, 0x001c0000, "mul.w");
    measure_one_insn(FMT_DJK, 0x00240000, "crc.w.b.w");
    measure_one_insn(FMT_DJ, 0x01140400, "fabs.s");
    measure_one_insn(FMT_DJ, 0x01140800, "fabs.d");
    measure_one_insn(FMT_DJK, 0x001a0000, "rotr.b");
    measure_one_insn(FMT_DJK, 0x001a8000, "rotr.h");
    measure_one_insn(FMT_DJK, 0x001b0000, "rotr.w");
    measure_one_insn(FMT_DJK, 0x001b8000, "rotr.d");
    measure_one_insn(FMT_DJK, 0x70ee0000, "vrotr.b");
    measure_one_insn(FMT_DJK, 0x70ee8000, "vrotr.h");
    measure_one_insn(FMT_DJK, 0x70ef0000, "vrotr.w");
    measure_one_insn(FMT_DJK, 0x70ef8000, "vrotr.d");
    measure_one_insn(FMT_DJK, 0x74ee0000, "xvrotr.b");
    measure_one_insn(FMT_DJK, 0x74ee8000, "xvrotr.h");
    measure_one_insn(FMT_DJK, 0x74ef0000, "xvrotr.w");
    measure_one_insn(FMT_DJK, 0x74ef8000, "xvrotr.d");
    measure_one_insn(FMT_JK, 0x003f8000, "x86rotr.b");
    measure_one_insn(FMT_JK, 0x003f8001, "x86rotr.h");
    measure_one_insn(FMT_JK, 0x003f8002, "x86rotr.w");
    measure_one_insn(FMT_JK, 0x003f8003, "x86rotr.d");

    munmap(payload, JIT_BUFFER_SIZE);

    return 0;
}
