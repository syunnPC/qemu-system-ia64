/*
 * IA-64 Local SAPIC and interval timer architecture operations.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/host-utils.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "arch/arch.h"
#include "trace.h"

/* ---- Local SAPIC helpers ---- */

static int sapic_find_isr(CPUIA64State *env);
static void ia64_sapic_set_special_work(CPUState *cs,
                                        run_on_cpu_data data);

CPUState *ia64_cpu_by_sapic_id(uint8_t id, uint8_t eid)
{
    CPUState *cs;
    uint64_t lid = ia64_sapic_lid(id, eid);

    CPU_FOREACH(cs) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

        if ((qatomic_read(&cpu->env.cr[IA64_CR_SAPIC_LID]) &
             (IA64_SAPIC_LID_ID_MASK | IA64_SAPIC_LID_EID_MASK)) == lid) {
            return cs;
        }
    }
    return NULL;
}

void ia64_sapic_set_xtp(CPUState *cs, uint8_t xtp)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    qatomic_set(&cpu->env.interrupt.sapic_xtp,
                xtp & IA64_SAPIC_XTP_WRITABLE_MASK);
}

uint8_t ia64_sapic_get_xtp(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    return qatomic_read(&cpu->env.interrupt.sapic_xtp);
}

static CPUState *sapic_redirect_target(CPUState *requested)
{
    CPUState *cs;
    CPUState *selected = NULL;
    IA64CPU *requested_cpu = ia64_cpu_from_cpu_state(requested);
    uint64_t requested_eid =
        qatomic_read(&requested_cpu->env.cr[IA64_CR_SAPIC_LID]) &
        IA64_SAPIC_LID_EID_MASK;
    unsigned int requested_index = MAX(requested->cpu_index, 0);
    unsigned int max_index = requested_index;
    unsigned int span;
    unsigned int best_priority = UINT_MAX;
    unsigned int best_distance = UINT_MAX;

    CPU_FOREACH(cs) {
        max_index = MAX(max_index, (unsigned int)MAX(cs->cpu_index, 0));
    }
    span = max_index + 1;

    CPU_FOREACH(cs) {
        IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
        uint8_t xtp = ia64_sapic_get_xtp(cs);
        unsigned int priority;
        unsigned int index;
        unsigned int distance;

        if ((qatomic_read(&cpu->env.cr[IA64_CR_SAPIC_LID]) &
             IA64_SAPIC_LID_EID_MASK) != requested_eid ||
            (xtp & IA64_SAPIC_XTP_DISABLE)) {
            continue;
        }
        priority = xtp & IA64_SAPIC_XTP_PRIORITY_MASK;
        index = MAX(cs->cpu_index, 0);
        distance = (index + span - requested_index) % span;
        if (priority < best_priority ||
            (priority == best_priority && distance < best_distance)) {
            selected = cs;
            best_priority = priority;
            best_distance = distance;
        }
    }

    return selected != NULL ? selected : requested;
}

static void sapic_deliver_to(CPUState *cs, IA64SapicDeliveryMode delivery,
                             uint8_t vector)
{
    if (delivery == IA64_SAPIC_DELIVERY_PMI ||
        delivery == IA64_SAPIC_DELIVERY_INIT) {
        run_on_cpu_data data = RUN_ON_CPU_HOST_INT(
            ((unsigned int)delivery << 8) | vector);

        if (qemu_cpu_is_self(cs)) {
            ia64_sapic_set_special_work(cs, data);
        } else {
            async_run_on_cpu(cs, ia64_sapic_set_special_work, data);
        }
    } else {
        ia64_sapic_set_irq(cs, vector);
    }
}

