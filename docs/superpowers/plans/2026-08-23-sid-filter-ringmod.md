# Resonant Filter and Ring Modulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared, fixed-point resonant filter (combinable low/band/high-pass, per-voice routing) and assignable ring modulation (triangle waveform only) to the existing 8-voice synthesizer, controllable via 6 new Forth words.

**Architecture:** `kernel/audio/synth.c`/`.h` gain a Chamberlin state-variable filter core (a pure, parameterized function, independently host-testable) plus two new per-voice fields (`ring_partner`, `filter_route`). `synth_render_half()` (already existing, extended here) splits its per-voice sum into filtered/bypass accumulators and runs the filtered portion through the one shared filter instance each sample. `synth_osc_sample()`'s triangle case gains an XOR-based ring-mod rewrite that is behaviorally identical to today's code when ring mod is inactive. Six new bare Forth words extend the existing control surface, wired exactly like `WAVE`/`DUTY`/`ONA`/`GATE-ON`/`GATE-OFF`.

**Tech Stack:** C (i686-elf-gcc cross-compiler), fixed-point (Q14) DSP, this kernel's existing synth engine / Forth VM infrastructure.

**Spec:** `docs/superpowers/specs/2026-08-23-sid-filter-ringmod-design.md`

## Global Constraints

- No floating point anywhere (`kernel/Makefile`'s `CFLAGS` carries `-mgeneral-regs-only`) — filter coefficients come from precomputed tables, not runtime `sin()`.
- One shared filter (not one per voice), matching the real SID's architecture scaled to 8 voices.
- Cutoff: 0-255 (8-bit, not SID's native 11-bit — a practically-sized precomputed table). Resonance: 0-15 (matches SID's 4-bit register). Filter mode: 0-7 bitmask (bit0=LP, bit1=BP, bit2=HP), any combination summable.
- Ring modulation only affects `WAVE_TRIANGLE`; has zero effect on pulse/saw/noise, matching real SID exactly.
- Ring-mod partner is per-voice assignable (any of the 8 voices), not the real chip's fixed 3-voice ring topology.
- The filter's internal recursive state (`lp`/`bp`) must be clamped every sample — an aggressive cutoff+resonance combination can otherwise grow it without bound and corrupt the whole mixed output, not just distort.
- All setters clamp or no-op on out-of-range input, following the existing convention (`DUTY`-style clamp for continuous parameters; `VOICE`/`WAVE`/`ONA`-style hard reject for discrete indices with a meaningless out-of-range value).

---

## Task 1: Filter core and coefficient tables

**Files:**
- Modify: `kernel/audio/synth.h`
- Modify: `kernel/audio/synth.c`
- Modify: `kernel/tests/test_synth.c`

**Interfaces:**
- Produces: `SYNTH_FILTER_MODE_LP`/`_BP`/`_HP` (1/2/4), `SYNTH_FILTER_STATE_MAX` (65536), `struct synth_filter_state { int lp; int bp; }`, `extern const int synth_filter_f_coeff[256]`, `extern const int synth_filter_q_coeff[16]`, `extern int synth_filter_cutoff_index`, `extern int synth_filter_res_index`, `extern int synth_filter_mode_mask`, `void synth_set_filter_cutoff(int cutoff)`, `void synth_set_filter_resonance(int resonance)`, `void synth_set_filter_mode(int mode_mask)`, `int synth_filter_process_sample(struct synth_filter_state *st, int input, int f_coeff, int q_coeff, int mode_mask)`. **Task 3 calls `synth_filter_process_sample()` directly from `synth_render_half()` and reads the three `synth_filter_*_index`/`_mask` globals via the coefficient tables.**

- [ ] **Step 1: Modify `kernel/audio/synth.h`**

Replace:
```c
int synth_envelope_advance_sample(struct synth_voice *v);

/* Renders len bytes of 8-bit unsigned PCM (128 = silence) into buf,
```
with:
```c
int synth_envelope_advance_sample(struct synth_voice *v);

#define SYNTH_FILTER_MODE_LP 1
#define SYNTH_FILTER_MODE_BP 2
#define SYNTH_FILTER_MODE_HP 4
#define SYNTH_FILTER_STATE_MAX 65536

struct synth_filter_state {
    int lp;
    int bp;
};

extern const int synth_filter_f_coeff[256];
extern const int synth_filter_q_coeff[16];

/* Current global filter settings -- set by synth_set_filter_cutoff()/
 * _resonance()/_mode() below, read by synth_render_half() (via the
 * f_coeff/q_coeff table lookups) once the filter is wired into the
 * mixer. Exposed here (not static in synth.c) the same way
 * synth_voices[] already is, so host tests can verify each setter's
 * clamping directly. */
extern int synth_filter_cutoff_index;
extern int synth_filter_res_index;
extern int synth_filter_mode_mask;

void synth_set_filter_cutoff(int cutoff);
void synth_set_filter_resonance(int resonance);
void synth_set_filter_mode(int mode_mask);

/* One Chamberlin state-variable filter step: updates *st in place and
 * returns the sum of whichever of low/band/high-pass outputs mode_mask
 * selects (bits: SYNTH_FILTER_MODE_LP/BP/HP, any combination). f_coeff/
 * q_coeff are Q14 fixed-point (looked up from synth_filter_f_coeff[]/
 * synth_filter_q_coeff[] by the caller). Clamps its own internal state
 * to +-SYNTH_FILTER_STATE_MAX -- an aggressive cutoff+resonance
 * combination could otherwise grow this recursive state without bound
 * and eventually corrupt the whole mixed output, not just distort. */
int synth_filter_process_sample(struct synth_filter_state *st, int input,
                                 int f_coeff, int q_coeff, int mode_mask);

/* Renders len bytes of 8-bit unsigned PCM (128 = silence) into buf,
```

- [ ] **Step 2: Append to `kernel/audio/synth.c`** (end of file)

```c

/* Cutoff-to-frequency-coefficient table: Q14 fixed-point f = 2*sin(pi*fc/fs)
 * for fc log-spaced 20Hz-3000Hz (index 0-255), fs = SYNTH_SAMPLE_RATE.
 * The 3000Hz upper bound is deliberately conservative -- comfortably
 * under fs/6 (~3675Hz), the range where this simple (non-oversampled)
 * state-variable topology stays numerically well-behaved. No floating
 * point exists in this kernel, so this table (like ona_phase_increment[])
 * is generated once on the host and committed as a literal -- exact
 * Python: `round(2.0 * math.sin(math.pi * (20.0 * (3000.0/20.0) **
 * (i/255.0)) / 22050.0) * 16384)` for i in 0..255. */
const int synth_filter_f_coeff[256] = {
    93, 95, 97, 99, 101, 103, 105, 107,
    109, 111, 114, 116, 118, 121, 123, 125,
    128, 130, 133, 136, 138, 141, 144, 147,
    150, 153, 156, 159, 162, 165, 168, 172,
    175, 179, 182, 186, 189, 193, 197, 201,
    205, 209, 213, 217, 222, 226, 231, 235,
    240, 245, 249, 254, 259, 265, 270, 275,
    281, 286, 292, 298, 304, 310, 316, 322,
    328, 335, 342, 348, 355, 362, 369, 377,
    384, 392, 400, 408, 416, 424, 432, 441,
    450, 459, 468, 477, 486, 496, 506, 516,
    526, 537, 547, 558, 569, 581, 592, 604,
    616, 628, 640, 653, 666, 679, 693, 707,
    721, 735, 749, 764, 780, 795, 811, 827,
    843, 860, 877, 894, 912, 930, 949, 968,
    987, 1006, 1026, 1047, 1067, 1089, 1110, 1132,
    1155, 1178, 1201, 1225, 1249, 1274, 1299, 1325,
    1351, 1378, 1405, 1433, 1461, 1490, 1520, 1550,
    1581, 1612, 1644, 1677, 1710, 1744, 1779, 1814,
    1850, 1886, 1924, 1962, 2001, 2040, 2081, 2122,
    2164, 2207, 2251, 2295, 2341, 2387, 2435, 2483,
    2532, 2582, 2633, 2685, 2738, 2793, 2848, 2904,
    2962, 3020, 3080, 3141, 3203, 3267, 3331, 3397,
    3464, 3533, 3603, 3674, 3746, 3820, 3896, 3973,
    4051, 4131, 4213, 4296, 4380, 4467, 4555, 4645,
    4736, 4830, 4925, 5022, 5120, 5221, 5324, 5429,
    5535, 5644, 5755, 5868, 5983, 6100, 6220, 6342,
    6466, 6593, 6722, 6853, 6987, 7123, 7262, 7404,
    7548, 7695, 7845, 7998, 8153, 8311, 8472, 8637,
    8804, 8974, 9147, 9324, 9504, 9687, 9873, 10062,
    10255, 10452, 10652, 10855, 11062, 11272, 11486, 11704,
    11926, 12151, 12380, 12613, 12850, 13090, 13335, 13583,
};

/* Resonance-to-Q14-feedback-coefficient table, index 0-15 matching
 * SID's own 4-bit resonance register. Q sweeps 0.707 (heavily damped,
 * index 0) to 8.0 (sharp resonant peak, index 15), q = 1/Q. Exact
 * Python: `round((1.0 / (0.707 + i * (8.0 - 0.707) / 15.0)) * 16384)`
 * for i in 0..15. */
const int synth_filter_q_coeff[16] = {
    23174, 13731, 9756, 7566, 6178, 5221, 4521, 3986, 3564, 3223, 2942, 2706, 2505, 2331, 2181, 2048,
};

int synth_filter_cutoff_index = 128;
int synth_filter_res_index = 0;
int synth_filter_mode_mask = SYNTH_FILTER_MODE_LP;

void synth_set_filter_cutoff(int cutoff) {
    if (cutoff < 0) {
        cutoff = 0;
    }
    if (cutoff > 255) {
        cutoff = 255;
    }
    synth_filter_cutoff_index = cutoff;
}

void synth_set_filter_resonance(int resonance) {
    if (resonance < 0) {
        resonance = 0;
    }
    if (resonance > 15) {
        resonance = 15;
    }
    synth_filter_res_index = resonance;
}

void synth_set_filter_mode(int mode_mask) {
    if (mode_mask < 0) {
        mode_mask = 0;
    }
    if (mode_mask > 7) {
        mode_mask = 7;
    }
    synth_filter_mode_mask = mode_mask;
}

int synth_filter_process_sample(struct synth_filter_state *st, int input,
                                 int f_coeff, int q_coeff, int mode_mask) {
    int hp = input - st->lp - ((q_coeff * st->bp) >> 14);
    int bp_new = st->bp + ((f_coeff * hp) >> 14);
    int lp_new = st->lp + ((f_coeff * bp_new) >> 14);

    if (lp_new > SYNTH_FILTER_STATE_MAX) {
        lp_new = SYNTH_FILTER_STATE_MAX;
    }
    if (lp_new < -SYNTH_FILTER_STATE_MAX) {
        lp_new = -SYNTH_FILTER_STATE_MAX;
    }
    if (bp_new > SYNTH_FILTER_STATE_MAX) {
        bp_new = SYNTH_FILTER_STATE_MAX;
    }
    if (bp_new < -SYNTH_FILTER_STATE_MAX) {
        bp_new = -SYNTH_FILTER_STATE_MAX;
    }

    st->lp = lp_new;
    st->bp = bp_new;

    {
        int out = 0;
        if (mode_mask & SYNTH_FILTER_MODE_LP) {
            out += lp_new;
        }
        if (mode_mask & SYNTH_FILTER_MODE_BP) {
            out += bp_new;
        }
        if (mode_mask & SYNTH_FILTER_MODE_HP) {
            out += hp;
        }
        return out;
    }
}
```

- [ ] **Step 3: Append tests to `kernel/tests/test_synth.c`**

Insert before `int main(void) {`:

```c
static void test_filter_table_sanity(void) {
    int i;
    CHECK(synth_filter_f_coeff[0] == 93, "filter cutoff table starts at the expected 20Hz coefficient");
    CHECK(synth_filter_f_coeff[255] == 13583, "filter cutoff table ends at the expected 3000Hz coefficient");
    for (i = 1; i < 256; i++) {
        CHECK(synth_filter_f_coeff[i] > synth_filter_f_coeff[i - 1], "filter cutoff table monotonically increasing");
    }
    CHECK(synth_filter_q_coeff[0] == 23174, "filter resonance table starts at the expected Q=0.707 coefficient");
    CHECK(synth_filter_q_coeff[15] == 2048, "filter resonance table ends at the expected Q=8.0 coefficient");
    for (i = 1; i < 16; i++) {
        CHECK(synth_filter_q_coeff[i] < synth_filter_q_coeff[i - 1], "filter resonance table monotonically decreasing (higher index = higher resonance = lower q)");
    }
}

static void test_filter_setters_clamp(void) {
    synth_set_filter_cutoff(-5);
    CHECK(synth_filter_cutoff_index == 0, "filter cutoff clamps negative input to 0");
    synth_set_filter_cutoff(9999);
    CHECK(synth_filter_cutoff_index == 255, "filter cutoff clamps out-of-range input to 255");
    synth_set_filter_cutoff(100);
    CHECK(synth_filter_cutoff_index == 100, "filter cutoff accepts an in-range value unchanged");

    synth_set_filter_resonance(-1);
    CHECK(synth_filter_res_index == 0, "filter resonance clamps negative input to 0");
    synth_set_filter_resonance(999);
    CHECK(synth_filter_res_index == 15, "filter resonance clamps out-of-range input to 15");

    synth_set_filter_mode(-1);
    CHECK(synth_filter_mode_mask == 0, "filter mode clamps negative input to 0");
    synth_set_filter_mode(999);
    CHECK(synth_filter_mode_mask == 7, "filter mode clamps out-of-range input to 7 (LP|BP|HP)");
    synth_set_filter_mode(SYNTH_FILTER_MODE_BP);
    CHECK(synth_filter_mode_mask == SYNTH_FILTER_MODE_BP, "filter mode accepts an in-range value unchanged");
}

static void test_filter_lowpass_step_response_converges(void) {
    struct synth_filter_state st = {0, 0};
    int i;
    int f_coeff = synth_filter_f_coeff[64];  /* a moderate cutoff */
    int q_coeff = synth_filter_q_coeff[0];   /* lowest resonance */
    int last = 0;
    int converged = 0;

    for (i = 0; i < 2000; i++) {
        int out = synth_filter_process_sample(&st, 1000, f_coeff, q_coeff, SYNTH_FILTER_MODE_LP);
        if (i > 1000) {
            int delta = out - last;
            if (delta < 0) delta = -delta;
            if (delta < 5) {
                converged = 1;
            }
        }
        last = out;
    }
    CHECK(converged, "low-pass output settles toward a steady value under a held step input");
    CHECK(last > 500 && last < 1500, "settled low-pass output is in a sane range near the step input, not wildly off");
}

static void test_filter_resonance_increases_peak_overshoot(void) {
    struct synth_filter_state st_low = {0, 0};
    struct synth_filter_state st_high = {0, 0};
    int i;
    int f_coeff = synth_filter_f_coeff[200];
    int peak_low = 0, peak_high = 0;

    for (i = 0; i < 200; i++) {
        int out_low = synth_filter_process_sample(&st_low, 1000, f_coeff, synth_filter_q_coeff[0], SYNTH_FILTER_MODE_LP);
        int out_high = synth_filter_process_sample(&st_high, 1000, f_coeff, synth_filter_q_coeff[15], SYNTH_FILTER_MODE_LP);
        if (out_low > peak_low) peak_low = out_low;
        if (out_high > peak_high) peak_high = out_high;
    }
    CHECK(peak_high > peak_low, "higher resonance produces a larger peak overshoot than lower resonance at the same cutoff");
}

static void test_filter_mode_zero_is_silent(void) {
    struct synth_filter_state st = {0, 0};
    int i;
    int all_zero = 1;
    for (i = 0; i < 100; i++) {
        int out = synth_filter_process_sample(&st, 1000, synth_filter_f_coeff[200], synth_filter_q_coeff[8], 0);
        if (out != 0) {
            all_zero = 0;
        }
    }
    CHECK(all_zero, "filter mode 0 (no LP/BP/HP selected) produces silence regardless of input");
}

static void test_filter_never_exceeds_state_clamp_across_full_range(void) {
    int cutoff_idx, res_idx, mode;
    int any_exceeded = 0;
    for (cutoff_idx = 0; cutoff_idx < 256; cutoff_idx += 17) {
        for (res_idx = 0; res_idx < 16; res_idx++) {
            for (mode = 1; mode <= 7; mode++) {
                struct synth_filter_state st = {0, 0};
                int i;
                for (i = 0; i < 500; i++) {
                    synth_filter_process_sample(&st, 1016, synth_filter_f_coeff[cutoff_idx], synth_filter_q_coeff[res_idx], mode);
                    if (st.lp > SYNTH_FILTER_STATE_MAX || st.lp < -SYNTH_FILTER_STATE_MAX ||
                        st.bp > SYNTH_FILTER_STATE_MAX || st.bp < -SYNTH_FILTER_STATE_MAX) {
                        any_exceeded = 1;
                    }
                }
            }
        }
    }
    CHECK(!any_exceeded, "internal filter state never exceeds its clamp across a sweep of the full cutoff/resonance/mode range");
}
```

And add these calls inside `main()`, right after the last existing test call:
```c
    test_filter_table_sanity();
    test_filter_setters_clamp();
    test_filter_lowpass_step_response_converges();
    test_filter_resonance_increases_peak_overshoot();
    test_filter_mode_zero_is_silent();
    test_filter_never_exceeds_state_clamp_across_full_range();
```

- [ ] **Step 4: Build and run the test**

Run from `kernel/`:
```bash
gcc -m32 -Wall -Wextra tests/test_synth.c audio/synth.c -o /tmp/test_synth
/tmp/test_synth
```
Expected: no compiler warnings, output `PASS`, exit code 0. (This entire task's new coefficient tables and filter core were empirically verified while writing this plan — 256 cutoffs x 16 resonances x 7 mode combinations x 5000 samples each, 143M samples total, and the safety clamp never once triggered even at the most extreme setting.)

- [ ] **Step 5: Confirm freestanding compilation is still clean**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
i686-elf-gcc -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -Wall -Wextra -Iaudio -c audio/synth.c -o /tmp/synth_filter_freestanding_check.o
```
Expected: exit 0, no warnings.

- [ ] **Step 6: Commit**

```bash
git add kernel/audio/synth.h kernel/audio/synth.c kernel/tests/test_synth.c
git commit -m "audio: fixed-point resonant filter core, host-tested"
```

---

## Task 2: Ring modulation

**Files:**
- Modify: `kernel/audio/synth.h`
- Modify: `kernel/audio/synth.c`
- Modify: `kernel/tests/test_synth.c`

**Interfaces:**
- Consumes: `struct synth_voice` (Task 1's predecessor state, unmodified by Task 1).
- Produces: new `struct synth_voice` field `ring_partner` (`-1` = off, `0`-`7` = ring against that voice), `void synth_set_ring_partner(int voice, int partner)`, `void synth_clear_ring_partner(int voice)`. **Changes `synth_osc_sample()`'s signature** — this task's own Step 6 updates its one call site inside `synth_render_half()` to match; Task 3 edits that same function again afterward (for the filter-routing split) and must build on this task's version of it, not the pre-Task-2 one.

- [ ] **Step 1: Modify `struct synth_voice` in `kernel/audio/synth.h`**

Replace:
```c
    int sustain_level;
    int release_rate;
};
```
with:
```c
    int sustain_level;
    int release_rate;
    /* -1 = ring mod off; 0-7 = ring-modulate this voice's triangle
     * output against that voice's own phase. Meaningless for any
     * other waveform -- matches real SID, whose ring mod is wired
     * directly into the triangle generator's fold-direction bit,
     * not a generic effect. */
    int ring_partner;
};
```

- [ ] **Step 2: Modify `kernel/audio/synth.h` -- new declarations and `synth_osc_sample()`'s signature**

Replace:
```c
void synth_gate_on(int voice);
void synth_gate_off(int voice);

