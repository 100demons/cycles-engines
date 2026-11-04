/*
 * BIAS: every tuning value in one place.
 *
 * These are the spec's GUESS values ("BIA for Model:Cycles: Technical Spec and Correction Brief"): starting points,
 * not rules. Change them by ear. Each line gives the spec's own starting value in its comment.
 *
 * Both the C source and tools/gen_bias.py read this file: the C turns the linear values into fixed point at compile
 * time (no float at run time), and the generator computes the tables that need exp or log (decay, Liquid, the fold
 * pulse, the spread exponents) from the same numbers. After changing a value, run tools/gen_bias.py.
 * The generator reads only the "#define BIAS_CFG_<NAME> <number>" lines.
 */
#ifndef BIAS_CONFIG_H
#define BIAS_CONFIG_H

/* --- Decay ---------------------------------------------------------------------------------------------------- */
#define BIAS_CFG_DECAY_MIN_S     0.010   /* DECAY 0: time to -60 dB, seconds. Spec: about 10 ms */
#define BIAS_CFG_DECAY_MAX_S     4.0     /* DECAY 126: time to -60 dB, seconds. Spec: about 4 s. DECAY 127 drones */

/* --- Harmonic (COLOR) ----------------------------------------------------------------------------------------- */
#define BIAS_CFG_HARM_EDGE       0.625   /* where osc 3-6 stop stretching and start rising. Spec: about 0.625 */
#define BIAS_CFG_HARM_A_START    0.3     /* osc 3-6 start level. Spec: HARM_A_START, try 0.3 */
#define BIAS_CFG_HARM_D_MIN      0.03    /* osc 3-6 shortest decay, fraction of DECAY. Spec: HARM_D_MIN, try 3 % */
#define BIAS_CFG_HARM_D_MAX      0.70    /* osc 3-6 longest decay, fraction of DECAY. Spec: HARM_D_MAX, try 70 % */
#define BIAS_CFG_HARM_TILT       0.94    /* each higher oscillator's decay x this, so upper modes die a little sooner.
                                          * Spec: "a little faster", no number */
#define BIAS_CFG_HARM_A_FADE     0.02    /* osc 3-6 fade in to the start level over this much of the knob after 0.25,
                                          * so a held drone does not click when they appear. Not in the spec */

/* --- Spread (SWEEP) ------------------------------------------------------------------------------------------- */
#define BIAS_CFG_PRIME_SET_B     0       /* 0: set A (1, 2, 3, 5, 7, 11), the spec's default. 1: set B (1, 3, 5, 7, 11,
                                          * 13), the earlier BIAS builds' set */
#define BIAS_CFG_SPREAD_LOG      1       /* 1: r = h * (p/h)^spread, the spec's default. 0: linear */

/* --- Fold (CONTOUR) ------------------------------------------------------------------------------------------- */
#define BIAS_CFG_FOLD_T_MIN      0.041667 /* lowest folder threshold at 3/4 of the knob. Spec: FOLD_T_MIN, try 1/24 */
#define BIAS_CFG_PULSE_MAX       0.5     /* the top quarter's pulse mix at full CONTOUR. Spec: PULSE_MAX, no number */
#define BIAS_CFG_PULSE_TAU_S     0.0003  /* the pulse's decay time constant, seconds. Spec: about 0.3 ms */

/* --- Liquid (PUNCH on) ---------------------------------------------------------------------------------------- */
#define BIAS_CFG_LIQUID_SEMIS    24      /* pitch envelope depth, semitones. Spec: start at +24 */
#define BIAS_CFG_LIQUID_TAU_S    0.025   /* pitch envelope time constant, seconds. Spec: 25 ms */

/* --- The pitch-locked clock --------------------------------------------------------------------------------- */
#define BIAS_CFG_WINDOW_KHZ      36.0    /* the internal rate is N x f0, N a power of two, in [W/2, W) kHz with W this
                                          * value (24 < W <= 48). Spec: 24 to 48 kHz when CPU allows, 12 to 24 as the
                                          * budget mode */

/* --- Onset (Attack removed: fixed at the spec's noon setting) ------------------------------------------------- */
#define BIAS_CFG_POP_PHASE       0.25    /* phase every oscillator restarts at on a trig, in cycles. Spec: a quarter
                                          * cycle, so a sine starts at its peak and clicks */

/* --- GATE (optional in the spec) ------------------------------------------------------------------------------ */
#define BIAS_CFG_SUSTAIN         0.5     /* GATE held: the envelopes freeze when the fundamental falls to this level.
                                          * The spec allows a hold "while the gate is on"; this is the earlier BIAS
                                          * builds' level */

/* --- Output (the spec: normalise by the sum of levels, then soft-limit; the method is a GUESS) ----------------- */
#define BIAS_CFG_SOFT_K          0.2133  /* the soft limit f(y) = y - K y^3 on |y| <= 1: the earlier BIAS builds' curve */

/* --- Pitch ---------------------------------------------------------------------------------------------------- */
#define BIAS_CFG_PITCH_OFFSET    -24     /* semitones added to the note: the fixed range (the old Bass position). The
                                          * range switch is removed; PITCH covers it */

#endif /* BIAS_CONFIG_H */