bool ia64_sapic_deliver(IA64SapicDestinationMode destination_mode,
                        uint8_t id, uint8_t eid, bool redirect,
                        IA64SapicDeliveryMode delivery, uint8_t vector)
{
    CPUState *cs;

    /* SAPIC interrupt transactions use physical ID/EID addressing. */
    if (destination_mode != IA64_SAPIC_DESTINATION_PHYSICAL) {
        return false;
    }

    switch (delivery) {
    case IA64_SAPIC_DELIVERY_INT_REDIRECT:
        redirect = true;
        /* fall through */
    case IA64_SAPIC_DELIVERY_INT:
        if (!ia64_external_interrupt_vector_valid(vector)) {
            return false;
        }
        break;
    case IA64_SAPIC_DELIVERY_NMI:
        vector = 2;
        break;
    case IA64_SAPIC_DELIVERY_EXTINT:
        vector = 0;
        break;
    case IA64_SAPIC_DELIVERY_PMI:
        if (vector >= 4) {
            return false;
        }
        break;
    case IA64_SAPIC_DELIVERY_INIT:
        vector = 0;
        break;
    default:
        return false;
    }

    cs = ia64_cpu_by_sapic_id(id, eid);
    if (cs == NULL) {
        return false;
    }
    if (redirect && delivery != IA64_SAPIC_DELIVERY_NMI &&
        delivery != IA64_SAPIC_DELIVERY_EXTINT) {
        cs = sapic_redirect_target(cs);
        sapic_deliver_to(cs, delivery, vector);
        return true;
    }

    sapic_deliver_to(cs, delivery, vector);
    return true;
}

static bool sapic_vector_active(const uint64_t bitmap[4], int vector)
{
    return bitmap[vector / 64] & (1ULL << (vector % 64));
}

static int sapic_vector_priority(int vector)
{
    if (vector == 2) {
        return 257;
    }
    if (vector == 0) {
        return 256;
    }
    if (vector >= 16) {
        return vector;
    }
    return -1;
}

static int sapic_vector_priority_class(int vector)
{
    return vector >= 16 ? vector >> 4 : -1;
}

static bool sapic_vector_unmasked(CPUIA64State *env, int vector)
{
    uint64_t tpr = env->cr[IA64_CR_SAPIC_TPR];
    int isr = sapic_find_isr(env);
    int vector_priority = sapic_vector_priority(vector);
    int vector_class = sapic_vector_priority_class(vector);

    if (vector_priority <= sapic_vector_priority(isr)) {
        return false;
    }

    if (vector == 2) {
        return true;
    }
    if (vector == 0) {
        return !(tpr & IA64_TPR_MMI);
    }
    return !(tpr & IA64_TPR_MMI) &&
           ((uint64_t)vector_class > ((tpr & IA64_TPR_MIC_MASK) >> 4));
}

static int sapic_find_highest_normal(const uint64_t bitmap[4])
{
    int word;

    for (word = 3; word >= 0; word--) {
        uint64_t active = bitmap[word];

        if (word == 0) {
            active &= ~0xffffULL;
        }
        if (active) {
            return word * 64 + 63 - clz64(active);
        }
    }
    return IA64_SPURIOUS_VECTOR;
}

static int sapic_find_irr(CPUIA64State *env)
{
    int vector;

    if (sapic_vector_active(env->interrupt.sapic_irr, 2) &&
        sapic_vector_unmasked(env, 2)) {
        return 2;
    }
    if (sapic_vector_active(env->interrupt.sapic_irr, 0) &&
        sapic_vector_unmasked(env, 0)) {
        return 0;
    }
    vector = sapic_find_highest_normal(env->interrupt.sapic_irr);
    if (vector != IA64_SPURIOUS_VECTOR &&
        sapic_vector_unmasked(env, vector)) {
        return vector;
    }
    return IA64_SPURIOUS_VECTOR;
}

static int sapic_find_isr(CPUIA64State *env)
{
    if (sapic_vector_active(env->interrupt.sapic_isr, 2)) {
        return 2;
    }
    if (sapic_vector_active(env->interrupt.sapic_isr, 0)) {
        return 0;
    }
    return sapic_find_highest_normal(env->interrupt.sapic_isr);
}