int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped);
```
with:
```c
void synth_gate_on(int voice);
void synth_gate_off(int voice);
void synth_set_ring_partner(int voice, int partner);
void synth_clear_ring_partner(int voice);

/* ring_active/ring_partner_phase_accum only affect WAVE_TRIANGLE's fold
 * direction (real SID's ring mod is wired into the triangle generator
 * specifically) -- pass ring_active=0 for every other waveform, or
 * whenever the calling voice's ring_partner is -1. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped, int ring_active,
                      unsigned int ring_partner_phase_accum);
```

- [ ] **Step 3: Modify `synth_init()` in `kernel/audio/synth.c`**

Replace:
```c
        synth_voices[v].release_rate = SYNTH_ENV_FULL << 8;
    }
}
```
with:
```c
        synth_voices[v].release_rate = SYNTH_ENV_FULL << 8;
        synth_voices[v].ring_partner = -1;
    }
}
```

- [ ] **Step 4: Modify `kernel/audio/synth.c` -- add the two setters, right after `synth_set_ona()`**

Replace:
```c
    synth_voices[voice].phase_increment = ona_phase_increment[ona - 1];
}

/* 16-bit Galois LFSR, maximal-length tap mask 0xB400.
```
with:
```c
    synth_voices[voice].phase_increment = ona_phase_increment[ona - 1];
}

void synth_set_ring_partner(int voice, int partner) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (partner < 0 || partner > 7) {
        return;
    }
    synth_voices[voice].ring_partner = partner;
}

void synth_clear_ring_partner(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].ring_partner = -1;
}

/* 16-bit Galois LFSR, maximal-length tap mask 0xB400.
```

- [ ] **Step 5: Modify `synth_osc_sample()` in `kernel/audio/synth.c`**

Replace:
```c
/* Returns a signed sample in roughly [-128, 127] for one oscillator.
 * duty_threshold (0-255) only matters for WAVE_PULSE. noise_lfsr is
 * only read/advanced for WAVE_NOISE. phase_accum's top 8 bits
 * (pos8, 0-255) give this sample's position within the current
 * period for the three deterministic waveforms. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped) {
    unsigned int pos8 = (phase_accum >> 24) & 0xFFu;

    switch (wave) {
    case WAVE_SAW:
        return (int)pos8 - 128;
    case WAVE_TRIANGLE: {
        unsigned int tri_pos = (pos8 < 128u) ? pos8 : (255u - pos8);
        return (int)(tri_pos * 2u) - 128;
    }
    case WAVE_PULSE:
```
with:
```c
/* Returns a signed sample in roughly [-128, 127] for one oscillator.
 * duty_threshold (0-255) only matters for WAVE_PULSE. noise_lfsr is
 * only read/advanced for WAVE_NOISE. phase_accum's top 8 bits
 * (pos8, 0-255) give this sample's position within the current
 * period for the three deterministic waveforms. ring_active/
 * ring_partner_phase_accum only matter for WAVE_TRIANGLE -- see the
 * WAVE_TRIANGLE case below and synth.h's doc comment. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped, int ring_active,
                      unsigned int ring_partner_phase_accum) {
    unsigned int pos8 = (phase_accum >> 24) & 0xFFu;

    switch (wave) {
    case WAVE_SAW:
        return (int)pos8 - 128;
    case WAVE_TRIANGLE: {
        /* msb (pos8's own top bit) decides fold direction; lower7 is
         * the position within that half-period. Ring mod XORs a
         * partner voice's own top bit into that fold decision --
         * exactly how real SID's ring mod is wired into the triangle
         * generator. Behaviorally identical to the plain (pos8<128)
         * form when ring_active is 0: msb=0 -> tri_pos=lower7
         * (matches pos8<128's pos8==lower7); msb=1 -> tri_pos=
         * 127-lower7 (matches pos8>=128's 255-pos8 = 127-lower7). */
        unsigned int msb = (pos8 >> 7) & 1u;
        unsigned int lower7 = pos8 & 0x7Fu;
        unsigned int tri_pos;
        if (ring_active) {
            unsigned int partner_pos8 = (ring_partner_phase_accum >> 24) & 0xFFu;
            msb ^= (partner_pos8 >> 7) & 1u;
        }
        tri_pos = msb ? (127u - lower7) : lower7;
        return (int)(tri_pos * 2u) - 128;
    }
    case WAVE_PULSE:
```

- [ ] **Step 6: Modify the one call site of `synth_osc_sample()` inside `synth_render_half()`**

Replace:
```c
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped);
            int level = synth_envelope_advance_sample(voice);
```
with:
```c
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int ring_active = (voice->ring_partner >= 0) ? 1 : 0;
            unsigned int ring_partner_accum = ring_active
                ? synth_voices[voice->ring_partner].phase_accum : 0u;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped,
                                        ring_active, ring_partner_accum);
            int level = synth_envelope_advance_sample(voice);
```

- [ ] **Step 7: Update every existing `synth_osc_sample()` call in `kernel/tests/test_synth.c` for the new signature**

The existing test file has 17 calls to `synth_osc_sample(...)`, each ending in `&lfsr, N)` or `&lfsr_a, N)`/`&lfsr_b, N)` where `N` is `0` or `1` (the `phase_wrapped` argument). Append `, 0, 0` (ring inactive, no partner) to each one, immediately before its closing `)` — i.e. every existing call like:
```c
synth_osc_sample(WAVE_SAW, 0x00000000u, 128, &lfsr, 0)
```
becomes:
```c
synth_osc_sample(WAVE_SAW, 0x00000000u, 128, &lfsr, 0, 0, 0)
```
Do this for all 17 existing call sites (in `test_waveform_saw`, `test_waveform_triangle`, `test_waveform_pulse`, `test_waveform_noise`) — every one, or the file will not compile against the new signature.

- [ ] **Step 8: Append ring-mod tests to `kernel/tests/test_synth.c`**

Insert before `int main(void) {`:

```c
static void test_ring_mod_changes_triangle_when_partner_msb_differs(void) {
    unsigned int lfsr = 1;
    /* own phase at pos8=0 (msb=0); partner at pos8=255 (msb=1) --
     * ring mod should flip the fold direction relative to no ring mod. */
    int without_ring = synth_osc_sample(WAVE_TRIANGLE, 0x00000000u, 128, &lfsr, 0, 0, 0);
    int with_ring = synth_osc_sample(WAVE_TRIANGLE, 0x00000000u, 128, &lfsr, 0, 1, 0xFF000000u);
    CHECK(without_ring != with_ring, "ring mod changes the triangle output when the partner's MSB differs from this voice's own");
}

static void test_ring_mod_no_effect_when_partner_msb_matches(void) {
    unsigned int lfsr = 1;
    /* own phase at pos8=0 (msb=0); partner also at pos8=0 (msb=0) --
     * XORing two matching bits is 0, so ring mod should be a no-op here. */
    int without_ring = synth_osc_sample(WAVE_TRIANGLE, 0x00000000u, 128, &lfsr, 0, 0, 0);
    int with_ring = synth_osc_sample(WAVE_TRIANGLE, 0x00000000u, 128, &lfsr, 0, 1, 0x00000000u);
    CHECK(without_ring == with_ring, "ring mod against a partner with the same MSB is a no-op, as XOR-of-equal-bits predicts");
}

static void test_ring_mod_self_reference_is_harmless(void) {
    unsigned int lfsr = 1;
    /* A voice ring-modulating against its own phase: XOR-with-self
     * always clears the bit, so this must behave exactly like msb=0,
     * not crash or produce a wildly out-of-range sample. */
    int self_ring = synth_osc_sample(WAVE_TRIANGLE, 0xFF000000u, 128, &lfsr, 0, 1, 0xFF000000u);
    CHECK(self_ring >= -128 && self_ring <= 127, "ring mod against itself stays in the valid sample range, no crash or overflow");
    CHECK(self_ring == 126, "ring mod against itself always clears the fold bit (XOR of equal bits is 0), so tri_pos = lower7 = 127 here -> sample 126");
}

static void test_ring_mod_has_no_effect_on_non_triangle_waveforms(void) {
    unsigned int lfsr_a = 42, lfsr_b = 42;
    int saw_without = synth_osc_sample(WAVE_SAW, 0x00000000u, 128, &lfsr_a, 0, 0, 0);
    int saw_with = synth_osc_sample(WAVE_SAW, 0x00000000u, 128, &lfsr_a, 0, 1, 0xFF000000u);
    CHECK(saw_without == saw_with, "ring mod has no effect on sawtooth (only wired into the triangle generator)");

    {
        int pulse_without = synth_osc_sample(WAVE_PULSE, 0x00000000u, 128, &lfsr_a, 0, 0, 0);
        int pulse_with = synth_osc_sample(WAVE_PULSE, 0x00000000u, 128, &lfsr_a, 0, 1, 0xFF000000u);
        CHECK(pulse_without == pulse_with, "ring mod has no effect on pulse");
    }

    {
        int noise_without = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 1, 0, 0);
        int noise_with = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_b, 1, 1, 0xFF000000u);
        CHECK(noise_without == noise_with, "ring mod has no effect on noise (both advance the LFSR identically regardless of ring params)");
    }
}

