// Vita3K substitute API wrapper for C++ code
// This provides declarations for using substitute's transform-dis from C++.
// The actual implementation is in transform-dis.c (compiled as C).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// From substitute-internal.h
typedef uint32_t uint_tptr;

// From arm/misc.h
#define ARCH_MAX_CODE_ALIGNMENT 4

// From arm/arch-dis.h — must match C struct layout exactly!
struct arch_dis_ctx {
    bool pc_low_bit;
    uint8_t it_conds[5];
    uint8_t bccrel_bits;
    void *bccrel_p;
};

static inline void arch_dis_ctx_init(struct arch_dis_ctx *ctx) {
    ctx->pc_low_bit = false;
    memset(ctx->it_conds, 0xe, 5);
    ctx->bccrel_bits = 0;
    ctx->bccrel_p = nullptr;
}

// From substitute.h error codes
#define SUBSTITUTE_OK 0
#define SUBSTITUTE_ERR_FUNC_TOO_SHORT 1
#define SUBSTITUTE_ERR_FUNC_BAD_INSN_AT_START 2
#define SUBSTITUTE_ERR_FUNC_CALLS_AT_START 3

// From transform-dis.h
#define TRANSFORM_DIS_BAN_CALLS 1
#define TD_MAX_REWRITTEN_SIZE 256
int transform_dis_main(const void *code_ptr,
                       void **rewritten_ptr_ptr,
                       uint_tptr pc_patch_start,
                       uint_tptr *pc_patch_end_p,
                       uint_tptr pc_trampoline,
                       struct arch_dis_ctx *arch_ctx_p,
                       int *offset_by_pcdiff,
                       int options);

// From arm/jump-patch.h
#define MAX_JUMP_PATCH_SIZE 8
#define MAX_EXTENDED_PATCH_SIZE (MAX_JUMP_PATCH_SIZE + 14)

static inline int jump_patch_size(uint_tptr pc,
                                  uint_tptr dpc,
                                  struct arch_dis_ctx arch,
                                  bool force) {
    (void)dpc; (void)arch; (void)force;
    return (pc & 2) ? 10 : 8;
}

// Forward-declare op16/op32 used by make_jump_patch
static inline void op16(void **codep, uint16_t op) {
    *(uint16_t *)*codep = op;
    *codep = (uint8_t *)*codep + 2;
}

static inline void op32(void **codep, uint32_t op) {
    // Match C: unaligned_w32 = memcpy of 32-bit LE value
    memcpy(*codep, &op, 4);
    *codep = (uint8_t *)*codep + 4;
}

static inline uint_tptr make_jump_pc(void *code_base, void **codep, bool thumb) {
    // Compute current emulated PC based on pointer diff — not needed for make_jump_patch
    // LDR_PC helper needs actx_pc, but we inline it here
    (void)code_base; (void)codep; (void)thumb;
    return 0;
}

static inline void make_jump_patch(void **codep, uint_tptr pc,
                                   uint_tptr dpc,
                                   struct arch_dis_ctx arch) {
    // LDR PC, [PC, #-4] + literal pool
    // If PC is not 4-aligned in Thumb mode, emit NOP first
    if (pc & 2)
        op16(codep, 0xbf00); // Thumb NOP
    if (arch.pc_low_bit)
        op32(codep, 0xf000f8df); // Thumb2: LDR.W PC, [PC]
    else
        op32(codep, 0xe51ff004); // ARM: LDR PC, [PC, #-4]
    op32(codep, (uint32_t)dpc);  // Literal: destination address
}

#ifdef __cplusplus
}
#endif