void ia64_sapic_update_interrupt(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    bool special = (env->pal.pal_mca_pending &&
                    !(env->psr & IA64_PSR_MC)) ||
                   (env->interrupt.sapic_init_pending &&
                    !(env->psr & IA64_PSR_MC)) ||
                   (env->interrupt.sapic_pmi_pending &&
                    (env->psr & IA64_PSR_IC) &&
                    env->pal.pal_pmi_entry != 0);

    if (special || sapic_find_irr(env) != IA64_SPURIOUS_VECTOR) {
        cpu_set_interrupt(cs, CPU_INTERRUPT_HARD);
        qemu_cpu_kick(cs);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

bool ia64_sapic_has_pending(CPUIA64State *env)
{
    int vector = sapic_find_irr(env);
    return vector != IA64_SPURIOUS_VECTOR;
}

bool ia64_sapic_has_pmi(CPUIA64State *env)
{
    return env->interrupt.sapic_pmi_pending != 0;
}

bool ia64_sapic_has_init(CPUIA64State *env)
{
    return env->interrupt.sapic_init_pending;
}

int ia64_sapic_accept_pmi(CPUIA64State *env)
{
    uint16_t pending = env->interrupt.sapic_pmi_pending;
    int vector;

    if (!pending) {
        return -1;
    }
    vector = 31 - clz32(pending);
    env->interrupt.sapic_pmi_pending &= ~(1U << vector);
    trace_ia64_sapic_vector(env_cpu(env)->cpu_index, "accept-pmi", vector);
    ia64_sapic_update_interrupt(env);
    return vector;
}

bool ia64_sapic_accept_init(CPUIA64State *env)
{
    if (!env->interrupt.sapic_init_pending) {
        return false;
    }
    env->interrupt.sapic_init_pending = false;
    env->interrupt.sapic_init_reason = 0;
    trace_ia64_sapic_vector(env_cpu(env)->cpu_index, "accept-init", 0);
    ia64_sapic_update_interrupt(env);
    return true;
}

static int ia64_itv_vector(CPUIA64State *env)
{
    uint8_t vector = env->cr[IA64_CR_ITV] & 0xFF;

    return ia64_external_interrupt_vector_valid(vector) ? vector : -1;
}

static bool ia64_itv_masked(CPUIA64State *env)
{
    return env->cr[IA64_CR_ITV] & IA64_VECTOR_MASKED;
}

static bool ia64_itm_interrupt_active(CPUIA64State *env)
{
    int vector = ia64_itv_vector(env);

    return vector >= 0 && sapic_vector_active(env->interrupt.sapic_isr, vector);
}

static uint64_t ia64_itc_ticks_for_ns(uint64_t elapsed_ns,
                                      uint32_t frequency_hz,
                                      uint32_t initial_fraction,
                                      uint32_t *final_fraction)
{
    uint64_t seconds;
    uint64_t nanoseconds;
    uint64_t fraction;
    uint64_t ticks;

    /*
     * ITC is synchronized frequently by guest reads.  For every interval
     * shorter than one second the product is bounded by 1.6e18 on all
     * supported models, so convert it directly and avoid first dividing the
     * elapsed time into seconds and nanoseconds.
     */
    if (likely(elapsed_ns < NANOSECONDS_PER_SECOND)) {
        fraction = elapsed_ns * frequency_hz + initial_fraction;
        ticks = fraction / NANOSECONDS_PER_SECOND;
        if (final_fraction != NULL) {
            *final_fraction = fraction % NANOSECONDS_PER_SECOND;
        }
        return ticks;
    }

    seconds = elapsed_ns / NANOSECONDS_PER_SECOND;
    nanoseconds = elapsed_ns % NANOSECONDS_PER_SECOND;

    fraction = nanoseconds * frequency_hz + initial_fraction;
    ticks = seconds * frequency_hz;

    ticks += fraction / NANOSECONDS_PER_SECOND;
    if (final_fraction != NULL) {
        *final_fraction = fraction % NANOSECONDS_PER_SECOND;
    }
    return ticks;
}

static uint64_t ia64_itc_delay_ns(CPUIA64State *env, uint64_t delta_ticks)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t required_ticks;
    uint64_t maximum_ticks;
    uint64_t delay_ns;

    if (delta_ticks > UINT64_MAX - env->interrupt.itc_tick_debt) {
        return INT64_MAX;
    }
    required_ticks = delta_ticks + env->interrupt.itc_tick_debt;
    maximum_ticks = ia64_itc_ticks_for_ns(INT64_MAX,
                                          icc->itc_frequency_hz,
                                          env->interrupt.itc_fraction,
                                          NULL);
    if (required_ticks > maximum_ticks) {
        return INT64_MAX;
    }

    delay_ns = muldiv64(required_ticks, NANOSECONDS_PER_SECOND,
                        icc->itc_frequency_hz);
    /*
     * muldiv64() rounds down.  Host timers have nanosecond resolution, so
     * choose the first representable instant at which the model oscillator
     * has produced every required tick.  This also handles rates above
     * 1 GHz, where several guest ticks can elapse in one host nanosecond.
     */
    if (ia64_itc_ticks_for_ns(delay_ns, icc->itc_frequency_hz,
                             env->interrupt.itc_fraction, NULL) <
        required_ticks) {
        delay_ns++;
    }
    return delay_ns;
}

void ia64_itc_advance_pending_itm(CPUIA64State *env)
{
    ia64_itc_sync(env);

    /*
     * Once the timer interrupt is in service, the guest has not armed the next
     * ITM yet.  Expose at least the first tick after the matched deadline so a
     * handler cannot observe the equality as still pending.
     */
    if (!env->interrupt.itm_armed &&
        env->interrupt.itm_last_match_valid &&
        ia64_itm_interrupt_active(env) &&
        (int64_t)(env->ar_itc - (env->interrupt.itm_last_match + 1)) < 0) {
        uint64_t ticks = env->interrupt.itm_last_match + 1 - env->ar_itc;

        env->ar_itc += ticks;
        /*
         * This post-match advancement is an emulation artifact needed when
         * the host timer lands exactly on ITM.  Record it as debt instead of
         * changing the oscillator phase: later physical ticks repay it, so
         * repeated handler reads do not make the model clock run fast.
         */
        env->interrupt.itc_tick_debt =
            ticks > UINT64_MAX - env->interrupt.itc_tick_debt ?
            UINT64_MAX : env->interrupt.itc_tick_debt + ticks;
    }
}

int ia64_sapic_accept(CPUIA64State *env)
{
    int vector = sapic_find_irr(env);
    if (vector != IA64_SPURIOUS_VECTOR) {
        int idx = vector / 64;
        int bit = vector % 64;
        env->interrupt.sapic_irr[idx] &= ~(1ULL << bit);
        env->interrupt.sapic_isr[idx] |= (1ULL << bit);
        trace_ia64_sapic_vector(env_cpu(env)->cpu_index, "accept", vector);
        ia64_sapic_update_interrupt(env);
    }
    return vector;
}

void ia64_sapic_eoi(CPUIA64State *env)
{
    int vector = sapic_find_isr(env);
    if (vector != IA64_SPURIOUS_VECTOR) {
        int idx = vector / 64;
        int bit = vector % 64;
        env->interrupt.sapic_isr[idx] &= ~(1ULL << bit);
        trace_ia64_sapic_vector(env_cpu(env)->cpu_index, "eoi", vector);
    }
    ia64_sapic_update_interrupt(env);
}

int ia64_sapic_get_ivr(CPUIA64State *env)
{
    return ia64_sapic_accept(env);
}

static void ia64_sapic_set_irq_work(CPUState *cs, run_on_cpu_data data)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    uint8_t vector = data.host_int;
    int idx = vector / 64;
    int bit = vector % 64;

    cpu->env.interrupt.sapic_irr[idx] |= (1ULL << bit);
    trace_ia64_sapic_vector(cs->cpu_index, "raise", vector);
    ia64_sapic_update_interrupt(&cpu->env);
}