static void test_ring_partner_setters(void) {
    synth_init();
    CHECK(synth_voices[0].ring_partner == -1, "ring partner defaults to -1 (off) after synth_init()");

    synth_set_ring_partner(0, 3);
    CHECK(synth_voices[0].ring_partner == 3, "synth_set_ring_partner sets an in-range partner");

    synth_set_ring_partner(0, -1);
    CHECK(synth_voices[0].ring_partner == 3, "synth_set_ring_partner ignores an out-of-range (negative) partner, does not clear it");

    synth_set_ring_partner(0, 8);
    CHECK(synth_voices[0].ring_partner == 3, "synth_set_ring_partner ignores an out-of-range (too high) partner");

    synth_clear_ring_partner(0);
    CHECK(synth_voices[0].ring_partner == -1, "synth_clear_ring_partner turns ring mod back off");
}
```

And add these calls inside `main()`, right after the last existing test call:
```c
    test_ring_mod_changes_triangle_when_partner_msb_differs();
    test_ring_mod_no_effect_when_partner_msb_matches();
    test_ring_mod_self_reference_is_harmless();
    test_ring_mod_has_no_effect_on_non_triangle_waveforms();
    test_ring_partner_setters();
```

- [ ] **Step 9: Build and run the test**

```bash
gcc -m32 -Wall -Wextra tests/test_synth.c audio/synth.c -o /tmp/test_synth
/tmp/test_synth
```
Expected: no compiler warnings, output `PASS`, exit code 0.

- [ ] **Step 10: Confirm freestanding compilation is still clean**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
i686-elf-gcc -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -Wall -Wextra -Iaudio -c audio/synth.c -o /tmp/synth_filter_freestanding_check.o
```
Expected: exit 0, no warnings.

