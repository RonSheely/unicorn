/* Unicorn Emulator Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2015 */
/* Modified for Unicorn Engine by Chen Huitao<chenhuitao@hfmrit.com>, 2020 */

#include "sysemu/cpus.h"
#include "cpu.h"
#include "unicorn_common.h"
#include "uc_priv.h"
#include "unicorn.h"

M68kCPU *cpu_m68k_init(struct uc_struct *uc);

static void m68k_set_pc(struct uc_struct *uc, uint64_t address)
{
    ((CPUM68KState *)uc->cpu->env_ptr)->pc = address;
}

static uint64_t m68k_get_pc(struct uc_struct *uc)
{
    return ((CPUM68KState *)uc->cpu->env_ptr)->pc;
}

static void m68k_release(void *ctx)
{
    int i;
    TCGContext *tcg_ctx = (TCGContext *)ctx;
    M68kCPU *cpu = (M68kCPU *)tcg_ctx->uc->cpu;
    CPUTLBDesc *d = cpu->neg.tlb.d;
    CPUTLBDescFast *f = cpu->neg.tlb.f;
    CPUTLBDesc *desc;
    CPUTLBDescFast *fast;

    release_common(ctx);
    for (i = 0; i < NB_MMU_MODES; i++) {
        desc = &(d[i]);
        fast = &(f[i]);
        g_free(desc->iotlb);
        g_free(fast->table);
    }
}

void m68k_reg_reset(struct uc_struct *uc)
{
    CPUArchState *env = uc->cpu->env_ptr;

    memset(env->aregs, 0, sizeof(env->aregs));
    memset(env->dregs, 0, sizeof(env->dregs));

    env->pc = 0;
}

static uc_err reg_read(CPUM68KState *env, unsigned int regid, void *value,
                       size_t *size)
{
    uc_err ret = UC_ERR_ARG;

    if (regid >= UC_M68K_REG_A0 && regid <= UC_M68K_REG_A7) {
        CHECK_REG_TYPE(uint32_t);
        *(uint32_t *)value = env->aregs[regid - UC_M68K_REG_A0];
    } else if (regid >= UC_M68K_REG_D0 && regid <= UC_M68K_REG_D7) {
        CHECK_REG_TYPE(uint32_t);
        *(uint32_t *)value = env->dregs[regid - UC_M68K_REG_D0];
    } else {
        switch (regid) {
        default:
            break;
        case UC_M68K_REG_PC:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->pc;
            break;
        case UC_M68K_REG_SR:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->sr;
            break;
        }
    }

    return ret;
}

static uc_err reg_write(CPUM68KState *env, unsigned int regid,
                        const void *value, size_t *size, int *setpc)
{
    uc_err ret = UC_ERR_ARG;

    if (regid >= UC_M68K_REG_A0 && regid <= UC_M68K_REG_A7) {
        CHECK_REG_TYPE(uint32_t);
        env->aregs[regid - UC_M68K_REG_A0] = *(uint32_t *)value;
    } else if (regid >= UC_M68K_REG_D0 && regid <= UC_M68K_REG_D7) {
        CHECK_REG_TYPE(uint32_t);
        env->dregs[regid - UC_M68K_REG_D0] = *(uint32_t *)value;
    } else {
        switch (regid) {
        default:
            break;
        case UC_M68K_REG_PC:
            CHECK_REG_TYPE(uint32_t);
            env->pc = *(uint32_t *)value;
            *setpc = 1;
            break;
        case UC_M68K_REG_SR:
            CHECK_REG_TYPE(uint32_t);
            cpu_m68k_set_sr(env, *(uint32_t *)value);
            break;
        }
    }

    return ret;
}

static uc_err reg_read_batch(CPUM68KState *env, unsigned int *regs,
                             void *const *vals, size_t *sizes, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        unsigned int regid = regs[i];
        void *value = vals[i];
        uc_err err = reg_read(env, regid, value, sizes ? sizes + i : NULL);
        if (err) {
            return err;
        }
    }

    return UC_ERR_OK;
}

static uc_err reg_write_batch(CPUM68KState *env, unsigned int *regs,
                              const void *const *vals, size_t *sizes, int count,
                              int *setpc)
{
    int i;

    for (i = 0; i < count; i++) {
        unsigned int regid = regs[i];
        const void *value = vals[i];
        uc_err err =
            reg_write(env, regid, value, sizes ? sizes + i : NULL, setpc);
        if (err) {
            return err;
        }
    }

    return UC_ERR_OK;
}

int m68k_reg_read(struct uc_struct *uc, unsigned int *regs, void *const *vals,
                  size_t *sizes, int count)
{
    CPUM68KState *env = &(M68K_CPU(uc->cpu)->env);
    return reg_read_batch(env, regs, vals, sizes, count);
}

int m68k_reg_write(struct uc_struct *uc, unsigned int *regs,
                   const void *const *vals, size_t *sizes, int count)
{
    CPUM68KState *env = &(M68K_CPU(uc->cpu)->env);
    int setpc = 0;
    uc_err err = reg_write_batch(env, regs, vals, sizes, count, &setpc);
    if (err) {
        return err;
    }
    if (setpc) {
        // force to quit execution and flush TB
        uc->quit_request = true;
        break_translation_loop(uc);
    }

    return UC_ERR_OK;
}

DEFAULT_VISIBILITY
int m68k_context_reg_read(struct uc_context *ctx, unsigned int *regs,
                          void *const *vals, size_t *sizes, int count)
{
    CPUM68KState *env = (CPUM68KState *)ctx->data;
    return reg_read_batch(env, regs, vals, sizes, count);
}

DEFAULT_VISIBILITY
int m68k_context_reg_write(struct uc_context *ctx, unsigned int *regs,
                           const void *const *vals, size_t *sizes, int count)
{
    CPUM68KState *env = (CPUM68KState *)ctx->data;
    int setpc = 0;
    return reg_write_batch(env, regs, vals, sizes, count, &setpc);
}

static int m68k_cpus_init(struct uc_struct *uc, const char *cpu_model)
{
    M68kCPU *cpu;

    cpu = cpu_m68k_init(uc);
    if (cpu == NULL) {
        return -1;
    }
    return 0;
}

DEFAULT_VISIBILITY
void m68k_uc_init(struct uc_struct *uc)
{
    uc->release = m68k_release;
    uc->reg_read = m68k_reg_read;
    uc->reg_write = m68k_reg_write;
    uc->reg_reset = m68k_reg_reset;
    uc->set_pc = m68k_set_pc;
    uc->get_pc = m68k_get_pc;
    uc->cpus_init = m68k_cpus_init;
    uc->cpu_context_size = offsetof(CPUM68KState, end_reset_fields);
    uc_common_init(uc);
}