void ia64_sapic_set_irq(CPUState *cs, uint8_t vector)
{
    run_on_cpu_data data = RUN_ON_CPU_HOST_INT(vector);

    if (qemu_cpu_is_self(cs)) {
        ia64_sapic_set_irq_work(cs, data);
    } else {
        async_run_on_cpu(cs, ia64_sapic_set_irq_work, data);
    }
}

void ia64_sapic_set_init(CPUState *cs, uint8_t reason)
{
    run_on_cpu_data data = RUN_ON_CPU_HOST_INT(
        ((unsigned int)IA64_SAPIC_DELIVERY_INIT << 8) | reason);

    if (qemu_cpu_is_self(cs)) {
        ia64_sapic_set_special_work(cs, data);
    } else {
        async_run_on_cpu(cs, ia64_sapic_set_special_work, data);
    }
}

static void ia64_sapic_set_special_work(CPUState *cs,
                                        run_on_cpu_data data)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    unsigned int encoded = data.host_int;
    IA64SapicDeliveryMode delivery = encoded >> 8;
    uint8_t vector = encoded;

    if (delivery == IA64_SAPIC_DELIVERY_PMI) {
        cpu->env.interrupt.sapic_pmi_pending |= 1U << vector;
        trace_ia64_sapic_vector(cs->cpu_index, "raise-pmi", vector);
    } else {
        g_assert(delivery == IA64_SAPIC_DELIVERY_INIT);
        if (!cpu->env.interrupt.sapic_init_pending ||
            vector > cpu->env.interrupt.sapic_init_reason) {
            cpu->env.interrupt.sapic_init_reason = vector;
        }
        cpu->env.interrupt.sapic_init_pending = true;
        trace_ia64_sapic_vector(cs->cpu_index, "raise-init", 0);
    }
    ia64_sapic_update_interrupt(&cpu->env);
}