- [ ] **Step 11: Commit**

```bash
git add kernel/audio/synth.h kernel/audio/synth.c kernel/tests/test_synth.c
git commit -m "audio: ring modulation on the triangle waveform, host-tested"
```

---

## Task 3: Mixer integration -- filter routing in `synth_render_half()`

**Files:**
- Modify: `kernel/audio/synth.h`
- Modify: `kernel/audio/synth.c`
- Modify: `kernel/tests/test_synth.c`

**Interfaces:**
- Consumes: everything from Tasks 1-2 (`synth_filter_process_sample()`, `synth_filter_f_coeff[]`/`synth_filter_q_coeff[]`, `synth_filter_cutoff_index`/`_res_index`/`_mode_mask`, `struct synth_voice.ring_partner`, the extended `synth_osc_sample()`).
- Produces: new `struct synth_voice` field `filter_route` (0 = bypass, 1 = routed through the filter), `void synth_set_voice_filter_route(int voice, int routed)`. **Task 4 (Forth control surface) calls this and the Task 1/2 setters directly.**

- [ ] **Step 1: Modify `struct synth_voice` in `kernel/audio/synth.h`**

Replace:
```c
    int ring_partner;
};
```
with:
```c
    int ring_partner;
    /* 0 = straight to output (bypasses the shared filter); 1 = routed
     * through it. Matches real SID's own per-voice filter routing bits,
     * scaled to 8 voices. */
    int filter_route;
};
```

- [ ] **Step 2: Modify `kernel/audio/synth.h` -- new declaration**

Replace:
```c
void synth_set_ring_partner(int voice, int partner);
void synth_clear_ring_partner(int voice);
```
with:
```c
void synth_set_ring_partner(int voice, int partner);
void synth_clear_ring_partner(int voice);
void synth_set_voice_filter_route(int voice, int routed);
```

- [ ] **Step 3: Modify `synth_init()` in `kernel/audio/synth.c`**

Replace:
```c
        synth_voices[v].ring_partner = -1;
    }
}
```
with:
```c
        synth_voices[v].ring_partner = -1;
        synth_voices[v].filter_route = 0;
    }
}
```

- [ ] **Step 4: Modify `kernel/audio/synth.c` -- add the setter, right after `synth_clear_ring_partner()`**

Replace:
```c
void synth_clear_ring_partner(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].ring_partner = -1;
}
```
with:
```c
void synth_clear_ring_partner(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].ring_partner = -1;
}

void synth_set_voice_filter_route(int voice, int routed) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].filter_route = routed ? 1 : 0;
}
```

- [ ] **Step 5: Modify `synth_render_half()` in `kernel/audio/synth.c`**

Replace:
```c
void synth_render_half(unsigned char *buf, unsigned int len) {
    unsigned int i;
    int v;

    for (i = 0; i < len; i++) {
        int sum = 0;

        for (v = 0; v < SYNTH_NUM_VOICES; v++) {
            struct synth_voice *voice = &synth_voices[v];
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int ring_active = (voice->ring_partner >= 0) ? 1 : 0;
            unsigned int ring_partner_accum = ring_active
                ? synth_voices[voice->ring_partner].phase_accum : 0u;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped,
                                        ring_active, ring_partner_accum);
            int level = synth_envelope_advance_sample(voice);

            /* level is Q0.15 (0..32768): at full envelope this is an
             * exact pass-through of osc (127*32768>>15 == 127). */
            sum += (osc * level) >> 15;
            voice->phase_accum = new_accum;
        }

        /* Must clamp before biasing to unsigned 8-bit: 8 simultaneously
         * maxed-out voices sum to roughly +-1016, about 4x what a
         * signed byte holds -- an unclamped sum would wrap into loud
         * garbage instead of just clipping. */
        if (sum > 127) {
            sum = 127;
        }
        if (sum < -128) {
            sum = -128;
        }
        buf[i] = (unsigned char)(sum + 128);
    }
}
```
with:
```c
/* The one shared filter instance (matches "one shared filter", not one
 * per voice, per the SID architecture this is scaled up from). Its
 * lp/bp state persists across calls to synth_render_half() the same
 * way each voice's phase_accum/envelope_level already do. */
static struct synth_filter_state synth_filter;

void synth_render_half(unsigned char *buf, unsigned int len) {
    unsigned int i;
    int v;

    for (i = 0; i < len; i++) {
        int filtered_sum = 0;
        int bypass_sum = 0;
        int sum;
        int filtered_out;

        for (v = 0; v < SYNTH_NUM_VOICES; v++) {
            struct synth_voice *voice = &synth_voices[v];
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int ring_active = (voice->ring_partner >= 0) ? 1 : 0;
            unsigned int ring_partner_accum = ring_active
                ? synth_voices[voice->ring_partner].phase_accum : 0u;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped,
                                        ring_active, ring_partner_accum);
            int level = synth_envelope_advance_sample(voice);
            int contribution;

            /* level is Q0.15 (0..32768): at full envelope this is an
             * exact pass-through of osc (127*32768>>15 == 127). */
            contribution = (osc * level) >> 15;
            if (voice->filter_route) {
                filtered_sum += contribution;
            } else {
                bypass_sum += contribution;
            }
            voice->phase_accum = new_accum;
        }

        filtered_out = synth_filter_process_sample(&synth_filter, filtered_sum,
                                                     synth_filter_f_coeff[synth_filter_cutoff_index],
                                                     synth_filter_q_coeff[synth_filter_res_index],
                                                     synth_filter_mode_mask);
        sum = filtered_out + bypass_sum;

        /* Must clamp before biasing to unsigned 8-bit: unfiltered voices
         * alone can already sum to roughly +-1016 (8 maxed-out voices),
         * about 4x what a signed byte holds, and a resonant filter can
         * amplify filtered_sum well past that near its cutoff on top --
         * an unclamped sum would wrap into loud garbage instead of just
         * clipping. */
        if (sum > 127) {
            sum = 127;
        }
        if (sum < -128) {
            sum = -128;
        }
        buf[i] = (unsigned char)(sum + 128);
    }
}
```

- [ ] **Step 6: Append integration tests to `kernel/tests/test_synth.c`**

Insert before `int main(void) {`:

```c
static void test_render_half_default_filter_route_matches_unfiltered_behavior(void) {
    unsigned char buf[4];
    synth_init();
    synth_set_voice_waveform(0, WAVE_SAW);
    synth_voices[0].phase_accum = 0;
    synth_voices[0].phase_increment = 0;
    synth_voices[0].envelope_stage = ENV_SUSTAIN;
    synth_voices[0].envelope_level = SYNTH_ENV_FULL << 8;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL << 8;
    /* filter_route defaults to 0 (bypass) after synth_init() -- output
     * should be identical to the pre-filter engine's behavior, proving
     * adding the filter didn't change anything for voices that don't
     * opt into it. */
    synth_render_half(buf, 1);
    CHECK(buf[0] == 0, "a bypass-routed voice's output is unaffected by the filter (matches the original single-voice-passthrough test)");
}

static void test_render_half_filter_route_changes_output(void) {
    unsigned char buf_bypass[8];
    unsigned char buf_filtered[8];
    int i;
    int differs = 0;

    synth_init();
    synth_set_voice_waveform(0, WAVE_PULSE);
    synth_set_duty(0, 50);
    synth_voices[0].phase_accum = 0;
    synth_voices[0].phase_increment = 0x10000000u; /* a mid-range tone */
    synth_voices[0].envelope_stage = ENV_SUSTAIN;
    synth_voices[0].envelope_level = SYNTH_ENV_FULL << 8;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL << 8;
    synth_set_filter_cutoff(64);
    synth_set_filter_resonance(8);
    synth_set_filter_mode(SYNTH_FILTER_MODE_LP);
    synth_set_voice_filter_route(0, 0);
    synth_render_half(buf_bypass, 8);

    synth_init();
    synth_set_voice_waveform(0, WAVE_PULSE);
    synth_set_duty(0, 50);
    synth_voices[0].phase_accum = 0;
    synth_voices[0].phase_increment = 0x10000000u;
    synth_voices[0].envelope_stage = ENV_SUSTAIN;
    synth_voices[0].envelope_level = SYNTH_ENV_FULL << 8;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL << 8;
    synth_set_filter_cutoff(64);
    synth_set_filter_resonance(8);
    synth_set_filter_mode(SYNTH_FILTER_MODE_LP);
    synth_set_voice_filter_route(0, 1);
    synth_render_half(buf_filtered, 8);

    for (i = 0; i < 8; i++) {
        if (buf_bypass[i] != buf_filtered[i]) {
            differs = 1;
        }
    }
    CHECK(differs, "routing a voice through the filter produces different output than bypassing it, same source signal");
}

static void test_render_half_filter_state_persists_across_calls(void) {
    unsigned char buf1[4];
    unsigned char buf2[4];
    synth_init();
    synth_set_voice_waveform(0, WAVE_PULSE);
    synth_voices[0].phase_accum = 0;
    synth_voices[0].phase_increment = 0;
    synth_voices[0].envelope_stage = ENV_SUSTAIN;
    synth_voices[0].envelope_level = SYNTH_ENV_FULL << 8;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL << 8;
    synth_set_filter_cutoff(220);
    synth_set_filter_resonance(4);
    synth_set_filter_mode(SYNTH_FILTER_MODE_LP);
    synth_set_voice_filter_route(0, 1);
    synth_render_half(buf1, 4);
    synth_render_half(buf2, 4);
    /* A low-pass ramping toward a held step input reaches its
     * steady-state within buf1's own 4 samples at this cutoff; buf2's
     * first sample continues from that converged state rather than
     * restarting the ramp -- if the filter's internal state were reset
     * each call instead of persisting (like each voice's phase_accum/
     * envelope_level already do), buf2[0] would replay buf1[0]'s exact
     * startup value instead of picking up where buf1 left off. */
    CHECK(buf1[0] != buf2[0],
          "the shared filter's internal state persists across synth_render_half() calls, not reset each time");
}
```