static void ia64_itm_raise(CPUIA64State *env, uint64_t itm_value)
{
    IA64CPU *cpu = container_of(env, IA64CPU, env);
    int vector = ia64_itv_vector(env);

    if (vector < 0) {
        return;
    }

    if (env->interrupt.itm_last_match_valid &&
        env->interrupt.itm_last_match == itm_value) {
        ia64_sapic_update_interrupt(env);
        return;
    }

    env->interrupt.itm_last_match = itm_value;
    env->interrupt.itm_last_match_valid = true;
    trace_ia64_itm(CPU(cpu)->cpu_index, "fire", env->ar_itc, itm_value,
                   vector);
    ia64_sapic_set_irq(CPU(cpu), vector);
}

static bool ia64_itm_update_pending(CPUIA64State *env, uint64_t itc,
                                    uint64_t itm_value, bool was_armed)
{
    int64_t delta_ticks = (int64_t)(itm_value - itc);
    int vector = ia64_itv_vector(env);

    if (vector < 0) {
        return true;
    }

    if (ia64_itv_masked(env)) {
        return true;
    }

    if (delta_ticks > 0) {
        return false;
    }

    /*
     * The architecture raises an interval timer interrupt when ITC equals
     * ITM.  A freshly programmed value already behind the current ITC has
     * missed that equality and must not synthesize an interrupt.  If this
     * value was armed while it was still in the future, a late QEMU timer
     * callback still represents the equality that elapsed in virtual time.
     */
    if (delta_ticks == 0 || was_armed) {
        ia64_itm_raise(env, itm_value);
    }
    return true;
}

static void ia64_itm_timer_work(CPUState *cs, run_on_cpu_data data)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    uint64_t itm;
    bool was_armed;

    (void)data;
    if (!cpu->env.interrupt.itm_armed) {
        return;
    }
    was_armed = cpu->env.interrupt.itm_armed;
    cpu->env.interrupt.itm_armed = false;
    itm = cpu->env.cr[IA64_CR_ITM];

    ia64_itc_advance_pending_itm(&cpu->env);

    if (!ia64_itm_update_pending(&cpu->env, cpu->env.ar_itc, itm,
                                 was_armed)) {
        ia64_itm_update(&cpu->env, itm);
    }
}

void ia64_itm_timer_cb(void *opaque)
{
    CPUState *cs = CPU(opaque);

    if (qemu_cpu_is_self(cs)) {
        ia64_itm_timer_work(cs, RUN_ON_CPU_NULL);
    } else {
        async_run_on_cpu(cs, ia64_itm_timer_work, RUN_ON_CPU_NULL);
    }
}