And add these calls inside `main()`, right after the last existing test call:
```c
    test_render_half_default_filter_route_matches_unfiltered_behavior();
    test_render_half_filter_route_changes_output();
    test_render_half_filter_state_persists_across_calls();
```

- [ ] **Step 7: Build and run the full test suite**

```bash
gcc -m32 -Wall -Wextra tests/test_synth.c audio/synth.c -o /tmp/test_synth
/tmp/test_synth
```
Expected: no compiler warnings, output `PASS`, exit code 0.

- [ ] **Step 8: Confirm freestanding compilation is still clean**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
i686-elf-gcc -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -Wall -Wextra -Iaudio -c audio/synth.c -o /tmp/synth_filter_freestanding_check.o
```
Expected: exit 0, no warnings.

- [ ] **Step 9: Commit**

```bash
git add kernel/audio/synth.h kernel/audio/synth.c kernel/tests/test_synth.c
git commit -m "audio: route the shared filter into the mixer, host-tested"
```

---

## Task 4: The Forth control surface

**Files:**
- Modify: `kernel/forth/forth_hooks.h`
- Modify: `kernel/kernel.c`
- Modify: `kernel/forth/forth.c`

**Interfaces:**
- Consumes: `synth_set_filter_cutoff()`, `synth_set_filter_resonance()`, `synth_set_filter_mode()`, `synth_set_voice_filter_route()`, `synth_set_ring_partner()`, `synth_clear_ring_partner()` (all from Tasks 1-3), and the existing `synth_current_voice`/`audio_ensure_stream_started()` from the already-shipped engine's Forth wiring.
- Produces: six Forth words `FILTER-CUTOFF`, `FILTER-RES`, `FILTER-MODE`, `FILTER-ROUTE`, `RING-PARTNER`, `RING-OFF`.

Not host-buildable in isolation the same way Tasks 1-3 are (this wires into the real Forth VM and the already-shipped audio-stream machinery) — verified via headless QEMU, same pattern as the original engine's own Task 4.

- [ ] **Step 1: Modify `kernel/forth/forth_hooks.h`**

Replace:
```c
void forth_hook_synth_gate_on(void);
void forth_hook_synth_gate_off(void);
```
with:
```c
void forth_hook_synth_gate_on(void);
void forth_hook_synth_gate_off(void);
void forth_hook_synth_filter_cutoff(int cutoff);
void forth_hook_synth_filter_res(int resonance);
void forth_hook_synth_filter_mode(int mode_mask);
void forth_hook_synth_filter_route(int routed);
void forth_hook_synth_ring_partner(int partner);
void forth_hook_synth_ring_off(void);
```

- [ ] **Step 2: Modify `kernel/kernel.c` -- the six new hooks**

Replace:
```c
void forth_hook_synth_gate_on(void) {
    audio_ensure_stream_started();
    synth_gate_on(synth_current_voice);
}

void forth_hook_synth_gate_off(void) {
    synth_gate_off(synth_current_voice);
}
```
with:
```c
void forth_hook_synth_gate_on(void) {
    audio_ensure_stream_started();
    synth_gate_on(synth_current_voice);
}

void forth_hook_synth_gate_off(void) {
    synth_gate_off(synth_current_voice);
}

void forth_hook_synth_filter_cutoff(int cutoff) {
    audio_ensure_stream_started();
    synth_set_filter_cutoff(cutoff);
}

void forth_hook_synth_filter_res(int resonance) {
    audio_ensure_stream_started();
    synth_set_filter_resonance(resonance);
}

void forth_hook_synth_filter_mode(int mode_mask) {
    audio_ensure_stream_started();
    synth_set_filter_mode(mode_mask);
}

void forth_hook_synth_filter_route(int routed) {
    audio_ensure_stream_started();
    synth_set_voice_filter_route(synth_current_voice, routed);
}

void forth_hook_synth_ring_partner(int partner) {
    audio_ensure_stream_started();
    synth_set_ring_partner(synth_current_voice, partner);
}

void forth_hook_synth_ring_off(void) {
    synth_clear_ring_partner(synth_current_voice);
}
```

- [ ] **Step 3: Modify `kernel/forth/forth.c` -- the six new primitives**

Replace:
```c
static void prim_synth_gate_on(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_gate_on();
}

static void prim_synth_gate_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_gate_off();
}
```
with:
```c
static void prim_synth_gate_on(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_gate_on();
}

static void prim_synth_gate_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_gate_off();
}

/* FILTER-CUTOFF/-RES/-MODE/-ROUTE: like DUTY, the underlying
 * synth_set_* setters already clamp gracefully -- no hard Forth-level
 * error, matching DUTY's own convention rather than VOICE/WAVE/ONA's
 * discrete-index-with-a-meaningless-out-of-range-value convention. */
static void prim_synth_filter_cutoff(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    forth_hook_synth_filter_cutoff((int)n);
}

static void prim_synth_filter_res(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    forth_hook_synth_filter_res((int)n);
}

static void prim_synth_filter_mode(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    forth_hook_synth_filter_mode((int)n);
}

static void prim_synth_filter_route(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    forth_hook_synth_filter_route((int)n);
}

/* RING-PARTNER: a discrete voice index with a meaningless out-of-range
 * value (there is no voice 8) -- matches VOICE/WAVE/ONA's hard-error
 * convention, not DUTY's clamp-gracefully one. */
static void prim_synth_ring_partner(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    if (n < 0 || n > 7) {
        forth_set_error(vm, "BAD PARTNER");
        return;
    }
    forth_hook_synth_ring_partner((int)n);
}

static void prim_synth_ring_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_ring_off();
}
```

- [ ] **Step 4: Modify `kernel/forth/forth.c` -- the primitive table**

Replace:
```c
    {"GATE-ON", prim_synth_gate_on}, {"GATE-OFF", prim_synth_gate_off},
```
with:
```c
    {"GATE-ON", prim_synth_gate_on}, {"GATE-OFF", prim_synth_gate_off},
    {"FILTER-CUTOFF", prim_synth_filter_cutoff}, {"FILTER-RES", prim_synth_filter_res},
    {"FILTER-MODE", prim_synth_filter_mode}, {"FILTER-ROUTE", prim_synth_filter_route},
    {"RING-PARTNER", prim_synth_ring_partner}, {"RING-OFF", prim_synth_ring_off},
```

- [ ] **Step 5: Full clean rebuild**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
make clean
make
```
Expected: builds clean, no new warnings (the pre-existing `kernel.elf has a LOAD segment with RWX permissions` linker warning is expected and unrelated).

```bash
cd ../boot
make
```
Expected: builds clean.

- [ ] **Step 6: Re-run the full host test suite (regression check)**

```bash
cd ../kernel
gcc -m32 tests/test_synth.c audio/synth.c -o /tmp/test_synth && /tmp/test_synth
nasm -f elf32 sched/context_switch.asm -o /tmp/cs_host.o
gcc -m32 tests/test_context_switch.c /tmp/cs_host.o -o /tmp/tcs && /tmp/tcs
gcc -m32 tests/test_font.c gfx/font.c -o /tmp/tf && /tmp/tf
gcc -m32 tests/test_scheduler.c sched/scheduler.c /tmp/cs_host.o -o /tmp/tsc && /tmp/tsc
gcc -m32 -Igfx -Igui tests/test_editor.c gui/editor.c -o /tmp/ted && /tmp/ted
rm -f /tmp/cs_host.o
```
Expected: `PASS` from all five.

- [ ] **Step 7: Headless QEMU verification**

Boot with `-device sb16,audiodev=snd0 -audiodev wav,id=snd0,path=<tmp>.wav`, same pattern as the engine's own verification. Drive the FORTH console via the monitor socket and run a script that proves the filter is actually audible, not just wired without error:

```
0 VOICE 1 WAVE 40 ONA 10 50 80 500 ADSR 1 FILTER-ROUTE GATE-ON
```

(voice 0, sawtooth -- a harmonically rich waveform, chosen deliberately so a low-pass filter's effect is clearly measurable, unlike e.g. noise -- at ona 40, routed through the filter, gated on). Then set a low, resonant cutoff and let it sustain:
```
20 FILTER-CUTOFF 10 FILTER-RES 1 FILTER-MODE
```
(cutoff index 20 -- a low, muffled cutoff -- resonance 10, low-pass mode only). Wait at least 1 second of sustain, capture that as WAV region A. Then open the filter up:
```
240 FILTER-CUTOFF
```
Wait another second of sustain, capture that as WAV region B. Then:
```
GATE-OFF
```
Wait for release, `quit` over the monitor for a clean exit.

Inspect the resulting WAV file: confirm region A (low cutoff) and region B (high cutoff) have measurably different spectral content -- a simple, sufficient proxy given no FFT tooling is assumed available: compute each region's zero-crossing rate (how often consecutive samples cross the silence midpoint) -- a low-pass-filtered sawtooth at a low cutoff should have a markedly lower zero-crossing rate (smoother, fewer high-frequency components) than the same signal with the filter opened up wide. A bare non-silence check would not be sufficient evidence the filter did anything (a bug that made the filter a no-op would still pass a non-silence check).

Report the exact frame count, sample rate, and the two regions' zero-crossing-rate (or other spectral proxy) measurements in the task report, following this project's established WAV-inspection convention.

- [ ] **Step 8: Commit**

```bash
git add kernel/forth/forth_hooks.h kernel/kernel.c kernel/forth/forth.c
git commit -m "audio: filter and ring-mod Forth control surface (FILTER-CUTOFF/RES/MODE/ROUTE, RING-PARTNER/OFF)"
```

---

## Task 5: Documentation

**Files:**
- Modify: `docs/IDEAS.md`
- Modify: `docs/BUILD_LOG.md`

**Interfaces:** None (docs only).

- [ ] **Step 1: Update `docs/IDEAS.md`**

Find the Audio entry (already struck through, with sub-project (A) and (B)'s engine noted as done and (B)'s filter explicitly flagged as future work, per the earlier entry's own text). Append a new paragraph to that same entry (do not create a new bullet) recording that the filter and ring modulation have now shipped too. Name the concrete pieces (one shared fixed-point resonant filter, combinable low/band/high-pass, per-voice routing; per-voice assignable ring modulation on the triangle waveform) and state explicitly that sub-project (C) -- a real control surface/note-sequencing language -- remains unbuilt, separate future work (an arpeggio effect, if it lands as its own follow-up before (C) does, would get its own entry/strikethrough at that time -- don't reference it preemptively here). Reference `docs/superpowers/specs/2026-08-23-sid-filter-ringmod-design.md` and this entry's `docs/BUILD_LOG.md` counterpart.

- [ ] **Step 2: Add a `docs/BUILD_LOG.md` entry**

New entry at the end of the file, following this file's established house style (compare the most recent few entries, especially the SID synth engine's own entry, for format: bold sub-section leads, dense technical prose, backtick-quoted symbols/files, a trailing `Files:` line). Must include, using the **actual** results from Tasks 1-4's reports (not invented numbers):

- What was built: the filter core + coefficient tables (Task 1), ring modulation (Task 2), mixer integration (Task 3), and the Forth control surface (Task 4) -- one paragraph each is reasonable, matching the engine entry's own per-task structure.
- How it was verified: the exact host-test pass/fail results from Tasks 1-3 (note `kernel/tests/test_synth.c` grew further, still part of the same host suite alongside the other four host-tested files), the specific empirical stability sweep done while writing this plan (256 cutoffs x 16 resonances x 7 mode combinations x 5000 samples, clamp never triggered), and Task 4's actual headless-QEMU zero-crossing-rate (or whatever spectral proxy was actually used) comparison between a low and high filter cutoff -- copied from that task's real report, not the illustrative numbers in this plan.
- **A real-hardware verification pass is still pending**, same bar every prior piece of this feature has been held to.
- Any deferred findings from Task 4's own review (if the review process surfaces any) recorded as known gaps, following this project's established "known minor gaps, deferred, not fixed here" convention.
- A `Files:` line listing every file touched across all 5 tasks.

- [ ] **Step 3: Commit**

```bash
git add docs/IDEAS.md docs/BUILD_LOG.md
git commit -m "docs: close out the resonant filter + ring modulation follow-up"
```