void ia64_itm_update(CPUIA64State *env, uint64_t itm_value)
{
    IA64CPU *cpu = container_of(env, IA64CPU, env);
    uint64_t itc;
    int64_t delta_ticks;
    uint64_t delay_ns;
    int64_t deadline_ns;
    bool was_armed = env->interrupt.itm_armed &&
                     env->interrupt.itm_armed_value == itm_value;

    ia64_itc_advance_pending_itm(env);
    itc = env->ar_itc;
    delta_ticks = (int64_t)(itm_value - itc);

    if (ia64_itm_update_pending(env, itc, itm_value, was_armed)) {
        timer_del(cpu->itm_timer);
        env->interrupt.itm_armed = false;
        ia64_itc_advance_pending_itm(env);
        return;
    }

    delay_ns = ia64_itc_delay_ns(env, delta_ticks);
    env->interrupt.itm_armed = true;
    env->interrupt.itm_armed_value = itm_value;
    if (delay_ns == INT64_MAX ||
        env->interrupt.itc_last_ns > INT64_MAX - (int64_t)delay_ns) {
        deadline_ns = INT64_MAX;
    } else {
        deadline_ns = env->interrupt.itc_last_ns + (int64_t)delay_ns;
    }
    trace_ia64_itm(CPU(cpu)->cpu_index, "arm", itc, itm_value,
                   ia64_itv_vector(env));
    timer_mod(cpu->itm_timer, deadline_ns);
}

void ia64_itc_sync(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    int64_t now = ia64_itc_clock_ns();
    int64_t elapsed = now - env->interrupt.itc_last_ns;

    if (elapsed > 0) {
        uint64_t ticks = ia64_itc_ticks_for_ns(
            elapsed, icc->itc_frequency_hz, env->interrupt.itc_fraction,
            &env->interrupt.itc_fraction);

        env->interrupt.itc_last_ns = now;
        if (ticks <= env->interrupt.itc_tick_debt) {
            env->interrupt.itc_tick_debt -= ticks;
        } else {
            ticks -= env->interrupt.itc_tick_debt;
            env->interrupt.itc_tick_debt = 0;
            env->ar_itc += ticks;
        }
    }
}

void ia64_itc_check_timer(CPUIA64State *env)
{
    bool was_armed;

    ia64_itc_advance_pending_itm(env);
    was_armed = env->interrupt.itm_armed &&
                env->interrupt.itm_armed_value == env->cr_itm;

    if (was_armed && (int64_t)(env->cr_itm - env->ar_itc) <= 0) {
        env->interrupt.itm_armed = false;
    }
    ia64_itm_update_pending(env, env->ar_itc, env->cr_itm, was_armed);
    ia64_itc_advance_pending_itm(env);
}

void ia64_itc_enter_halt(CPUIA64State *env)
{
    ia64_itc_advance_pending_itm(env);
    ia64_itm_update(env, env->cr_itm);
}

bool ia64_cpu_has_work(CPUState *cs)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    bool nmi_pending = (env->interrupt.sapic_irr[0] & (1ULL << 2)) != 0;
    bool interrupts_enabled = (env->psr & IA64_PSR_I) ||
                              (cs->halted && env->interrupt.pal_halt_wake);

    /*
     * ia64_sapic_update_interrupt() maintains CPU_INTERRUPT_HARD whenever
     * IRR, ISR or TPR changes, and the ITM callback does the same when its
     * deadline expires.  Do not rescan IRR or reschedule the timer from this
     * exec-loop hot path.  PAL_HALT_LIGHT wakes only for an interrupt that
     * is actually deliverable; PSR.i does not mask NMI vector 2.
     */
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD) &&
           (interrupts_enabled || nmi_pending ||
            (env->pal.pal_mca_pending && !(env->psr & IA64_PSR_MC)) ||
            (env->interrupt.sapic_pmi_pending &&
             (env->psr & IA64_PSR_IC) &&
             env->pal.pal_pmi_entry != 0) ||
            (env->interrupt.sapic_init_pending &&
             !(env->psr & IA64_PSR_MC)));
}


uint64_t ia64_interrupt_itc_read(CPUIA64State *env)
{
    return ia64_itc_read(env);
}
