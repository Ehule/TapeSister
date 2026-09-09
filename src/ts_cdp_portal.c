#include "tapesister/cdp_portal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Native command layouts, modes, and scalar ranges are audited against CDP8
   distort/blur/stretch/filter/modify and cdp2k sources. Portal-specific bounds
   protect source compatibility and output size; stable IDs preserve recipes.
   The runtime supplies the DSP, including PVOC analysis/synthesis for spectra. */
#define GROUP(lo, def) {"cycles", "CYCLE GROUP", "NUMBER OF WAVECYCLES IN EACH GROUP", "", TS_PORTAL_INTEGER, lo, 32767, def}
#define SKIP {"skip", "SKIP CYCLES", "LEAVE THESE INITIAL WAVECYCLES UNPROCESSED", "-s", TS_PORTAL_INTEGER, 0, 32767, 0}
#define MULT {"multiplier", "REPEATS", "TIMES EACH WAVECYCLE GROUP IS REPEATED", "", TS_PORTAL_INTEGER, 2, 16, 2}
#define CYCLEFLAG {"cycles", "CYCLE GROUP", "NUMBER OF WAVECYCLES IN EACH GROUP", "-c", TS_PORTAL_INTEGER, 1, 32767, 8}
#define ACUITY {"acuity", "ACUITY", "SMALLER VALUES MAKE A NARROWER, MORE RESONANT FILTER", "", TS_PORTAL_REAL, .05, 1, .5}
#define FILTER_GAIN {"gain", "OUTPUT GAIN", "LINEAR OUTPUT MULTIPLIER; RESONANCE CAN STILL BOOST LEVEL", "", TS_PORTAL_REAL, .01, 1, .5}
#define FILTER_FREQ {"frequency", "FREQUENCY HZ", "20 TO 6000 HZ; ALSO LIMITED TO ONE SIXTH OF SOURCE RATE", "", TS_PORTAL_REAL, 20, 6000, 1000}
#define FILTER_TAIL {"tail", "TAIL SECONDS", "APPEND 0.01 TO 2 SECONDS FOR FILTER DECAY", "-t", TS_PORTAL_REAL, .01, 2, .25}
#define CHORUS_AMP {"amplitude", "AMP SCATTER", "MAXIMUM RANDOM PARTIAL-AMPLITUDE RATIO; 1 IS UNCHANGED", "", TS_PORTAL_REAL, 1, 1028, 1.5}
#define CHORUS_FREQ {"frequency", "FREQ SCATTER", "FREQUENCY SCATTER RATIO; 1 DISABLES SCATTER BUT CDP STILL RE-BINS PARTIALS", "", TS_PORTAL_REAL, 1, 4, 1.05}
#define EQ_GAIN {"db", "BOOST / CUT DB", "GAIN IN THE SELECTED FREQUENCY REGION; -24 TO +24 DB", "", TS_PORTAL_REAL, -24, 24, -6}
#define EQ_FREQ {"frequency", "FREQUENCY HZ", "FILTER FREQUENCY; MUST BE BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 1000}
#define EQ_PRESCALE {"prescale", "INPUT GAIN", "SCALE INPUT BEFORE EQ; 0.01 TO 1", "-s", TS_PORTAL_REAL, .01, 1, .5}
#define SPECTRUM_PARAMS { \
    {"divide", "DIVIDE HZ", "SPECTRAL SPLIT; MUST FIT THE STRETCH RATIO AND ANALYSIS BINS", "", TS_PORTAL_REAL, 500, 5000, 1500}, \
    {"ratio", "STRETCH RATIO", "PARTIAL-FREQUENCY RATIO; 1 MAKES NO CHANGE AND IS REJECTED BY CDP", "", TS_PORTAL_REAL, .25, 4, 1.4}, \
    {"exponent", "EXPONENT", "SHAPE OF THE FREQUENCY-STRETCH CURVE", "", TS_PORTAL_REAL, .25, 8, 1}, \
    {"depth", "DEPTH", "STRETCH DEPTH; CDP REQUIRES A POSITIVE VALUE", "-d", TS_PORTAL_REAL, .01, 1, 1} }
#define SWEEP_PARAMS {ACUITY, FILTER_GAIN, \
    {"low", "LOW HZ", "LOWER SWEEP FREQUENCY; MUST BE BELOW HIGH HZ", "", TS_PORTAL_REAL, 20, 6000, 200}, \
    {"high", "HIGH HZ", "UPPER SWEEP FREQUENCY; MAXIMUM ONE SIXTH OF SOURCE RATE", "", TS_PORTAL_REAL, 20, 6000, 3000}, \
    {"rate", "SWEEP RATE HZ", "SWEEP CYCLES PER SECOND; ZERO HOLDS THE STARTING PHASE", "", TS_PORTAL_REAL, 0, 20, .5}, \
    FILTER_TAIL, {"phase", "START PHASE", "0 STARTS LOW; 0.5 STARTS HIGH; 1 RETURNS LOW", "-p", TS_PORTAL_REAL, 0, 1, 0} }
/* CDP8 env/ap_envel.c, envfuncs.c, envprepro.c, and cdp2k/tklib1.c. */
#define ENV_WINDOW {"window", "WINDOW MS", "PEAK-ENVELOPE WINDOW; CDP ROUNDS TO SUPPORTED SAMPLE BLOCKS", "", TS_PORTAL_REAL, 5, 200, 20}
#define ENV_GROUP {"cycles", "CYCLE GROUP", "WAVECYCLES PER ENVELOPE; MUST FIT SOURCE", "", TS_PORTAL_INTEGER, 1, 1000, 8}
#define ENV_EXP {"exponent", "EXPONENT", "CURVE EXPONENT; 1 IS LINEAR", "-e", TS_PORTAL_REAL, .125, 8, 1}
#define ENV_GATE {"gate", "GATE LEVEL", "LINEAR LEVEL BELOW WHICH THE ENVELOPE IS SILENCED", "", TS_PORTAL_REAL, 0, 1, .02}
#define ENV_SMOOTH {"smooth", "SMOOTH WINDOWS", "REMOVE SHORT LOW-LEVEL SEGMENTS; ZERO DISABLES SMOOTHING", "", TS_PORTAL_INTEGER, 0, 64, 0}
#define FADE_IN {"in", "FADE IN SEC", "START FADE DURATION; START AND END FADES MUST NOT OVERLAP", "", TS_PORTAL_REAL, 0, 5, .01}
#define FADE_OUT {"out", "FADE OUT SEC", "END FADE DURATION; START AND END FADES MUST NOT OVERLAP", "", TS_PORTAL_REAL, 0, 5, .02}
static const TsPortalProcess processes[] = {
    {"distort.reverse", "CYCLE REVERSE", "Reverse groups of wavecycles. Larger groups reveal reversed gestures; small groups reshape the timbre.", "reverse", 1, 0, 1, 0, {GROUP(1,8)}, TS_PORTAL_WAVESET, "distort"},
    {"distort.repeat", "CYCLE REPEAT", "Repeat groups of wavecycles to stretch the sound. Group size changes the texture of the repetition.", "repeat", 1, 0, 3, 1, {MULT, CYCLEFLAG, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.repeat2", "REPEAT FIXED", "Repeat wavecycle groups without stretching the overall duration. Listen for changes in local articulation.", "repeat2", 1, 0, 3, 0, {MULT, CYCLEFLAG, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.interpolate", "INTERPOLATE", "Stretch by repeating wavecycles and interpolating between them. Compare this with ordinary cycle repeat.", "interpolate", 1, 0, 2, 1, {MULT, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.multiply", "FREQ MULTIPLY", "Multiply wavecycle frequency by an integer. This is waveset distortion, not a transparent pitch shifter.", "multiply", 1, 0, 2, 0,
        {{"factor", "MULTIPLIER", "INTEGER FREQUENCY MULTIPLIER", "", TS_PORTAL_INTEGER, 2, 16, 2},
         {"smooth", "SMOOTHING", "ENABLE CDP SMOOTHING IF GLITCHES APPEAR", "-s", TS_PORTAL_SWITCH, 0, 1, 1}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.divide", "FREQ DIVIDE", "Divide wavecycle frequency by an integer. Optional interpolation gives a different, often cleaner texture.", "divide", 1, 0, 2, 0,
        {{"factor", "DIVISOR", "INTEGER FREQUENCY DIVISOR", "", TS_PORTAL_INTEGER, 2, 16, 2},
         {"interpolate", "INTERPOLATE", "INTERPOLATE WAVEFORMS DURING DIVISION", "-i", TS_PORTAL_SWITCH, 0, 1, 1}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.omit", "CYCLE OMIT", "Replace A out of every B wavecycles with silence. A must remain smaller than B. Inspect the new gaps.", "omit", 1, 0, 2, 0,
        {{"omit", "OMIT A", "CYCLES TO SILENCE IN EACH GROUP", "", TS_PORTAL_INTEGER, 1, 32767, 1},
         {"every", "EVERY B", "TOTAL CYCLES PER GROUP; MUST EXCEED A", "", TS_PORTAL_INTEGER, 2, 32768, 4}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.average", "CYCLE AVERAGE", "Average shapes across successive wavecycles. This changes waveform detail, not simply the volume envelope.", "average", 1, 0, 3, 0,
        {GROUP(2,5), {"max_length", "MAX WAVE SEC", "MAXIMUM PERMITTED WAVECYCLE LENGTH IN SECONDS", "-m", TS_PORTAL_REAL, 0.001, 1, 0.1}, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.delete.1", "KEEP ONE", "Retain one wavecycle in every group, deleting the others. The result becomes shorter.", "delete", 1, 1, 2, 1, {GROUP(2,3), SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.delete.2", "KEEP LOUDEST", "Retain only the strongest wavecycle in each group. The result becomes shorter and differently articulated.", "delete", 1, 2, 2, 1, {GROUP(2,3), SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.delete.3", "DROP WEAKEST", "Delete the weakest wavecycle in each group. Compare the changed duration and transient structure.", "delete", 1, 3, 2, 1, {GROUP(2,3), SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.5", "HALF INVERT", "Invert half cycles to change the waveform contour. This mode has no numerical parameters.", "reform", 1, 5, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    /* CDP8 blur/ap_blur.c, stretch/ap_stretch.c and cdp2k/tklib1.c.
       Fixed 1024-point PVOC analysis, overlap 3, hop 128, 513 bins.
       Scalar native parameters only; no breakpoint-file input in this batch. */
    {"blur.blur", "SPECTRAL BLUR", "Average spectra across time. Longer window groups soften transients into a spectral haze. Analysis and resynthesis are automatic.", "blur", 1, 0, 1, 0,
        {{"windows", "BLUR WINDOWS", "SPECTRAL WINDOWS TO AVERAGE; MUST FIT THE SOURCE", "", TS_PORTAL_INTEGER, 1, 4096, 8}}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.suppress", "SUPPRESS PARTIALS", "Remove the loudest spectral partials in each frame, revealing quieter components underneath. Large counts can intentionally approach silence.", "suppress", 1, 0, 1, 0,
        {{"partials", "PARTIALS", "NUMBER OF LOUDEST PARTIALS TO REJECT (513 ANALYSIS BINS)", "", TS_PORTAL_INTEGER, 1, 513, 8}}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.5", "SPECTRAL CHORUS", "Scatter partial amplitudes and frequencies. Values near one are subtle; larger amounts become grainy or noisy. Random results can vary each render.", "chorus", 1, 5, 2, 0,
        {{"amplitude", "AMP SCATTER", "MAXIMUM RANDOM PARTIAL-AMPLITUDE RATIO; 1 IS UNCHANGED", "", TS_PORTAL_REAL, 1, 1028, 1.5},
         {"frequency", "FREQ SCATTER", "FREQUENCY SCATTER RATIO; 1 DISABLES SCATTER BUT CDP STILL RE-BINS PARTIALS", "", TS_PORTAL_REAL, 1, 4, 1.01}}, TS_PORTAL_SPECTRAL, "blur"},
    {"stretch.time.1", "SPECTRAL TIME", "Stretch or compress time while retaining pitch. Ratio two doubles the duration. Spectral resynthesis can soften transients; compare source and result.", "time", 1, 1, 1, 1,
        {{"ratio", "TIME RATIO", "OUTPUT DURATION MULTIPLIER; PORTAL RANGE 0.25 TO 16", "", TS_PORTAL_REAL, .25, 16, 1.5}}, TS_PORTAL_SPECTRAL, "stretch"},
    /* CDP8 modify/ap_modify.c usage2 and cdp2k/tklib1.c MOD_PITCH /
       MOD_RADICAL ranges. Speed/depth limits are bounded Portal ranges. */
    {"modify.speed.1", "TAPE SPEED", "Change playback speed and pitch together, like changing tape speed. Faster makes the sound shorter and higher; slower makes it longer and lower.", "speed", 1, 1, 1, 1,
        {{"speed", "SPEED RATIO", "PLAYBACK SPEED MULTIPLIER; PORTAL RANGE 0.125 TO 8", "", TS_PORTAL_REAL, .125, 8, .75}}, TS_PORTAL_TIME, "modify"},
    {"modify.speed.2", "TAPE TRANSPOSE", "Transpose by semitones using resampling. Pitch and duration change together. Unlike spectral time stretch, this does not preserve pitch while changing duration.", "speed", 1, 2, 1, 1,
        {{"semitones", "SEMITONES", "RESAMPLED TRANSPOSITION; PORTAL RANGE -36 TO +36 SEMITONES", "", TS_PORTAL_REAL, -36, 36, -7}}, TS_PORTAL_TIME, "modify"},
    {"modify.speed.6", "TAPE VIBRATO", "Oscillate playback speed to bend pitch continuously. Slow rates give tape-like drift; higher rates and depths create rapid pitch motion. Duration can change.", "speed", 1, 6, 2, 1,
        {{"rate", "RATE HZ", "VIBRATO CYCLES PER SECOND; CDP RANGE 0 TO 120 HZ", "", TS_PORTAL_REAL, 0, 120, 5},
         {"depth", "DEPTH SEMITONES", "VIBRATO DEPTH; PORTAL RANGE 0 TO 24 SEMITONES", "", TS_PORTAL_REAL, 0, 24, .667}}, TS_PORTAL_TIME, "modify"},
    {"modify.radical.1", "SOUND REVERSE", "Reverse the entire source snapshot. This reverses the order of larger gestures and transients, rather than reversing individual wavecycle groups.", "radical", 1, 1, 0, 0, {{0}}, TS_PORTAL_TIME, "modify"},
    /* CDP8 filter/ap_filter.c, filters0.c, fltpcon.c, cdp2k/tklib1.c,
       include/filtcon.h. Bounded scalar controls; positive explicit tails avoid
       CDP's automatic, potentially long tail when -t0 is supplied. */
    {"filter.variable.1", "NOTCH FILTER", "Carve a band out while keeping surrounding frequencies. Lower acuity narrows the notch. Compare with Band Pass at the same frequency.", "variable", 1, 1, 4, 1,
        {ACUITY, FILTER_GAIN, FILTER_FREQ, FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    {"filter.variable.2", "BAND PASS", "Isolate a frequency band to reveal a tone inside the source. Lower acuity makes it narrower and more resonant. Reduce output gain if needed.", "variable", 1, 2, 4, 1,
        {ACUITY, FILTER_GAIN, FILTER_FREQ, FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    {"filter.variable.3", "LOW PASS", "Keep low frequencies and soften the highs. Lower acuity adds resonance at the cutoff and can turn a transient into a ringing tone.", "variable", 1, 3, 4, 1,
        {ACUITY, FILTER_GAIN, FILTER_FREQ, FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    {"filter.variable.4", "HIGH PASS", "Remove lows to uncover upper harmonics and noisy detail. Lower acuity adds resonance at the cutoff. Compare with Low Pass on the same source.", "variable", 1, 4, 4, 1,
        {ACUITY, FILTER_GAIN, FILTER_FREQ, FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    {"filter.sweeping.2", "SWEEPING BAND", "Move a resonant band between two frequencies. Slow sweeps reveal layers; fast sweeps create rhythm. Scroll controls for rate, tail, and phase.", "sweeping", 1, 2, 7, 1,
        {ACUITY, FILTER_GAIN,
         {"low", "LOW HZ", "LOWER SWEEP FREQUENCY; MUST BE BELOW HIGH HZ", "", TS_PORTAL_REAL, 20, 6000, 200},
         {"high", "HIGH HZ", "UPPER SWEEP FREQUENCY; MAXIMUM ONE SIXTH OF SOURCE RATE", "", TS_PORTAL_REAL, 20, 6000, 3000},
         {"rate", "SWEEP RATE HZ", "SWEEP CYCLES PER SECOND; ZERO HOLDS THE STARTING PHASE", "", TS_PORTAL_REAL, 0, 20, .5},
         FILTER_TAIL,
         {"phase", "START PHASE", "0 STARTS LOW; 0.5 STARTS HIGH; 1 RETURNS LOW", "-p", TS_PORTAL_REAL, 0, 1, 0}}, TS_PORTAL_FILTER, "filter"},
    {"filter.phasing.2", "PHASING", "Mix with a delayed allpass signal for comb-like coloration. Gain changes interference; delay sets its spacing. Delay stays fixed in this mode.", "phasing", 1, 2, 3, 1,
        {{"gain", "PHASING GAIN", "ALLPASS FEEDBACK COEFFICIENT; PORTAL RANGE -0.95 TO 0.95", "", TS_PORTAL_REAL, -.95, .95, .6},
         {"delay", "DELAY MS", "0.1 TO 50 MS; MUST NOT EXCEED HALF THE SOURCE DURATION", "", TS_PORTAL_REAL, .1, 50, 3},
         FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    /* CDP8 modify/ap_modify.c, brapcon.c, granula1.c, cdp2k/tklib1.c
       and include/modicon.h. Native mono modes with 5 ms start/end splices
       and fixed 0.5 scatter. Grain length is 50 ms except in mode 4. */
    {"modify.brassage.1", "GRANULAR PITCH", "Shift pitch in overlapping 50 ms grains while keeping roughly the same duration. Compare with Tape Transpose. Grain scatter varies each render.", "brassage", 1, 1, 1, 1,
        {{"semitones", "SEMITONES", "GRANULAR TRANSPOSITION; PORTAL RANGE -24 TO +24 SEMITONES", "", TS_PORTAL_REAL, -24, 24, 7}}, TS_PORTAL_GRAIN, "modify"},
    {"modify.brassage.2", "GRANULAR TIME", "Change duration using overlapping 50 ms grains. Velocity 0.5 roughly doubles time; 2 halves it. Pitch is retained, with grain texture and scatter.", "brassage", 1, 2, 1, 1,
        {{"velocity", "VELOCITY", "INPUT ADVANCE SPEED; 0.5 DOUBLES TIME, 2 HALVES IT; ZERO EXCLUDED", "", TS_PORTAL_REAL, .125, 8, .5}}, TS_PORTAL_GRAIN, "modify"},
    {"modify.brassage.4", "GRAIN SCRAMBLE", "Rebuild the sound from grains selected behind the current source position. Grain size sets detail; lookback sets how far it reaches into the past.", "brassage", 1, 4, 2, 1,
        {{"grain_ms", "GRAIN MS", "GRAIN LENGTH; 12 TO 250 MS, LONGER THAN THE TWO 5 MS SPLICES", "", TS_PORTAL_REAL, 12, 250, 50},
         {"lookback_ms", "LOOKBACK MS", "RANDOM BACKWARD SEARCH; MUST NOT EXCEED TWICE SOURCE DURATION", "-r", TS_PORTAL_REAL, 0, 2000, 250}}, TS_PORTAL_GRAIN, "modify"},
    {"modify.brassage.5", "GRAIN DENSITY", "Break the source into scattered 50 ms grains. Density below one leaves gaps; higher values overlap grains. Compare the gaps in the result waveform.", "brassage", 1, 5, 1, 1,
        {{"density", "DENSITY", "GRAIN OVERLAP; BELOW 1 LEAVES GAPS; PORTAL RANGE 0.125 TO 2", "", TS_PORTAL_REAL, .125, 2, .5}}, TS_PORTAL_GRAIN, "modify"},
    /* Extended spectral modes: blur/ap_blur.c, stretch/ap_stretch.c,
       cdp2k/formantsg.c, cdp2k/tklib1.c and include/speccon.h. */
    {"blur.avrg", "SPECTRAL AVERAGE", "Smooth energy across neighboring frequency bins. Larger odd groups soften spectral detail. Compare with Spectral Blur, which averages through time.", "avrg", 1, 0, 1, 0,
        {{"bins", "NEIGHBOR BINS", "ODD NUMBER OF ADJACENT ANALYSIS BINS TO AVERAGE", "", TS_PORTAL_ODD_INTEGER, 3, 511, 13}}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.1", "AMPLITUDE CHORUS", "Randomise the amplitudes of partials while retaining their frequencies. Raise scatter to make the spectral balance flicker between frames.", "chorus", 1, 1, 1, 0,
        {CHORUS_AMP}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.2", "FREQUENCY CHORUS", "Scatter partial frequencies upward and downward. Amplitudes are not randomised. Small ratios detune; large ratios break apart the harmonic structure.", "chorus", 1, 2, 1, 0,
        {CHORUS_FREQ}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.3", "CHORUS UP", "Scatter partial frequencies upward only. A ratio of one leaves frequencies unchanged; larger ratios open the sound into an inharmonic shimmer.", "chorus", 1, 3, 1, 0,
        {CHORUS_FREQ}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.4", "CHORUS DOWN", "Scatter partial frequencies downward only. Compare with Chorus Up on the same source to hear how the direction changes the spectral texture.", "chorus", 1, 4, 1, 0,
        {CHORUS_FREQ}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.6", "AMP + CHORUS UP", "Randomise partial amplitudes and scatter their frequencies upward. The two controls independently change spectral balance and detuning.", "chorus", 1, 6, 2, 0,
        {CHORUS_AMP, CHORUS_FREQ}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.chorus.7", "AMP + CHORUS DOWN", "Randomise partial amplitudes and scatter their frequencies downward. Compare with Spectral Chorus for scatter in both directions.", "chorus", 1, 7, 2, 0,
        {CHORUS_AMP, CHORUS_FREQ}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.noise", "SPECTRAL NOISE", "Introduce noise into the existing spectrum. Zero leaves it unchanged; one saturates it with noise. Useful for moving pitched material toward breath or hiss.", "noise", 1, 0, 1, 0,
        {{"amount", "NOISE AMOUNT", "0 LEAVES THE SPECTRUM UNCHANGED; 1 SATURATES IT WITH NOISE", "", TS_PORTAL_REAL, 0, 1, .25}}, TS_PORTAL_SPECTRAL, "blur"},
    {"blur.spread", "SPECTRAL SPREAD", "Spread spectral peaks using a frequency-based formant envelope. Envelope bins set its resolution; spread moves toward a noisy texture.", "spread", 1, 0, 2, 0,
        {{"bins", "ENVELOPE BINS", "FREQUENCY-WISE FORMANT ENVELOPE GROUPING; NATIVE -f CONTROL", "-f", TS_PORTAL_INTEGER, 1, 64, 4},
         {"spread", "SPREAD", "DEGREE OF SPECTRAL PEAK SPREADING", "-s", TS_PORTAL_REAL, 0, 1, .5}}, TS_PORTAL_SPECTRAL, "blur"},
    {"stretch.spectrum.1", "STRETCH ABOVE", "Warp partial frequencies above the split frequency. Ratio changes tuning, exponent shapes the curve, and depth controls the amount. Duration is retained.", "spectrum", 1, 1, 4, 0,
        SPECTRUM_PARAMS, TS_PORTAL_SPECTRAL, "stretch"},
    {"stretch.spectrum.2", "STRETCH BELOW", "Warp partial frequencies below the split frequency. Explore inharmonic bass structures while leaving the upper region in place. Duration is retained.", "spectrum", 1, 2, 4, 0,
        SPECTRUM_PARAMS, TS_PORTAL_SPECTRAL, "stretch"},
    /* Complete fixed-EQ, sweeping-filter, and allpass mode sets. */
    {"filter.fixed.1", "LOW SHELF EQ", "Boost or cut frequencies below the shelf. Positive dB adds weight; negative dB thins the low end. Input gain leaves room for boosts.", "fixed", 1, 1, 4, 1,
        {EQ_GAIN, EQ_FREQ, FILTER_TAIL, EQ_PRESCALE}, TS_PORTAL_FILTER, "filter"},
    {"filter.fixed.2", "HIGH SHELF EQ", "Boost or cut frequencies above the shelf. Use it to brighten a texture or soften its upper edge. Input gain leaves room for boosts.", "fixed", 1, 2, 4, 1,
        {EQ_GAIN, EQ_FREQ, FILTER_TAIL, EQ_PRESCALE}, TS_PORTAL_FILTER, "filter"},
    {"filter.fixed.3", "PEAK EQ", "Boost or cut a band around the center frequency. Bandwidth sets the region in Hz. Compare narrow resonant accents with broader tonal changes.", "fixed", 1, 3, 5, 1,
        {{"bandwidth", "BANDWIDTH HZ", "WIDTH OF THE EQ BAND; MUST BE BELOW ONE QUARTER OF SOURCE RATE", "", TS_PORTAL_REAL, 20, 4000, 500},
         EQ_GAIN, EQ_FREQ, FILTER_TAIL, EQ_PRESCALE}, TS_PORTAL_FILTER, "filter"},
    {"filter.sweeping.1", "SWEEPING NOTCH", "Move a rejected band through the sound. Lower acuity narrows the notch. Scroll for sweep rate, tail, and starting phase.", "sweeping", 1, 1, 7, 1,
        SWEEP_PARAMS, TS_PORTAL_FILTER, "filter"},
    {"filter.sweeping.3", "SWEEPING LOW PASS", "Move a resonant low-pass cutoff between two frequencies. Slow rates open and close the texture; fast rates add rhythmic movement.", "sweeping", 1, 3, 7, 1,
        SWEEP_PARAMS, TS_PORTAL_FILTER, "filter"},
    {"filter.sweeping.4", "SWEEPING HIGH PASS", "Move a resonant high-pass cutoff between two frequencies. Sweep away the low body of a sound to reveal its upper detail.", "sweeping", 1, 4, 7, 1,
        SWEEP_PARAMS, TS_PORTAL_FILTER, "filter"},
    {"filter.phasing.1", "ALLPASS SHIFT", "Shift phase through a delayed allpass network. Unlike Phasing, this mode does not mix the dry sound back in. The decay tail is included.", "phasing", 1, 1, 3, 1,
        {{"gain", "FEEDBACK", "ALLPASS FEEDBACK COEFFICIENT; -0.95 TO 0.95", "", TS_PORTAL_REAL, -.95, .95, .6},
         {"delay", "DELAY MS", "0.1 TO 50 MS; MUST NOT EXCEED HALF THE SOURCE DURATION", "", TS_PORTAL_REAL, .1, 50, 3},
         FILTER_TAIL}, TS_PORTAL_FILTER, "filter"},
    /* Lo-fi / modulation: modify/radical.c and tklib1.c. */
    {"modify.radical.4", "BIT + RATE REDUCE", "Reduce amplitude resolution and average blocks of samples into held values. Larger blocks create a lower-rate texture. A partial final block is dropped.", "radical", 1, 4, 2, 1,
        {{"bits", "BITS", "NATIVE AMPLITUDE RESOLUTION; 1 TO 16 BITS", "", TS_PORTAL_INTEGER, 1, 16, 8},
         {"division", "RATE DIVISION", "SAMPLES PER AVERAGED AND HELD BLOCK; 1 TO 256", "", TS_PORTAL_INTEGER, 1, 256, 4}}, TS_PORTAL_LOFI, "modify"},
    {"modify.radical.7", "QUANTISE", "Apply mid-rise amplitude quantisation. Low bit counts add stepped distortion. This mode changes amplitude resolution without reducing the sample rate.", "radical", 1, 7, 1, 0,
        {{"bits", "BITS", "MID-RISE QUANTISATION RESOLUTION; 1 TO 16 BITS", "", TS_PORTAL_INTEGER, 1, 16, 6}}, TS_PORTAL_LOFI, "modify"},
    {"modify.radical.5", "RING MODULATE", "Multiply the source by a sine wave to create sum and difference frequencies. Low rates pulse; higher rates give metallic or inharmonic sidebands.", "radical", 1, 5, 1, 0,
        {{"frequency", "MODULATION HZ", "SINE MODULATOR FREQUENCY; MUST BE BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, .1, 12000, 150}}, TS_PORTAL_LOFI, "modify"},
    {"distort.reform.1", "FIXED SQUARE", "Replace half-cycles with a fixed-level square wave. This discards the amplitude envelope and can be loud. Review the peak before applying.", "reform", 1, 1, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.2", "SQUARE WAVE", "Square each half-cycle while following its peak level. Adds strong upper harmonics while retaining the changing amplitude of the source.", "reform", 1, 2, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.3", "FIXED TRIANGLE", "Replace half-cycles with fixed-level triangles. The original amplitude envelope is discarded. Review the output peak before applying.", "reform", 1, 3, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.4", "TRIANGLE WAVE", "Reshape half-cycles into triangles following their peak levels. Turns irregular wave shapes into a more geometric contour.", "reform", 1, 4, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.6", "CLICK STREAM", "Convert half-cycles into short clicks. Reveals the zero-crossing rhythm as a sharp, dense impulse texture.", "reform", 1, 6, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.7", "SINE WAVE", "Reshape half-cycles into sinusoidal curves following their peak levels. Simplifies the contour without imposing one global pitch.", "reform", 1, 7, 0, 0, {{0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.reform.8", "CONTOUR EXAGGERATE", "Apply an exponent to each half-cycle contour. Explore flattened or sharpened wave shapes; 1 preserves the contour.", "reform", 1, 8, 1, 0,
        {{"exponent", "EXAGGERATION", "HALF-CYCLE CONTOUR EXPONENT; 1 IS NEUTRAL", "", TS_PORTAL_REAL, .125, 8, 2}}, TS_PORTAL_WAVESET, "distort"},
    {"modify.loudness.1", "LINEAR GAIN", "Multiply amplitude by a positive gain. Values below 1 attenuate; values above 1 amplify. Review the output peak before applying.", "loudness", 1, 1, 1, 0,
        {{"gain", "GAIN", "POSITIVE AMPLITUDE MULTIPLIER; CDP REJECTS ZERO GAIN", "", TS_PORTAL_REAL, .001, 4, .5}}, TS_PORTAL_LEVEL, "modify"},
    {"modify.loudness.2", "DB GAIN", "Raise or lower the level in decibels. Positive values amplify; negative values attenuate. Zero retains the level.", "loudness", 1, 2, 1, 0,
        {{"db", "GAIN DB", "AMPLITUDE GAIN IN DECIBELS", "", TS_PORTAL_REAL, -24, 24, -6}}, TS_PORTAL_LEVEL, "modify"},
    {"modify.loudness.3", "RAISE PEAK", "CDP normalise raises the peak to a target. It rejects a sound already at or above that level. Use Set Peak when attenuation is needed.", "loudness", 1, 3, 1, 0,
        {{"level", "TARGET PEAK", "TARGET MUST EXCEED SOURCE PEAK; SILENCE CANNOT BE NORMALISED", "-l", TS_PORTAL_REAL, .01, 1, .9}}, TS_PORTAL_LEVEL, "modify"},
    {"modify.loudness.4", "SET PEAK", "Scale a non-silent sound up or down to the target peak. CDP rejects a source already at the requested level.", "loudness", 1, 4, 1, 0,
        {{"level", "TARGET PEAK", "LINEAR PEAK TARGET; 1 IS FULL SCALE", "-l", TS_PORTAL_REAL, .01, 1, .9}}, TS_PORTAL_LEVEL, "modify"},
    {"modify.loudness.6", "INVERT POLARITY", "Reverse the sign of every sample. The duration and amplitude envelope remain the same. Useful for phase comparisons and later layering.", "loudness", 1, 6, 0, 0, {{0}}, TS_PORTAL_LEVEL, "modify"},
    {"modify.revecho.1", "FEEDBACK DELAY", "Add a fixed delay with feedback and a dry/wet mix. Short times create resonance; longer times create echoes. Scroll for tail, input gain, and dry inversion.", "revecho", 1, 1, 6, 1,
        {{"delay", "DELAY MS", "DELAY TIME; AT LEAST ONE SOURCE SAMPLE", "", TS_PORTAL_REAL, .1, 2000, 180},
         {"mix", "WET MIX", "0 IS DRY; 1 IS DELAYED SIGNAL ONLY", "", TS_PORTAL_REAL, 0, 1, .4},
         {"feedback", "FEEDBACK", "FEEDBACK COEFFICIENT; NEGATIVE VALUES INVERT EACH REPEAT", "", TS_PORTAL_REAL, -.95, .95, .35},
         {"tail", "TAIL SECONDS", "EXTRA OUTPUT TIME FOR ECHO DECAY", "", TS_PORTAL_REAL, 0, 4, 1},
         {"prescale", "INPUT GAIN", "INPUT LEVEL BEFORE THE NATIVE FEEDBACK COMPENSATION", "-p", TS_PORTAL_REAL, .01, 1, .5},
         {"invert", "INVERT DRY", "REVERSE THE DRY SIGNAL FOR PHASING EFFECTS", "-i", TS_PORTAL_SWITCH, 0, 1, 0}}, TS_PORTAL_DELAY, "modify"},
    {"distort.envel.1", "CYCLE RISE", "Apply a rising envelope to each group of wavecycles. Group sets the rhythm, trough sets the starting level, and exponent bends the rise.", "envel", 1, 1, 3, 0,
        {ENV_GROUP, {"trough", "TROUGH LEVEL", "LEVEL AT THE START OF EACH RISE", "-t", TS_PORTAL_REAL, 0, 1, .1}, ENV_EXP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.envel.2", "CYCLE FALL", "Apply a falling envelope to each group of wavecycles. Use small groups for rough buzzing or larger groups for repeated decays.", "envel", 1, 2, 3, 0,
        {ENV_GROUP, {"trough", "TROUGH LEVEL", "LEVEL AT THE END OF EACH FALL", "-t", TS_PORTAL_REAL, 0, 1, .1}, ENV_EXP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.envel.3", "CYCLE TROUGH", "Dip the amplitude inside every group of wavecycles. Trough controls the depth of each dip and exponent controls its contour.", "envel", 1, 3, 3, 0,
        {ENV_GROUP, {"trough", "TROUGH LEVEL", "LOWEST LEVEL WITHIN EACH ENVELOPE GROUP", "", TS_PORTAL_REAL, 0, 1, .1}, ENV_EXP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.fractal", "CYCLE FRACTAL", "Superimpose miniature wavecycle copies on the source. Scale controls their size; copy gain and input gain control their contribution and headroom.", "fractal", 1, 0, 3, 0,
        {{"scale", "SCALE DIVISION", "INTEGER SHRINK FACTOR FOR THE COPIES", "", TS_PORTAL_INTEGER, 2, 64, 4},
         {"gain", "COPY GAIN", "COPY AMPLITUDE RELATIVE TO SOURCE", "", TS_PORTAL_REAL, .01, 4, .5},
         {"prescale", "INPUT GAIN", "SCALE THE SOURCE BEFORE ADDING COPIES", "-p", TS_PORTAL_REAL, .01, 1, .5}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.replace", "STRONGEST CYCLE", "Replace each group with copies of its strongest wavecycle, measured by summed absolute amplitude. Cycle lengths can change the resulting duration.", "replace", 1, 0, 2, 1,
        {{"cycles", "CYCLE GROUP", "GROUP TO SEARCH FOR THE STRONGEST CYCLE", "", TS_PORTAL_INTEGER, 2, 64, 4}, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.telescope", "CYCLE TELESCOPE", "Contract a group of wavecycles into one composite cycle. Choose the longest cycle length or the group's average length as the destination.", "telescope", 1, 0, 3, 1,
        {{"cycles", "CYCLE GROUP", "WAVECYCLES TO COMBINE INTO ONE", "", TS_PORTAL_INTEGER, 2, 64, 4}, SKIP,
         {"average", "AVERAGE LENGTH", "OFF USES LONGEST CYCLE; ON USES AVERAGE CYCLE LENGTH", "-a", TS_PORTAL_SWITCH, 0, 1, 0}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.pitch", "CYCLE PITCH WARP", "Randomly bend wavecycle pitch up and down. Octave range controls the excursion; cycle span controls how often new random targets are chosen.", "pitch", 1, 0, 3, 1,
        {{"octaves", "OCTAVE RANGE", "MAXIMUM RANDOM SHIFT UP OR DOWN", "", TS_PORTAL_REAL, .01, 2, .25},
         {"cycles", "CYCLE SPAN", "MAXIMUM CYCLES BETWEEN RANDOM PITCH TARGETS", "-c", TS_PORTAL_INTEGER, 2, 64, 8}, SKIP}, TS_PORTAL_WAVESET, "distort"},
    {"distort.overload.1", "NOISE OVERLOAD", "Clip peaks with a random noise pattern. Threshold selects the peaks; depth roughens their clipped tops. CDP applies its native level scaling.", "overload", 1, 1, 2, 0,
        {{"threshold", "CLIP LEVEL", "LINEAR AMPLITUDE WHERE OVERLOAD BEGINS", "", TS_PORTAL_REAL, .01, 1, .2},
         {"depth", "PATTERN DEPTH", "NOISE DEPTH ON CLIPPED PEAKS", "", TS_PORTAL_REAL, 0, 1, .5}}, TS_PORTAL_WAVESET, "distort"},
    {"distort.overload.2", "SINE OVERLOAD", "Impose a sine pattern on overloaded peaks. The pattern restarts on each clipped region; it is distinct from whole-signal ring modulation.", "overload", 1, 2, 3, 0,
        {{"threshold", "CLIP LEVEL", "LINEAR AMPLITUDE WHERE OVERLOAD BEGINS", "", TS_PORTAL_REAL, .01, 1, .2},
         {"depth", "PATTERN DEPTH", "SINE DEPTH ON CLIPPED PEAKS", "", TS_PORTAL_REAL, 0, 1, .5},
         {"frequency", "PATTERN HZ", "SINE PATTERN FREQUENCY; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, .1, 12000, 500}}, TS_PORTAL_WAVESET, "distort"},
    {"envel.warp.2", "ENVELOPE REVERSE", "Reverse the extracted loudness contour while the sound itself continues forward. Compare with Sound Reverse to hear the difference in attacks and texture.", "warp", 1, 2, 1, 0,
        {ENV_WINDOW}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.3", "ENVELOPE EXAGGERATE", "Raise envelope levels to a power. Exponents above 1 deepen the contrast; values below 1 bring quieter material forward. One retains the contour.", "warp", 1, 3, 2, 0,
        {ENV_WINDOW, {"exponent", "EXPONENT", "ENVELOPE POWER; 1 RETAINS THE CONTOUR", "", TS_PORTAL_REAL, .125, 8, 2}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.5", "ENVELOPE LIFT", "Add a fixed amount to the extracted envelope. Raises quiet material within the sound. Silence cannot supply new audio, and peaks can become louder.", "warp", 1, 5, 2, 0,
        {ENV_WINDOW, {"lift", "LIFT AMOUNT", "LINEAR AMOUNT ADDED TO ENVELOPE LEVELS", "", TS_PORTAL_REAL, 0, 1, .1}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.7", "ENVELOPE FLATTEN", "Smooth the loudness contour across neighbouring envelope windows. Window duration and averaging count together set the smoothing time.", "warp", 1, 7, 2, 0,
        {ENV_WINDOW, {"average", "AVERAGE WINDOWS", "WINDOWS TO AVERAGE; MUST BE LESS THAN SOURCE ENVELOPE LENGTH", "", TS_PORTAL_INTEGER, 2, 64, 4}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.8", "ENVELOPE GATE", "Silence envelope regions below the gate. The window sets detection resolution; optional smoothing removes short low-level regions.", "warp", 1, 8, 3, 0,
        {ENV_WINDOW, ENV_GATE, ENV_SMOOTH}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.9", "ENVELOPE INVERT", "Reflect envelope levels around a mirror level. Regions below the gate are silenced. This changes dynamics, not waveform polarity.", "warp", 1, 9, 3, 0,
        {ENV_WINDOW, ENV_GATE, {"mirror", "MIRROR LEVEL", "REFLECTION LEVEL; ABOVE GATE AND BELOW 1", "", TS_PORTAL_REAL, .01, .99, .3}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.10", "ENVELOPE LIMIT", "Squeeze envelope levels above a threshold toward a ceiling. This is offline envelope shaping; compare its result with the live output limiter.", "warp", 1, 10, 3, 0,
        {ENV_WINDOW, {"limit", "LIMIT LEVEL", "UPPER ENVELOPE LEVEL; MUST EXCEED THRESHOLD", "", TS_PORTAL_REAL, .01, 1, .5},
         {"threshold", "THRESHOLD", "LEVEL WHERE ENVELOPE COMPRESSION BEGINS", "", TS_PORTAL_REAL, 0, .99, .2}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.11", "ENVELOPE CORRUGATE", "Deepen detected envelope troughs to silence. Trough width and peak separation set which valleys become gaps in the sound.", "warp", 1, 11, 3, 0,
        {ENV_WINDOW, {"trough", "TROUGH WINDOWS", "WINDOWS TO SILENCE AT A TROUGH; LESS THAN PEAK SEPARATION", "", TS_PORTAL_INTEGER, 1, 32, 1},
         {"separation", "PEAK SEPARATION", "MINIMUM WINDOWS BETWEEN DETECTED PEAKS", "", TS_PORTAL_INTEGER, 2, 64, 4}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.12", "ENVELOPE EXPAND", "Silence material below the gate and push remaining envelope levels upward toward the threshold. Smoothing controls short low-level regions.", "warp", 1, 12, 4, 0,
        {ENV_WINDOW, ENV_GATE, {"threshold", "THRESHOLD", "LOWER TARGET LEVEL; MUST EXCEED GATE", "", TS_PORTAL_REAL, .01, 1, .1}, ENV_SMOOTH}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.14", "ENVELOPE CEILING", "Raise the loudness contour toward its maximum throughout the sound. Brings sustained detail forward while preserving the forward audio order.", "warp", 1, 14, 1, 0,
        {ENV_WINDOW}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.warp.15", "ENVELOPE DUCK", "Reduce envelope regions above the threshold to the duck level. Elsewhere the envelope is unchanged. Try it on pronounced attacks.", "warp", 1, 15, 3, 0,
        {ENV_WINDOW, {"duck", "DUCK LEVEL", "TARGET LEVEL IN REGIONS ABOVE THRESHOLD", "", TS_PORTAL_REAL, 0, 1, .1},
         {"threshold", "THRESHOLD", "LEVEL THAT TRIGGERS DUCKING", "", TS_PORTAL_REAL, 0, 1, .3}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.dovetail.1", "DOVETAIL FADES", "Fade the beginning and end without shortening the sound. Choose linear or exponential curves separately for the two ends.", "dovetail", 1, 1, 4, 0,
        {FADE_IN, FADE_OUT,
         {"in_type", "IN CURVE", "0 LINEAR; 1 EXPONENTIAL", "", TS_PORTAL_INTEGER, 0, 1, 0},
         {"out_type", "OUT CURVE", "0 LINEAR; 1 EXPONENTIAL", "", TS_PORTAL_INTEGER, 0, 1, 1}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.dovetail.2", "STEEP DOVETAIL", "Fade both ends using CDP's doubly exponential curves. Compare with ordinary dovetail fades at the same durations.", "dovetail", 1, 2, 2, 0,
        {FADE_IN, FADE_OUT}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.swell", "ENVELOPE SWELL", "Fade from silence to a chosen peak moment and back to silence. Peak time sets the balance between the rise and fall.", "swell", 1, 0, 2, 0,
        {{"peak", "PEAK TIME SEC", "PEAK TIME; LEAVE AT LEAST 5 MS AT EACH END", "", TS_PORTAL_REAL, .005, 5, .25},
         {"curve", "CURVE", "0 LINEAR; 1 EXPONENTIAL", "", TS_PORTAL_INTEGER, 0, 1, 0}}, TS_PORTAL_ENVELOPE, "envel"},
    {"envel.tremolo.1", "TREMOLO", "Modulate amplitude with a periodic envelope. Slow rates create pulses; faster rates add sidebands. Depth zero retains the source at the chosen gain.", "tremolo", 1, 1, 3, 0,
        {{"rate", "RATE HZ", "AMPLITUDE MODULATION RATE", "", TS_PORTAL_REAL, 0, 100, 5},
         {"depth", "DEPTH", "0 NO MODULATION; 1 FULL DEPTH", "", TS_PORTAL_REAL, 0, 1, .6},
         {"gain", "OUTPUT GAIN", "OVERALL LINEAR SIGNAL GAIN", "", TS_PORTAL_REAL, 0, 1, .8}}, TS_PORTAL_ENVELOPE, "envel"},
    /* Final scalar-input batch: focus, hilite, sfedit and extend (CDP8). */
    {"focus.accu", "SPECTRAL SUSTAIN", "Sustain each spectral band until louder material replaces it. Decay controls retention per second; glide moves held bands in octaves per second.", "accu", 1, 0, 2, 0,
        {{"decay", "DECAY / SECOND", "RETAINED AMPLITUDE AFTER ONE SECOND; 1 HOLDS LEVEL", "-d", TS_PORTAL_REAL, 0.001, 1, 0.25},
         {"glide", "GLIDE OCT / SEC", "PITCH DRIFT OF SUSTAINED SPECTRAL BANDS", "-g", TS_PORTAL_REAL, -2, 2, 0}}, TS_PORTAL_SPECTRAL, "focus"},
    {"focus.exag", "SPECTRAL CONTRAST", "Reshape relative partial levels while retaining total spectral amplitude. Below 1 emphasises stronger peaks; above 1 brings quieter partials forward.", "exag", 1, 0, 1, 0,
        {{"contrast", "CONTOUR RATIO", "CDP USES THE RECIPROCAL AS A POWER; 1 RETAINS CONTOUR", "", TS_PORTAL_REAL, 0.125, 8, 0.5}}, TS_PORTAL_SPECTRAL, "focus"},
    {"focus.focus", "SPECTRAL PEAK FOCUS", "Concentrate energy around detected spectral-envelope peaks. Peak count selects how many regions survive; width controls their bandwidth in octaves.", "focus", 1, 0, 3, 0,
        {{"bins", "ENVELOPE BINS", "LINEAR FREQUENCY GROUPING FOR THE FORMANT ENVELOPE", "-f", TS_PORTAL_INTEGER, 1, 32, 4},
         {"peaks", "PEAK COUNT", "MAXIMUM NUMBER OF SPECTRAL ENVELOPE PEAKS", "", TS_PORTAL_INTEGER, 1, 16, 4},
         {"width", "WIDTH OCTAVES", "BANDWIDTH AROUND EACH DETECTED PEAK", "", TS_PORTAL_REAL, 0.1, 4, 0.5}}, TS_PORTAL_SPECTRAL, "focus"},
    {"focus.fold", "SPECTRAL OCTAVE FOLD", "Move partials by octaves into a chosen frequency band. The band must span at least one octave. Full Spectrum retains a denser folded result.", "fold", 1, 0, 3, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"full", "FULL SPECTRUM", "RETAIN A FULLER FOLDED SPECTRUM", "-x", TS_PORTAL_SWITCH, 0, 1, 0}}, TS_PORTAL_SPECTRAL, "focus"},
    {"focus.step", "SPECTRAL STEP HOLD", "Freeze a spectral frame at regular intervals until the next step. Duration stays approximately unchanged; larger steps create blocky spectral motion.", "step", 1, 0, 1, 0,
        {{"step", "STEP SECONDS", "HOLD INTERVAL; AT LEAST TWO ANALYSIS HOPS AND WITHIN SOURCE", "", TS_PORTAL_REAL, 0.01, 2, 0.1}}, TS_PORTAL_SPECTRAL, "focus"},
    {"hilite.filter.1", "SPEC HIGH PASS", "Filter partial amplitudes in the analysis spectrum. Removed bands are not automatically made up in level. Skirt width controls the transition in Hz.", "filter", 1, 1, 2, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.2", "SPEC HIGH PASS NORM", "Filter partial amplitudes in the analysis spectrum. CDP restores each frame's total amplitude after filtering. Skirt width controls the transition in Hz.", "filter", 1, 2, 2, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.3", "SPEC LOW PASS", "Filter partial amplitudes in the analysis spectrum. Removed bands are not automatically made up in level. Skirt width controls the transition in Hz.", "filter", 1, 3, 2, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.4", "SPEC LOW PASS NORM", "Filter partial amplitudes in the analysis spectrum. CDP restores each frame's total amplitude after filtering. Skirt width controls the transition in Hz.", "filter", 1, 4, 2, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.5", "SPEC HIGH PASS GAIN", "Filter partial amplitudes in the analysis spectrum. Gain scales the filtered spectrum. Skirt width controls the transition in Hz.", "filter", 1, 5, 3, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200},
         {"gain", "OUTPUT GAIN", "LINEAR GAIN AFTER SPECTRAL FILTERING", "", TS_PORTAL_REAL, 0.01, 4, 0.5}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.6", "SPEC LOW PASS GAIN", "Filter partial amplitudes in the analysis spectrum. Gain scales the filtered spectrum. Skirt width controls the transition in Hz.", "filter", 1, 6, 3, 0,
        {{"frequency", "CUTOFF HZ", "SPECTRAL FILTER CUTOFF; BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 16000, 1000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200},
         {"gain", "OUTPUT GAIN", "LINEAR GAIN AFTER SPECTRAL FILTERING", "", TS_PORTAL_REAL, 0.01, 4, 0.5}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.7", "SPEC BAND PASS", "Filter partial amplitudes in the analysis spectrum. Removed bands are not automatically made up in level. Skirt width controls the transition in Hz.", "filter", 1, 7, 3, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.8", "SPEC BAND PASS NORM", "Filter partial amplitudes in the analysis spectrum. CDP restores each frame's total amplitude after filtering. Skirt width controls the transition in Hz.", "filter", 1, 8, 3, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.9", "SPEC NOTCH", "Filter partial amplitudes in the analysis spectrum. Removed bands are not automatically made up in level. Skirt width controls the transition in Hz.", "filter", 1, 9, 3, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.10", "SPEC NOTCH NORM", "Filter partial amplitudes in the analysis spectrum. CDP restores each frame's total amplitude after filtering. Skirt width controls the transition in Hz.", "filter", 1, 10, 3, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.11", "SPEC BAND PASS GAIN", "Filter partial amplitudes in the analysis spectrum. Gain scales the filtered spectrum. Skirt width controls the transition in Hz.", "filter", 1, 11, 4, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200},
         {"gain", "OUTPUT GAIN", "LINEAR GAIN AFTER SPECTRAL FILTERING", "", TS_PORTAL_REAL, 0.01, 4, 0.5}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.filter.12", "SPEC NOTCH GAIN", "Filter partial amplitudes in the analysis spectrum. Gain scales the filtered spectrum. Skirt width controls the transition in Hz.", "filter", 1, 12, 4, 0,
        {{"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000},
         {"skirt", "SKIRT WIDTH HZ", "TRANSITION WIDTH IN HZ; CDP CALLS THIS Q", "", TS_PORTAL_REAL, 1, 4000, 200},
         {"gain", "OUTPUT GAIN", "LINEAR GAIN AFTER SPECTRAL FILTERING", "", TS_PORTAL_REAL, 0.01, 4, 0.5}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.trace.1", "TRACE PARTIALS", "Retain only the loudest spectral partials in each frame. Compare with Suppress Partials, which removes the loudest instead.", "trace", 1, 1, 1, 0,
        {{"partials", "PARTIALS", "NUMBER OF LOUDEST ANALYSIS CHANNELS TO RETAIN", "", TS_PORTAL_INTEGER, 1, 512, 16}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.trace.2", "TRACE ABOVE", "Retain only the loudest spectral partials in each frame within the chosen frequency region; discard material outside it.", "trace", 1, 2, 2, 0,
        {{"partials", "PARTIALS", "NUMBER OF LOUDEST ANALYSIS CHANNELS TO RETAIN", "", TS_PORTAL_INTEGER, 1, 512, 16},
         {"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.trace.3", "TRACE BELOW", "Retain only the loudest spectral partials in each frame within the chosen frequency region; discard material outside it.", "trace", 1, 3, 2, 0,
        {{"partials", "PARTIALS", "NUMBER OF LOUDEST ANALYSIS CHANNELS TO RETAIN", "", TS_PORTAL_INTEGER, 1, 512, 16},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.trace.4", "TRACE BAND", "Retain only the loudest spectral partials in each frame within the chosen frequency region; discard material outside it.", "trace", 1, 4, 3, 0,
        {{"partials", "PARTIALS", "NUMBER OF LOUDEST ANALYSIS CHANNELS TO RETAIN", "", TS_PORTAL_INTEGER, 1, 512, 16},
         {"low", "LOW HZ", "LOWER FREQUENCY; BELOW HIGH HZ AND SOURCE NYQUIST", "", TS_PORTAL_REAL, 20, 8000, 300},
         {"high", "HIGH HZ", "UPPER FREQUENCY; ABOVE LOW HZ AND BELOW SOURCE NYQUIST", "", TS_PORTAL_REAL, 40, 16000, 3000}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.pluck", "SPECTRAL PLUCK", "Boost partials when they become newly prominent in the spectrum. Useful after spectral stepping or other effects that introduce changing bands.", "pluck", 1, 0, 1, 0,
        {{"gain", "ATTACK GAIN", "GAIN APPLIED TO NEWLY PROMINENT PARTIALS", "", TS_PORTAL_REAL, 0, 16, 2}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"hilite.bltr", "BLUR AND TRACE", "Average spectra across time, then retain only the loudest partials. Combines softened motion with a sparse spectral outline.", "bltr", 1, 0, 2, 0,
        {{"windows", "BLUR WINDOWS", "ANALYSIS WINDOWS TO AVERAGE; MUST FIT SOURCE", "", TS_PORTAL_INTEGER, 1, 256, 16},
         {"partials", "PARTIALS", "NUMBER OF LOUDEST ANALYSIS CHANNELS TO RETAIN", "", TS_PORTAL_INTEGER, 1, 512, 16}}, TS_PORTAL_SPECTRAL, "hilite"},
    {"sfedit.cut.1", "KEEP SEGMENT", "Keep the segment between Start and End, fading its edges. Times refer to the current Portal source.", "cut", 1, 1, 3, 1,
        {{"start", "START SECONDS", "START POSITION WITHIN THE SOURCE", "", TS_PORTAL_REAL, 0, 30, 0.1},
         {"end", "END SECONDS", "END POSITION; AFTER START AND WITHIN SOURCE", "", TS_PORTAL_REAL, 0.01, 30, 0.4},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "sfedit"},
    {"sfedit.cutend.1", "KEEP TAIL", "Keep a chosen duration from the end of the source, with a splice at the new beginning.", "cutend", 1, 1, 2, 1,
        {{"length", "LENGTH SECONDS", "DURATION TO KEEP AT THE END OF THE SOURCE", "", TS_PORTAL_REAL, 0.02, 30, 0.3},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "sfedit"},
    {"sfedit.excise.1", "REMOVE SEGMENT", "Remove an interior segment and close the gap with a crossfade. Leaves the surrounding material in its original order.", "excise", 1, 1, 3, 1,
        {{"start", "START SECONDS", "START POSITION WITHIN THE SOURCE", "", TS_PORTAL_REAL, 0, 30, 0.1},
         {"end", "END SECONDS", "END POSITION; AFTER START AND WITHIN SOURCE", "", TS_PORTAL_REAL, 0.01, 30, 0.4},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "sfedit"},
    {"extend.doublets", "SEGMENT REPEATS", "Divide the source into consecutive segments and repeat each before advancing. Sync asks CDP to stay near the original time position instead of expanding every segment.", "doublets", 1, 0, 3, 1,
        {{"segment", "SEGMENT SECONDS", "SEGMENT DURATION; MUST FIT SOURCE", "", TS_PORTAL_REAL, 0.02, 2, 0.1},
         {"repeats", "REPEATS", "NUMBER OF COPIES OF EACH SEGMENT", "", TS_PORTAL_INTEGER, 2, 16, 2},
         {"sync", "SYNC TO SOURCE", "TRY TO KEEP OUTPUT SYNCHRONISED TO SOURCE TIME", "-s", TS_PORTAL_SWITCH, 0, 1, 0}}, TS_PORTAL_STRUCTURE, "extend"},
    {"extend.loop.1", "ADVANCING LOOPS", "Splice repeated segments while moving through the source. Playback stops when it reaches the source end. Advance sets the distance between successive source starts.", "loop", 1, 1, 4, 1,
        {{"start", "START SECONDS", "START POSITION WITHIN THE SOURCE", "", TS_PORTAL_REAL, 0, 30, 0.1},
         {"length", "LOOP LENGTH MS", "LOOP SEGMENT LENGTH; MUST FIT SOURCE AND TWO SPLICES", "", TS_PORTAL_REAL, 20, 2000, 100},
         {"advance", "ADVANCE MS", "SOURCE ADVANCE BETWEEN LOOPS; ZERO REPEATS ONE SEGMENT", "", TS_PORTAL_REAL, 1, 1000, 25},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "extend"},
    {"extend.loop.2", "LOOP TO DURATION", "Splice repeated segments while moving through the source. Playback stops when it reaches the source end. Advance sets the distance between successive source starts.", "loop", 1, 2, 5, 1,
        {{"duration", "OUTPUT SECONDS", "REQUESTED OUTPUT DURATION; MAY END EARLY AT SOURCE END", "", TS_PORTAL_REAL, 0.1, 30, 2},
         {"start", "START SECONDS", "START POSITION WITHIN THE SOURCE", "", TS_PORTAL_REAL, 0, 30, 0.1},
         {"length", "LOOP LENGTH MS", "LOOP SEGMENT LENGTH; MUST FIT SOURCE AND TWO SPLICES", "", TS_PORTAL_REAL, 20, 2000, 100},
         {"advance", "ADVANCE MS", "SOURCE ADVANCE BETWEEN LOOPS; ZERO REPEATS ONE SEGMENT", "-l", TS_PORTAL_REAL, 0, 1000, 0},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "extend"},
    {"extend.loop.3", "LOOP COUNT", "Splice repeated segments while moving through the source. Playback stops when it reaches the source end. Advance sets the distance between successive source starts.", "loop", 1, 3, 5, 1,
        {{"repeats", "LOOP REPEATS", "REQUESTED LOOP COUNT; MAY END EARLY AT SOURCE END", "", TS_PORTAL_INTEGER, 1, 32, 4},
         {"start", "START SECONDS", "START POSITION WITHIN THE SOURCE", "", TS_PORTAL_REAL, 0, 30, 0.1},
         {"length", "LOOP LENGTH MS", "LOOP SEGMENT LENGTH; MUST FIT SOURCE AND TWO SPLICES", "", TS_PORTAL_REAL, 20, 2000, 100},
         {"advance", "ADVANCE MS", "SOURCE ADVANCE BETWEEN LOOPS; ZERO REPEATS ONE SEGMENT", "-l", TS_PORTAL_REAL, 0, 1000, 0},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5}}, TS_PORTAL_STRUCTURE, "extend"},
    {"extend.scramble.1", "RANDOM CHUNKS", "Cut and splice random source chunks into a new sequence. Choose a minimum and maximum chunk duration. Positive seeds make the choices repeatable.", "scramble", 1, 1, 5, 1,
        {{"minimum", "MIN CHUNK SEC", "MINIMUM CHUNK DURATION; BELOW MAXIMUM", "", TS_PORTAL_REAL, 0.06, 2, 0.1},
         {"maximum", "MAX CHUNK SEC", "MAXIMUM CHUNK DURATION; WITHIN SOURCE", "", TS_PORTAL_REAL, 0.07, 3, 0.3},
         {"duration", "OUTPUT SECONDS", "OUTPUT DURATION; LONGER THAN A CHUNK", "", TS_PORTAL_REAL, 0.1, 30, 2},
         {"splice", "SPLICE MS", "EDGE CROSSFADE LENGTH; MUST FIT THE SEGMENT", "-w", TS_PORTAL_REAL, 1, 50, 5},
         {"seed", "RANDOM SEED", "SAME POSITIVE SEED REPEATS THE RANDOM CHOICES", "-s", TS_PORTAL_INTEGER, 1, 32767, 1}}, TS_PORTAL_STRUCTURE, "extend"},
    {"strange.shift.1", "SPECTRAL HZ SHIFT", "Add the same frequency offset to all partials. This changes harmonic relationships, unlike musical transposition by a ratio.", "shift", 1, 1, 1, 0,
        {{"shift", "SHIFT HZ", "LINEAR FREQUENCY OFFSET; PARTIALS LEAVING THE SPECTRUM ARE LOST", "", TS_PORTAL_REAL, -2000, 2000, 100}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.shift.2", "HZ SHIFT ABOVE", "Add the same frequency offset to partials above the divide. This changes harmonic relationships, unlike musical transposition by a ratio.", "shift", 1, 2, 2, 0,
        {{"shift", "SHIFT HZ", "LINEAR FREQUENCY OFFSET; PARTIALS LEAVING THE SPECTRUM ARE LOST", "", TS_PORTAL_REAL, -2000, 2000, 100},
         {"divide", "DIVIDE HZ", "BOUNDARY BETWEEN SHIFTED AND UNSHIFTED FREQUENCIES", "", TS_PORTAL_REAL, 40, 16000, 1000}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.shift.3", "HZ SHIFT BELOW", "Add the same frequency offset to partials below the divide. This changes harmonic relationships, unlike musical transposition by a ratio.", "shift", 1, 3, 2, 0,
        {{"shift", "SHIFT HZ", "LINEAR FREQUENCY OFFSET; PARTIALS LEAVING THE SPECTRUM ARE LOST", "", TS_PORTAL_REAL, -2000, 2000, 100},
         {"divide", "DIVIDE HZ", "BOUNDARY BETWEEN SHIFTED AND UNSHIFTED FREQUENCIES", "", TS_PORTAL_REAL, 40, 16000, 1000}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.shift.4", "HZ SHIFT BAND", "Add the same frequency offset to partials inside the band. This changes harmonic relationships, unlike musical transposition by a ratio.", "shift", 1, 4, 3, 0,
        {{"shift", "SHIFT HZ", "LINEAR FREQUENCY OFFSET; PARTIALS LEAVING THE SPECTRUM ARE LOST", "", TS_PORTAL_REAL, -2000, 2000, 100},
         {"low", "LOW HZ", "LOWER FREQUENCY BOUNDARY; BELOW HIGH AND NYQUIST", "", TS_PORTAL_REAL, 40, 8000, 500},
         {"high", "HIGH HZ", "UPPER FREQUENCY BOUNDARY; ABOVE LOW AND BELOW NYQUIST", "", TS_PORTAL_REAL, 80, 16000, 4000}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.shift.5", "HZ SHIFT OUTSIDE", "Add the same frequency offset to partials outside the band. This changes harmonic relationships, unlike musical transposition by a ratio.", "shift", 1, 5, 3, 0,
        {{"shift", "SHIFT HZ", "LINEAR FREQUENCY OFFSET; PARTIALS LEAVING THE SPECTRUM ARE LOST", "", TS_PORTAL_REAL, -2000, 2000, 100},
         {"low", "LOW HZ", "LOWER FREQUENCY BOUNDARY; BELOW HIGH AND NYQUIST", "", TS_PORTAL_REAL, 40, 8000, 500},
         {"high", "HIGH HZ", "UPPER FREQUENCY BOUNDARY; ABOVE LOW AND BELOW NYQUIST", "", TS_PORTAL_REAL, 80, 16000, 4000}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.glis.1", "SHEPARD GLIDE", "Create Shepard tones inside the changing spectral envelope of the source. Glide rate controls direction and speed.", "glis", 1, 1, 2, 0,
        {{"bins", "ENVELOPE BINS", "FREQUENCY GROUPING FOR SOURCE FORMANT ENVELOPE", "-f", TS_PORTAL_INTEGER, 1, 32, 4},
         {"rate", "SEMITONES / SEC", "CONTINUOUS GLIDE RATE; NEGATIVE GLIDES DOWNWARD", "", TS_PORTAL_REAL, -24, 24, 4}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.glis.2", "INHARMONIC GLIDE", "Create inharmonic gliding partials inside the changing spectral envelope of the source. Glide rate controls direction and speed.", "glis", 1, 2, 3, 0,
        {{"bins", "ENVELOPE BINS", "FREQUENCY GROUPING FOR SOURCE FORMANT ENVELOPE", "-f", TS_PORTAL_INTEGER, 1, 32, 4},
         {"rate", "SEMITONES / SEC", "CONTINUOUS GLIDE RATE; NEGATIVE GLIDES DOWNWARD", "", TS_PORTAL_REAL, -24, 24, 4},
         {"spacing", "PARTIAL GAP HZ", "INHARMONIC PARTIAL SPACING; AT LEAST ONE ANALYSIS BIN", "", TS_PORTAL_REAL, 50, 4000, 200}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.glis.3", "SELF GLISSANDO", "Create a self-glissando inside the changing spectral envelope of the source. Glide rate controls direction and speed.", "glis", 1, 3, 2, 0,
        {{"bins", "ENVELOPE BINS", "FREQUENCY GROUPING FOR SOURCE FORMANT ENVELOPE", "-f", TS_PORTAL_INTEGER, 1, 32, 4},
         {"rate", "SEMITONES / SEC", "CONTINUOUS GLIDE RATE; NEGATIVE GLIDES DOWNWARD", "", TS_PORTAL_REAL, -24, 24, 4}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.waver.1", "SPECTRAL WAVER", "Oscillate between the original spectrum and an inharmonic stretch. Waver changes the frequency relationships between partials.", "waver", 1, 1, 3, 0,
        {{"rate", "WAVER RATE HZ", "OSCILLATION RATE; AT LEAST ONE CYCLE OVER SOURCE DURATION", "", TS_PORTAL_REAL, 0.5, 20, 2},
         {"stretch", "MAX STRETCH", "MAXIMUM INHARMONIC SPECTRAL STRETCH", "", TS_PORTAL_REAL, 1, 4, 1.5},
         {"low", "ABOVE HZ", "FREQUENCY ABOVE WHICH STRETCHING OCCURS", "", TS_PORTAL_REAL, 40, 8000, 500}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.waver.2", "SHAPED WAVER", "Oscillate between the original spectrum and an inharmonic stretch. Waver changes the frequency relationships between partials.", "waver", 1, 2, 4, 0,
        {{"rate", "WAVER RATE HZ", "OSCILLATION RATE; AT LEAST ONE CYCLE OVER SOURCE DURATION", "", TS_PORTAL_REAL, 0.5, 20, 2},
         {"stretch", "MAX STRETCH", "MAXIMUM INHARMONIC SPECTRAL STRETCH", "", TS_PORTAL_REAL, 1, 4, 1.5},
         {"low", "ABOVE HZ", "FREQUENCY ABOVE WHICH STRETCHING OCCURS", "", TS_PORTAL_REAL, 40, 8000, 500},
         {"exponent", "EXPONENT", "SHAPE OF THE INHARMONIC STRETCH", "", TS_PORTAL_REAL, 0.1, 8, 2}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.invert.1", "INVERT SPECTRUM", "Invert partial amplitudes relative to their observed maxima. Strong spectral regions become weak and weak regions become strong.", "invert", 1, 1, 0, 0,
        {{0}}, TS_PORTAL_SPECTRAL, "strange"},
    {"strange.invert.2", "INVERT KEEP ENVELOPE", "Invert partial amplitudes relative to their observed maxima, while retaining the source amplitude envelope.", "invert", 1, 2, 0, 0,
        {{0}}, TS_PORTAL_SPECTRAL, "strange"}

};
#undef ENV_WINDOW
#undef ENV_GROUP
#undef ENV_EXP
#undef ENV_GATE
#undef ENV_SMOOTH
#undef FADE_IN
#undef FADE_OUT
#undef GROUP
#undef SKIP
#undef MULT
#undef CYCLEFLAG
#undef ACUITY
#undef FILTER_GAIN
#undef FILTER_FREQ
#undef FILTER_TAIL
#undef CHORUS_AMP
#undef CHORUS_FREQ
#undef EQ_GAIN
#undef EQ_FREQ
#undef EQ_PRESCALE
#undef SPECTRUM_PARAMS
#undef SWEEP_PARAMS

#include "ts_cdp_portal_factory.inc"

static int fail(char *error, size_t size, const char *message)
{ if (error && size) snprintf(error, size, "%s", message); return 0; }
size_t ts_portal_process_count(void) { return sizeof(processes)/sizeof(processes[0]); }
const TsPortalProcess *ts_portal_process_at(size_t index)
{ return index < ts_portal_process_count() ? &processes[index] : NULL; }
const TsPortalProcess *ts_portal_process_find(const char *id)
{
    if (id) for (size_t i=0; i<ts_portal_process_count(); ++i)
        if (!strcmp(id, processes[i].id)) return &processes[i];
    const TsCdpRecipe *f=ts_portal_factory_find(id);
    return f?ts_portal_factory_process_at((size_t)ts_cdp_recipe_index_for_id(f->id)):NULL;
}
void ts_portal_recipe_default(TsPortalRecipe *r, const TsPortalProcess *p)
{
    memset(r, 0, sizeof(*r));
    if (!p) return;
    const TsCdpRecipe *f=ts_portal_factory_find(p->id);
    if(f){TsCdpRecipeValues v;ts_cdp_recipe_values_default(f,&v);ts_portal_factory_recipe(f,&v,r);return;}
    snprintf(r->process_id, sizeof(r->process_id), "%s", p->id);
    snprintf(r->name, sizeof(r->name), "%s", p->title);
    r->version=p->version;
    r->exposed=(1u<<p->parameter_count)-1u;
    for (unsigned i=0;i<p->parameter_count;++i) r->values[i]=p->parameters[i].initial;
}
void ts_portal_step_get(const TsPortalStep *s,TsPortalRecipe *r)
{
    memset(r,0,sizeof(*r));memcpy(r->process_id,s->process_id,sizeof(r->process_id));
    memcpy(r->name,s->name,sizeof(r->name));r->version=s->version;r->exposed=s->exposed;
    memcpy(r->values,s->values,sizeof(r->values));memcpy(r->macros,s->macros,sizeof(r->macros));
}
void ts_portal_step_set(TsPortalStep *s,const TsPortalRecipe *r)
{
    memcpy(s->process_id,r->process_id,sizeof(s->process_id));memcpy(s->name,r->name,sizeof(s->name));
    s->version=r->version;s->exposed=r->exposed;memcpy(s->values,r->values,sizeof(s->values));
    memcpy(s->macros,r->macros,sizeof(s->macros));
}
void ts_portal_recipe_exact(TsPortalRecipe *r)
{r->exposed=0;for(unsigned i=0;i<r->stage_count && i<TS_PORTAL_CHAIN_STAGES;++i)r->stages[i].exposed=0;}
int ts_portal_step_equal(const TsPortalStep *a,const TsPortalStep *b)
{return !strcmp(a->process_id,b->process_id) && a->version==b->version && a->bypass==b->bypass &&
    !memcmp(a->values,b->values,sizeof(a->values));}
int ts_portal_recipe_validate(const TsPortalRecipe *r, char *error, size_t size)
{
    const TsPortalProcess *p;
    if (!r || !memchr(r->description,0,sizeof(r->description)) || !memchr(r->process_id,0,sizeof(r->process_id)) ||
        !memchr(r->name,0,sizeof(r->name))) return fail(error,size,"MALFORMED RECIPE");
    for(const char *s=r->description;*s;++s)if((unsigned char)*s<32 || (unsigned char)*s>126 || *s=='|')return fail(error,size,"INVALID DESCRIPTION");
    if(r->stage_count || !strcmp(r->process_id,"@chain")) {
        if(strcmp(r->process_id,"@chain") || r->version!=1 || !r->stage_count || r->stage_count>TS_PORTAL_CHAIN_STAGES)
            return fail(error,size,"INVALID CHAIN VERSION OR STAGE COUNT (1 TO 8)");
        for(const char *s=r->name;*s;++s)if((unsigned char)*s<32 || (unsigned char)*s>126 || *s=='|')
            return fail(error,size,"INVALID CHAIN NAME");
        for(unsigned i=0;i<TS_PORTAL_PARAMS;++i)if(r->macros[i].configured)return fail(error,size,"CHAIN MACROS BELONG TO STAGES");
        for(unsigned i=0;i<r->stage_count;++i) {
            TsPortalRecipe step;ts_portal_step_get(&r->stages[i],&step);
            if(r->stages[i].bypass!=0 && r->stages[i].bypass!=1)
                return fail(error,size,"NESTED CHAINS OR INVALID BYPASS ARE NOT SUPPORTED");
            if(!ts_portal_recipe_validate(&step,error,size))return 0;
        }
        return 1;
    }
    p=ts_portal_process_find(r->process_id);
    if (!p || r->version!=p->version) return fail(error,size,"UNKNOWN PROCESS OR RECIPE VERSION");
    if (r->exposed >> p->parameter_count) return fail(error,size,"INVALID MACRO MASK");
    for (const char *s=r->name;*s;++s)
        if ((unsigned char)*s<32 || (unsigned char)*s>126 || *s=='|')
            return fail(error,size,"INVALID RECIPE NAME");
    const TsCdpRecipe *factory=ts_portal_factory_find(r->process_id);
    for (unsigned i=0;i<TS_PORTAL_PARAMS;++i) {
        const TsPortalMacro *m=&r->macros[i];
        if(m->configured!=0 && m->configured!=1)return fail(error,size,"INVALID MACRO CONFIGURATION");
        if(m->configured) {
            if(i>=p->parameter_count || !memchr(m->name,0,sizeof(m->name)) || !m->name[0] ||
               !isfinite(m->minimum) || !isfinite(m->maximum) || m->minimum>=m->maximum ||
               m->minimum<p->parameters[i].minimum || m->maximum>p->parameters[i].maximum ||
               ts_portal_parameter_quantize(p,i,m->minimum)!=m->minimum ||
               ts_portal_parameter_quantize(p,i,m->maximum)!=m->maximum)
                return fail(error,size,"MACRO RANGE MUST USE LEGAL CDP VALUES, MIN BELOW MAX");
            for(const char *s=m->name;*s;++s)if((unsigned char)*s<32 || (unsigned char)*s>126 || *s=='|')
                return fail(error,size,"INVALID MACRO NAME");
        }
        double v=r->values[i];
        if (!isfinite(v)) return fail(error,size,"NONFINITE PARAMETER");
        if(factory && i>=13) {
            if(v<0 || (i==13?v>1e8:v>UINT32_MAX || floor(v)!=v))return fail(error,size,"INVALID FACTORY SEED OR TUNING CONTEXT");
            continue;
        }
        if(factory && i<factory->control_count && factory->controls[i].type==TS_CDP_CONTROL_ENUMERATED &&
           ts_cdp_control_quantize(&factory->controls[i],(float)v)!=v)
            return fail(error,size,"CHOOSE ONE OF THIS FACTORY CONTROL'S NAMED MODES");
        if (i>=p->parameter_count) { if(v!=0) return fail(error,size,"UNUSED PARAMETER IS NOT ZERO"); continue; }
        const TsPortalParam *s=&p->parameters[i];
        if(v<s->minimum || v>s->maximum || (s->type!=TS_PORTAL_REAL && v!=floor(v)))
            return fail(error,size,"PARAMETER OUTSIDE CDP RANGE");
        if(s->type==TS_PORTAL_ODD_INTEGER && fmod(v,2)!=1)
            return fail(error,size,"PARAMETER MUST BE AN ODD INTEGER");
    }
    if (!strcmp(p->command,"omit") && r->values[0]>=r->values[1])
        return fail(error,size,"OMIT A MUST BE LESS THAN EVERY B");
    if (p->family==TS_PORTAL_FILTER && !strcmp(p->command,"sweeping") && r->values[2]>=r->values[3])
        return fail(error,size,"LOW HZ MUST BE BELOW HIGH HZ");
    if(p->family==TS_PORTAL_ENVELOPE && !strcmp(p->command,"warp")) {
        if((p->mode==9 || p->mode==12) && r->values[2]<=r->values[1])
            return fail(error,size,"MIRROR / THRESHOLD MUST EXCEED GATE");
        if(p->mode==10 && r->values[1]<=r->values[2])
            return fail(error,size,"LIMIT MUST EXCEED THRESHOLD");
        if(p->mode==11 && r->values[1]>=r->values[2])
            return fail(error,size,"TROUGH WIDTH MUST BE LESS THAN PEAK SEPARATION");
    }
    if(p->family==TS_PORTAL_STRUCTURE &&
       ((!strcmp(p->command,"cut") || !strcmp(p->command,"excise") ||
         (!strcmp(p->command,"scramble") && p->mode==1)) && r->values[0]>=r->values[1]))
        return fail(error,size,"START / MINIMUM MUST BE BELOW END / MAXIMUM");
    if(p->family==TS_PORTAL_SPECTRAL) {
        if((!strcmp(p->command,"fold") && r->values[1]<2*r->values[0]) ||
           (!strcmp(p->command,"filter") && p->mode>=7 && r->values[0]>=r->values[1]) ||
           (!strcmp(p->command,"shift") && p->mode>=4 && r->values[1]>=r->values[2]) ||
           (!strcmp(p->command,"trace") && p->mode==4 && r->values[1]>=r->values[2]))
            return fail(error,size,"LOW MUST BE BELOW HIGH; OCTAVE FOLD NEEDS AT LEAST ONE OCTAVE");
    }
    if(error && size) error[0]=0;
    return 1;
}
int ts_portal_build_command(const TsPortalRecipe *r, const TsSample *input,
                           TsCdpCommand *c, char *error, size_t size)
{
    const TsPortalProcess *p;
    size_t cycles=0; int sign=0;
    if (!ts_portal_recipe_validate(r,error,size)) return 0;
    p=ts_portal_process_find(r->process_id);
    if(!p || p->family!=TS_PORTAL_WAVESET)return fail(error,size,"USE THE PROCESS COMMAND PLAN FOR THIS FAMILY");
    if (!input || !input->data || input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES ||
        !input->sample_rate || input->channels!=1)
        return fail(error,size,"WAVESET PROCESSES REQUIRE A MONO SOURCE");
    for(size_t i=0;i<input->frames;++i) {
        float v=input->data[i];
        if(!isfinite(v)) return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        int next=v>0?1:v<0?-1:0;
        if(next && sign && next!=sign) ++cycles;
        if(next) sign=next;
    }
    cycles/=2;
    if(cycles<2) return fail(error,size,"SOURCE NEEDS AT LEAST TWO COMPLETE WAVECYCLES");
    size_t group=1, skip=0;
    for(unsigned i=0;i<p->parameter_count;++i) {
        if(!strcmp(p->parameters[i].id,"cycles") || !strcmp(p->parameters[i].id,"every")) group=(size_t)r->values[i];
        if(!strcmp(p->parameters[i].id,"skip")) skip=(size_t)r->values[i];
    }
    if(skip>=cycles || group>cycles-skip)
        return fail(error,size,"GROUP / SKIP EXCEEDS SOURCE WAVECYCLES");
    if(!strcmp(p->command,"overload") && p->mode==2 && r->values[2]>=input->sample_rate*.5)
        return fail(error,size,"PATTERN FREQUENCY MUST BE BELOW SOURCE NYQUIST");
    if(!strcmp(p->command,"pitch") && (double)input->frames*exp2(r->values[0])+1024>TS_PORTAL_MAX_FRAMES)
        return fail(error,size,"REQUESTED PITCH WARP EXCEEDS PORTAL LIMIT");
    if(!strcmp(p->command,"replace") && (double)input->frames*group>TS_PORTAL_MAX_FRAMES)
        return fail(error,size,"REQUESTED CYCLE REPLACEMENT EXCEEDS PORTAL LIMIT");
    if ((!strcmp(p->command,"repeat") || !strcmp(p->command,"interpolate")) &&
        input->frames>(size_t)TS_PORTAL_MAX_FRAMES/(size_t)r->values[0])
        return fail(error,size,"REQUESTED STRETCH EXCEEDS CANVAS LIMIT");
    memset(c,0,sizeof(*c));
    snprintf(c->executable,sizeof(c->executable),"distort");
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
    if(p->mode) snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
    for(unsigned i=0;i<p->parameter_count;++i) {
        const TsPortalParam *s=&p->parameters[i];
        if(s->type==TS_PORTAL_SWITCH) {
            if(r->values[i]) snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",s->flag);
        } else snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",s->flag,r->values[i]);
    }
    snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
    c->expected_output_type=TS_CDP_IO_WAV;
    return 1;
}

int ts_portal_build_commands(const TsPortalRecipe *r,const TsSample *input,
                             TsCdpCommand commands[TS_CDP_MAX_STAGES],size_t *count,
                             char *error,size_t size)
{
    if(count)*count=0;
    if(!commands || !count)return fail(error,size,"MISSING COMMAND DESTINATION");
    if(!ts_portal_recipe_validate(r,error,size))return 0;
    const TsPortalProcess *p=ts_portal_process_find(r->process_id);
    if(!p)return fail(error,size,"CHAINS MUST BE RENDERED ONE STAGE AT A TIME");
    if(p->family==TS_PORTAL_FACTORY)return fail(error,size,"FACTORY INSTRUMENTS USE THEIR EXISTING RENDERER");
    memset(commands,0,sizeof(*commands)*TS_CDP_MAX_STAGES);
    if(p->family==TS_PORTAL_WAVESET) {
        if(!ts_portal_build_command(r,input,&commands[0],error,size))return 0;
        *count=1;return 1;
    }
    if(p->family==TS_PORTAL_STRUCTURE) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"STRUCTURE PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
        double duration=(double)input->frames/input->sample_rate, estimate=duration;
        if(duration<.04)return fail(error,size,"STRUCTURE SOURCE NEEDS AT LEAST 40 MS");
        for(size_t i=0;i<input->frames;++i)if(!isfinite(input->data[i]))
            return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        if(!strcmp(p->executable,"sfedit")) {
            double first=!strcmp(p->command,"cutend")?duration-r->values[0]:r->values[0];
            double last=!strcmp(p->command,"cutend")?duration:r->values[1];
            double splice=r->values[p->parameter_count-1]*.001;
            if(first<0 || last>duration || last-first<=2*splice)
                return fail(error,size,"SEGMENT MUST FIT SOURCE AND TWO SPLICES");
            if(!strcmp(p->command,"excise") && (first<=splice || last+splice>=duration))
                return fail(error,size,"REMOVE SEGMENT NEEDS AUDIO BEFORE AND AFTER ITS SPLICES");
        } else if(!strcmp(p->command,"doublets")) {
            if(r->values[0]>=duration-.01)return fail(error,size,"SEGMENT MUST LEAVE AT LEAST 10 MS OF SOURCE AFTER IT");
            estimate=(duration+r->values[0])*r->values[1];
        } else if(!strcmp(p->command,"loop")) {
            unsigned offset=p->mode==1?0:1;
            double start=r->values[offset],length=r->values[offset+1]*.001;
            double advance=r->values[offset+2]*.001,splice=r->values[offset+3]*.001;
            /* CDP scalar ranges reserve 50 ms even when a smaller splice is used. */
            if(start>duration-.05 || length>duration-.05 ||
               start+length+splice>duration || length<=2*splice || advance>duration)
                return fail(error,size,"LOOP START / LENGTH / SPLICE MUST FIT SOURCE");
            if((p->mode==2 && r->values[0]<start+length+1.0/input->sample_rate) ||
               (p->mode==1 && start+length+advance>duration))
                return fail(error,size,"LOOP NEEDS ROOM FOR A COMPLETE REPEAT");
            estimate=p->mode==1?(ceil((duration-start)/advance)+1)*length:
                     p->mode==2?r->values[0]+length:(r->values[0]+1)*length;
        } else if(!strcmp(p->command,"scramble")) {
            double splice=r->values[3]*.001;
            if(r->values[1]>duration-.025 || r->values[0]<=2*splice ||
               r->values[2]<=r->values[1])
                return fail(error,size,"CHUNKS MUST FIT SOURCE, SPLICES AND OUTPUT DURATION");
            estimate=r->values[2]+duration;
        }
        if(ceil(estimate*input->sample_rate)+1024>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"REQUESTED STRUCTURE OUTPUT EXCEEDS PORTAL LIMIT");
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        if(p->mode)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i) {
            const TsPortalParam *param=&p->parameters[i];
            if(param->type==TS_PORTAL_SWITCH) {
                if(r->values[i])snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",param->flag);
            } else snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",param->flag,r->values[i]);
        }
        snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
        c->expected_output_type=TS_CDP_IO_WAV;*count=1;return 1;
    }
    if(p->family==TS_PORTAL_ENVELOPE) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"ENVELOPE PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
        double duration=(double)input->frames/input->sample_rate;
        if(duration<.04)return fail(error,size,"ENVELOPE SOURCE NEEDS AT LEAST 40 MS");
        if(!strcmp(p->command,"warp")) {
            if(r->values[0]>duration*1000)
                return fail(error,size,"ENVELOPE WINDOW EXCEEDS SOURCE DURATION");
            /* Match CDP's mono window rounding in generate_samp_windowsize.
               Require complete windows, conservatively excluding the final
               partial extraction window from source-dependent counts. */
            double requested=round(r->values[0]*.001*input->sample_rate),window;
            if(requested<1)return fail(error,size,"ENVELOPE WINDOW MUST FIT AT LEAST ONE SOURCE SAMPLE");
            if(requested<256) {
                double lower=256;while(lower>requested && lower>1)lower/=2;
                window=2*lower-requested>requested-lower?lower:2*lower;
            } else window=256*round(requested/256);
            size_t windows=(size_t)((double)input->frames/window);
            if(windows<2)return fail(error,size,"SOURCE NEEDS AT LEAST TWO ENVELOPE WINDOWS");
            if(p->mode==7 && r->values[1]>=windows)
                return fail(error,size,"AVERAGING WINDOWS EXCEED SOURCE ENVELOPE; LOWER COUNT OR WINDOW");
            if(p->mode==11 && r->values[2]>=windows)
                return fail(error,size,"PEAK SEPARATION EXCEEDS SOURCE ENVELOPE");
        }
        if(!strcmp(p->command,"dovetail") && r->values[0]+r->values[1]>=duration)
            return fail(error,size,"START AND END FADES MUST NOT OVERLAP; LOWER FADE DURATIONS");
        if(!strcmp(p->command,"swell") && r->values[0]>duration-.005)
            return fail(error,size,"SWELL PEAK MUST LEAVE AT LEAST 5 MS AT EACH END");
        double peak=0;
        for(size_t i=0;i<input->frames;++i) {
            if(!isfinite(input->data[i]))return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
            peak=fmax(peak,fabs(input->data[i]));
        }
        if(!strcmp(p->command,"warp")) {
            if(peak<1.0/32767)return fail(error,size,"SOURCE TOO QUIET TO EXTRACT AN ENVELOPE AFTER WAV STAGING");
            if((p->mode==8 || p->mode==9 || p->mode==12) && peak<=r->values[1])
                return fail(error,size,"GATE WOULD REMOVE THE ENTIRE ENVELOPE; LOWER GATE");
        }
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        if(p->mode)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i)
            snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",p->parameters[i].flag,r->values[i]);
        snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
        c->expected_output_type=TS_CDP_IO_WAV;
        *count=1;return 1;
    }
    if(p->family==TS_PORTAL_GRAIN) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"GRANULAR PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
        if((double)input->frames/input->sample_rate<.04)
            return fail(error,size,"GRANULAR SOURCE NEEDS AT LEAST 40 MS");
        double grain=round((p->mode==4?r->values[0]:50)*.001*input->sample_rate);
        double splice=round(.005*input->sample_rate);
        double pitch=p->mode==1?exp2(r->values[0]/12):1;
        if(grain<4 || splice<1 || grain<=2*splice)
            return fail(error,size,"GRAIN MUST FIT TWO 5 MS SPLICES AT THE SOURCE RATE");
        if((double)input->frames<=fmax(grain,floor(grain*pitch)+1))
            return fail(error,size,"SOURCE TOO SHORT FOR GRAIN / PITCH; USE A LONGER SELECTION");
        if(p->mode==1 && fabs(r->values[0])>12*log2((double)input->sample_rate/20))
            return fail(error,size,"GRANULAR PITCH EXCEEDS CDP RANGE AT THIS SOURCE RATE");
        if(p->mode==4 && r->values[1]>(double)input->frames*2000/input->sample_rate)
            return fail(error,size,"LOOKBACK MUST NOT EXCEED TWICE THE SOURCE DURATION");
        /* CDP rounds both hop sizes to whole frames. Use those hops instead
           of just 1/velocity, and include the last grain and maximum scatter. */
        double outstep=round(grain/(p->mode==5?r->values[0]:2));
        double instep=round(outstep*(p->mode==2?r->values[0]:1));
        if(outstep<1 || instep<1)
            return fail(error,size,"GRANULAR HOP TOO SMALL AT THIS SOURCE RATE");
        double grains=ceil(((double)input->frames-grain)/instep);
        double output_bound=(grains-1)*outstep+ceil(outstep*.5)+grain+2;
        if(output_bound>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"REQUESTED GRANULAR OUTPUT EXCEEDS PORTAL LIMIT");
        for(size_t i=0;i<input->frames;++i)if(!isfinite(input->data[i]))
            return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i)
            snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",p->parameters[i].flag,r->values[i]);
        snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
        c->expected_output_type=TS_CDP_IO_WAV;
        *count=1;return 1;
    }
    if(p->family==TS_PORTAL_FILTER) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"FILTER PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
        if((double)input->frames/input->sample_rate<.04)
            return fail(error,size,"FILTER SOURCE NEEDS AT LEAST 40 MS");
        double tail=0;
        for(unsigned i=0;i<p->parameter_count;++i) {
            const char *id=p->parameters[i].id;
            /* CDP's state-variable recurrence uses 2*pi*f/sr directly. Keep
               this batch below sr/6, safely inside its stability region for
               every exposed acuity, rather than permitting Nyquist. */
            if(strcmp(p->command,"fixed") && (!strcmp(id,"frequency") || !strcmp(id,"low") || !strcmp(id,"high")) &&
               r->values[i]>(double)input->sample_rate/6)
                return fail(error,size,"FILTER FREQUENCY MUST NOT EXCEED SOURCE RATE / 6");
            if(!strcmp(p->command,"fixed") && !strcmp(id,"frequency") && r->values[i]>=input->sample_rate*.5)
                return fail(error,size,"EQ FREQUENCY MUST BE BELOW SOURCE NYQUIST");
            if(!strcmp(id,"bandwidth") && r->values[i]>=input->sample_rate*.25)
                return fail(error,size,"EQ BANDWIDTH MUST BE BELOW SOURCE RATE / 4");
            if(!strcmp(id,"delay") &&
               (r->values[i]<1000.0/input->sample_rate || r->values[i]>(double)input->frames*500/input->sample_rate))
                return fail(error,size,"DELAY MUST FIT ONE SAMPLE TO HALF THE SOURCE DURATION");
            if(!strcmp(id,"tail"))tail=r->values[i];
        }
        if((double)input->frames+ceil(tail*input->sample_rate)>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"SOURCE PLUS FILTER TAIL EXCEEDS PORTAL LIMIT");
        for(size_t i=0;i<input->frames;++i)if(!isfinite(input->data[i]))
            return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i)
            snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",p->parameters[i].flag,r->values[i]);
        snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
        c->expected_output_type=TS_CDP_IO_WAV;
        *count=1;return 1;
    }
    if(p->family==TS_PORTAL_TIME || p->family==TS_PORTAL_LOFI ||
       p->family==TS_PORTAL_LEVEL || p->family==TS_PORTAL_DELAY) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"PROCESS REQUIRES A MONO SOURCE WITHIN THE PORTAL LIMIT");
        if(input->frames<(size_t)input->sample_rate/25)
            return fail(error,size,"SOURCE NEEDS AT LEAST 40 MS");
        double peak=0,tail=0;
        for(size_t i=0;i<input->frames;++i) {
            if(!isfinite(input->data[i]))return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
            peak=fmax(peak,fabs(input->data[i]));
        }
        if(p->family==TS_PORTAL_LOFI) {
            if(p->mode==5 && r->values[0]>=input->sample_rate*.5)
                return fail(error,size,"MODULATION FREQUENCY MUST BE BELOW SOURCE NYQUIST");
            if(p->mode==4 && r->values[1]>input->frames)
                return fail(error,size,"RATE DIVISION EXCEEDS SOURCE LENGTH");
        }
        if(p->family==TS_PORTAL_LEVEL && (p->mode==3 || p->mode==4)) {
            if(peak<1.0/32767)return fail(error,size,"SOURCE TOO QUIET TO NORMALISE AFTER WAV STAGING");
            if(fabs(peak-r->values[0])<.0001)
                return fail(error,size,"SOURCE PEAK ALREADY AT TARGET LEVEL");
            if(p->mode==3 && peak>r->values[0])
                return fail(error,size,"RAISE PEAK TARGET MUST EXCEED SOURCE PEAK; USE SET PEAK TO ATTENUATE");
        }
        if(p->family==TS_PORTAL_DELAY)for(unsigned i=0;i<p->parameter_count;++i) {
            const char *id=p->parameters[i].id;
            if(!strcmp(id,"delay") && r->values[i]<1000.0/input->sample_rate)
                return fail(error,size,"DELAY MUST FIT AT LEAST ONE SOURCE SAMPLE");
            if(!strcmp(id,"tail"))tail=r->values[i];
        }
        if((double)input->frames+ceil(tail*input->sample_rate)>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"SOURCE PLUS DELAY TAIL EXCEEDS PORTAL LIMIT");
        double expansion=1;
        if(!strcmp(p->command,"speed")) {
            if(p->mode==1)expansion=1/r->values[0];
            else if(p->mode==2)expansion=exp2(-r->values[0]/12);
            else expansion=exp2(r->values[1]/12); /* Conservative slowest vibrato speed. */
        }
        if(p->family==TS_PORTAL_TIME && p->changes_duration && (double)input->frames*expansion+1024>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"REQUESTED TIME PROCESS EXCEEDS PORTAL LIMIT");
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i) {
            const TsPortalParam *param=&p->parameters[i];
            if(param->type==TS_PORTAL_SWITCH) {
                if(r->values[i])snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",param->flag);
            } else snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",param->flag,r->values[i]);
        }
        snprintf(c->expected_output,sizeof(c->expected_output),"output.wav");
        c->expected_output_type=TS_CDP_IO_WAV;
        *count=1;return 1;
    }
    if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
       input->frames>TS_PORTAL_MAX_FRAMES)return fail(error,size,"SPECTRAL PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
    if(input->frames<2048 || input->frames<(size_t)input->sample_rate/25)
        return fail(error,size,"SPECTRAL SOURCE NEEDS AT LEAST 2048 FRAMES AND 40 MS");
    for(size_t i=0;i<input->frames;++i)if(!isfinite(input->data[i]))
        return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
    if(!strcmp(p->executable,"focus") || !strcmp(p->executable,"hilite") || !strcmp(p->executable,"strange")) {
        for(unsigned i=0;i<p->parameter_count;++i) {
            const char *id=p->parameters[i].id;
            if((!strcmp(id,"low") || !strcmp(id,"high") || !strcmp(id,"frequency") ||
                !strcmp(id,"skirt") || !strcmp(id,"divide")) && r->values[i]>=input->sample_rate*.5)
                return fail(error,size,"SPECTRAL FREQUENCIES MUST BE BELOW SOURCE NYQUIST");
        }
        if(!strcmp(p->command,"fold") && r->values[0]<input->sample_rate/1024.0)
            return fail(error,size,"FOLD LOW HZ MUST REACH THE FIRST ANALYSIS BIN");
        if(!strcmp(p->command,"step") && (r->values[0]<256.0/input->sample_rate ||
            r->values[0]>(double)input->frames/input->sample_rate))
            return fail(error,size,"STEP MUST FIT TWO ANALYSIS HOPS AND SOURCE DURATION");
        if(!strcmp(p->command,"bltr") && r->values[0]>(double)(input->frames/128))
            return fail(error,size,"BLUR WINDOWS EXCEED SOURCE");
        if(!strcmp(p->command,"shift")) {
            if(fabs(r->values[0])>=input->sample_rate*.5)
                return fail(error,size,"FREQUENCY SHIFT MUST STAY WITHIN SOURCE NYQUIST");
            for(unsigned i=1;i<p->parameter_count;++i)
                if(r->values[i]<input->sample_rate/4096.0 || r->values[i]>input->sample_rate*.5-input->sample_rate/4096.0)
                    return fail(error,size,"SHIFT BOUNDARY MUST STAY INSIDE THE ANALYSIS EDGE BINS");
        }
        if(!strcmp(p->command,"glis") && p->mode==2 &&
           (r->values[2]<input->sample_rate/1024.0 || r->values[2]>input->sample_rate*.25))
            return fail(error,size,"PARTIAL GAP MUST FIT ANALYSIS BIN WIDTH AND QUARTER SAMPLE RATE");
        if(!strcmp(p->command,"waver") && (r->values[0]<input->sample_rate/(double)input->frames ||
           r->values[0]>input->sample_rate/256.0))
            return fail(error,size,"WAVER RATE MUST FIT SOURCE DURATION AND ANALYSIS HOP");
    }
    if(!strcmp(p->id,"blur.blur") && r->values[0]>(double)(input->frames/128))
        return fail(error,size,"BLUR WINDOWS EXCEED SOURCE; LOWER BLUR OR LOAD A LONGER SOUND");
    if(!strcmp(p->command,"spectrum")) {
        /* Match CDP's 1024-point bin mapping and compatibility checks, while
           excluding zero denominators in the above/below stretch recurrence. */
        double split=floor(r->values[0]*1024/input->sample_rate+.5),ratio=r->values[1];
        if(fabs(ratio-1)<.000001)return fail(error,size,"STRETCH RATIO 1 MAKES NO CHANGE; CHOOSE A DIFFERENT RATIO");
        if(split<2 || split>=512 ||
           (p->mode==1 && round(512*(ratio>1?1/ratio:ratio))<=split) ||
           (p->mode==2 && (ratio>=split || 1/ratio>=split || (ratio>1 && split-1<=round(ratio)))))
            return fail(error,size,"SPECTRAL SPLIT / RATIO INCOMPATIBLE AT SOURCE RATE");
    }
    if(p->changes_duration && ((double)input->frames+1024)*r->values[0]+1024>TS_PORTAL_MAX_FRAMES)
        return fail(error,size,"REQUESTED STRETCH EXCEEDS PORTAL LIMIT");
    TsCdpCommand *a=&commands[0],*c=&commands[1],*s=&commands[2];
    snprintf(a->executable,sizeof(a->executable),"pvoc");
    const char *analysis[]={"anal","1","input.wav","input.ana","-c1024","-o3"};
    for(int i=0;i<6;++i)snprintf(a->arguments[a->argc++],TS_CDP_TEXT_MAX,"%s",analysis[i]);
    snprintf(a->expected_output,sizeof(a->expected_output),"input.ana");a->expected_output_type=TS_CDP_IO_ANALYSIS;
    snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
    if(p->mode)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.ana");
    snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"effect.ana");
    for(unsigned i=0;i<p->parameter_count;++i) {
        const TsPortalParam *param=&p->parameters[i];
        if(param->type==TS_PORTAL_SWITCH) {
            if(r->values[i])snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",param->flag);
        } else snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s%.9g",param->flag,r->values[i]);
    }
    snprintf(c->expected_output,sizeof(c->expected_output),"effect.ana");c->expected_output_type=TS_CDP_IO_ANALYSIS;
    snprintf(s->executable,sizeof(s->executable),"pvoc");
    const char *synth[]={"synth","effect.ana","output.wav"};
    for(int i=0;i<3;++i)snprintf(s->arguments[s->argc++],TS_CDP_TEXT_MAX,"%s",synth[i]);
    snprintf(s->expected_output,sizeof(s->expected_output),"output.wav");s->expected_output_type=TS_CDP_IO_WAV;
    *count=3;return 1;
}

static int portal_has_custom_macros(const TsPortalRecipe *r)
{
    if(r->description[0])return 1;
    for(unsigned n=0;n<(r->stage_count?r->stage_count:1);++n)
        for(unsigned i=0;i<TS_PORTAL_PARAMS;++i)
            if(r->stage_count?r->stages[n].macros[i].configured:r->macros[i].configured)return 1;
    return 0;
}
static int portal_write_macros(FILE *f,const TsPortalRecipe *r)
{
    unsigned count=0;for(unsigned i=0;i<TS_PORTAL_PARAMS;++i)count+=r->macros[i].configured!=0;
    if(fprintf(f,"M %u |%s\n",count,r->description)<0)return 0;
    for(unsigned i=0;i<TS_PORTAL_PARAMS;++i)if(r->macros[i].configured) {
        const TsPortalMacro *m=&r->macros[i];
        if(fprintf(f,"K %u %.17g %.17g |%s\n",i,m->minimum,m->maximum,m->name)<0)return 0;
    }
    return 1;
}
static int portal_read_macros(FILE *f,TsPortalRecipe *r)
{
    char line[512];unsigned count;int used=0;
    if(!fgets(line,sizeof(line),f) || !strchr(line,'\n'))return 0;
    char *name=strchr(line,'|');
    if(!name || sscanf(line,"M %u %n",&count,&used)!=1 || line+used!=name || count>TS_PORTAL_PARAMS)return 0;
    ++name;name[strcspn(name,"\r\n")]=0;if(strlen(name)>=sizeof(r->description))return 0;
    snprintf(r->description,sizeof(r->description),"%s",name);
    for(unsigned n=0;n<count;++n) {
        unsigned i;double lo,hi;used=0;
        if(!fgets(line,sizeof(line),f) || !strchr(line,'\n'))return 0;
        name=strchr(line,'|');
        if(!name || sscanf(line,"K %u %lf %lf %n",&i,&lo,&hi,&used)!=3 || line+used!=name ||
           i>=TS_PORTAL_PARAMS || r->macros[i].configured)return 0;
        ++name;name[strcspn(name,"\r\n")]=0;if(strlen(name)>=sizeof(r->macros[i].name))return 0;
        TsPortalMacro *m=&r->macros[i];m->configured=1;m->minimum=lo;m->maximum=hi;
        snprintf(m->name,sizeof(m->name),"%s",name);
    }
    return 1;
}
static int portal_write_recipe(FILE *f,char bank,int index,const TsPortalRecipe *r)
{
    if(fprintf(f,"%c %d %s %u %u",bank,index,r->process_id,r->version,r->exposed)<0)return 0;
    for(int n=0;n<TS_PORTAL_PARAMS;++n)if(fprintf(f," %.17g",r->values[n])<0)return 0;
    return fprintf(f," |%s\n",r->name)>0;
}
static int portal_read_recipe(char *line,char *bank,int *index,TsPortalRecipe *r)
{
    int used=0;char *name=strchr(line,'|');memset(r,0,sizeof(*r));
    if(!strchr(line,'\n') || !name || sscanf(line,"%c %d %63s %u %u %n",bank,index,r->process_id,&r->version,&r->exposed,&used)!=5)return 0;
    char *at=line+used;
    for(int n=0;n<TS_PORTAL_PARAMS;++n) {
        char *end;errno=0;r->values[n]=strtod(at,&end);
        if(end==at || end>name || errno)return 0;at=end;
    }
    while(*at==' ')++at;
    if(at!=name)return 0;
    ++name;name[strcspn(name,"\r\n")]=0;
    if(strlen(name)>=sizeof(r->name))return 0;
    snprintf(r->name,sizeof(r->name),"%s",name);return 1;
}
int ts_portal_library_save(const TsPortalLibrary *lib,const char *path,char *error,size_t size)
{
    char tmp[TS_CDP_PATH_MAX]; FILE *f; int ok=1,version=1;
    if(!lib || !path || snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp))
        return fail(error,size,"INVALID PORTAL LIBRARY PATH");
    for(int bank=0;bank<2;++bank) for(int i=0;i<TS_PORTAL_SLOTS;++i) {
        const TsPortalRecipe *r=bank?&lib->pins[i]:&lib->recipes[i];
        if(r->process_id[0] && !ts_portal_recipe_validate(r,error,size)) return 0;
        if(r->stage_count && version<2)version=2;
        if(portal_has_custom_macros(r))version=3;
    }
    f=fopen(tmp,"wb"); if(!f) return fail(error,size,"CANNOT SAVE PORTAL LIBRARY");
    ok=fprintf(f,"TSCDPPORTAL %d\n",version)>0;
    for(int bank=0;bank<2;++bank) for(int i=0;i<TS_PORTAL_SLOTS;++i) {
        const TsPortalRecipe *r=bank?&lib->pins[i]:&lib->recipes[i];
        if(!r->process_id[0]) continue;
        if(r->stage_count) {
            if(fprintf(f,"C %c %d %u |%s\n",bank?'P':'R',i,r->stage_count,r->name)<0)ok=0;
            if(version==3 && !portal_write_macros(f,r))ok=0;
            for(unsigned n=0;n<r->stage_count;++n) {
                TsPortalRecipe step;ts_portal_step_get(&r->stages[n],&step);
                if(!portal_write_recipe(f,r->stages[n].bypass?'B':'E',(int)n,&step))ok=0;
                if(version==3 && !portal_write_macros(f,&step))ok=0;
            }
        } else {
            if(!portal_write_recipe(f,bank?'P':'R',i,r))ok=0;
            if(version==3 && !portal_write_macros(f,r))ok=0;
        }
    }
    if(fclose(f)!=0) ok=0;
#ifdef _WIN32
    if(ok) ok=MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok) ok=rename(tmp,path)==0;
#endif
    if(!ok) { remove(tmp); return fail(error,size,"PORTAL SAVE FAILED; PREVIOUS FILE PRESERVED"); }
    if(error && size) error[0]=0;
    return 1;
}
int ts_portal_library_load(TsPortalLibrary *lib,const char *path,char *error,size_t size)
{
    /* Heap staging keeps an expanded library off the Windows UI stack. */
    TsPortalLibrary *next;char line[2048];FILE *f;int ok=1,lines=0,version=1;
    if(!lib || !path)return fail(error,size,"INVALID PORTAL LIBRARY PATH");
    f=fopen(path,"rb");
    if(!f){if(errno==ENOENT)return 1;return fail(error,size,"CANNOT READ PORTAL LIBRARY");}
    next=calloc(1,sizeof(*next));if(!next){fclose(f);return fail(error,size,"NOT ENOUGH MEMORY FOR LIBRARY");}
    if(!fgets(line,sizeof(line),f))ok=0;
    else if(!strcmp(line,"TSCDPPORTAL 3\n"))version=3;
    else if(!strcmp(line,"TSCDPPORTAL 2\n"))version=2;
    else if(strcmp(line,"TSCDPPORTAL 1\n"))ok=0;
    while(ok && fgets(line,sizeof(line),f)) {
        TsPortalRecipe r={0};char bank=0;int index=-1;
        if(++lines>TS_PORTAL_SLOTS*2){ok=0;break;}
        if(version>=2 && line[0]=='C') {
            int used=0;char *name=strchr(line,'|');
            if(!strchr(line,'\n') || !name || sscanf(line,"C %c %d %u %n",&bank,&index,&r.stage_count,&used)!=3 ||
               line+used!=name || !r.stage_count || r.stage_count>TS_PORTAL_CHAIN_STAGES){ok=0;break;}
            ++name;name[strcspn(name,"\r\n")]=0;
            if(strlen(name)>=sizeof(r.name)){ok=0;break;}
            snprintf(r.name,sizeof(r.name),"%s",name);snprintf(r.process_id,sizeof(r.process_id),"@chain");r.version=1;
            if(version==3 && !portal_read_macros(f,&r)){ok=0;break;}
            for(unsigned n=0;n<r.stage_count;++n) {
                TsPortalRecipe step;char state=0;int order=-1;
                if(!fgets(line,sizeof(line),f) || !portal_read_recipe(line,&state,&order,&step) ||
                   order!=(int)n || (state!='E' && state!='B') || !strcmp(step.process_id,"@chain")){ok=0;break;}
                if(version==3 && (!portal_read_macros(f,&step) || step.description[0])){ok=0;break;}
                ts_portal_step_set(&r.stages[n],&step);r.stages[n].bypass=state=='B';
            }
        } else {
            if(!portal_read_recipe(line,&bank,&index,&r) || (version==3 && !portal_read_macros(f,&r))){ok=0;break;}
        }
        if(!ok || (bank!='P' && bank!='R') || index<0 || index>=TS_PORTAL_SLOTS ||
           !ts_portal_recipe_validate(&r,error,size)){ok=0;break;}
        TsPortalRecipe *dst=bank=='P'?&next->pins[index]:&next->recipes[index];
        if(dst->process_id[0]){ok=0;break;}*dst=r;
    }
    if(ferror(f))ok=0;fclose(f);
    if(ok)*lib=*next;free(next);
    if(!ok)return fail(error,size,"PORTAL LIBRARY INVALID; NOTHING LOADED");
    if(error && size)error[0]=0;return 1;
}
void ts_portal_ui_init(TsPortalUi *ui)
{
    memset(ui,0,sizeof(*ui)); ui->dragging_parameter=-1; ui->dragging_wave=-1;
    ui->history_selected=-1; ui->number_focus=-1;
    ui->selected_tab=ui->selected_slot=-1;
    ts_portal_recipe_default(&ui->recipe,ts_portal_process_at(0));
    snprintf(ui->message,sizeof(ui->message),"CHOOSE A PROCESS; PREVIEW LEAVES YOUR TILE UNCHANGED");
}
static int contains(const char *s,const char *q)
{
    if(!*q) return 1;
    for(;*s;++s) {size_t i=0;while(q[i] && s[i] && toupper((unsigned char)s[i])==toupper((unsigned char)q[i])) ++i; if(!q[i]) return 1;}
    return 0;
}
const char *ts_portal_family_name(int family)
{ return family==TS_PORTAL_WAVESET?"WAVESET":family==TS_PORTAL_SPECTRAL?"SPECTRAL":family==TS_PORTAL_TIME?"TIME / TAPE":family==TS_PORTAL_FILTER?"FILTER":family==TS_PORTAL_GRAIN?"GRAINS":family==TS_PORTAL_LOFI?"LO-FI / MOD":family==TS_PORTAL_LEVEL?"LEVEL":family==TS_PORTAL_DELAY?"DELAY":family==TS_PORTAL_ENVELOPE?"ENVELOPE":family==TS_PORTAL_STRUCTURE?"STRUCTURE":family==TS_PORTAL_INSTRUMENTS?"CHAIN TOOLS":family==TS_PORTAL_FACTORY?"FACTORY":"UNKNOWN"; }

int ts_portal_library_edit(TsPortalLibrary *lib,const char *path,int pin,int slot,
                           TsPortalEdit edit,const TsPortalRecipe *recipe,const char *name,
                           char *error,size_t size)
{
    if(!lib || (pin!=0 && pin!=1) || slot<0 || slot>=TS_PORTAL_SLOTS)
        return fail(error,size,"INVALID LIBRARY SLOT");
    TsPortalLibrary next=*lib;
    TsPortalRecipe *dst=pin?&next.pins[slot]:&next.recipes[slot];
    if(edit!=TS_PORTAL_REPLACE && !dst->process_id[0])
        return fail(error,size,"CHOOSE AN OCCUPIED SLOT");
    if(edit==TS_PORTAL_REMOVE) memset(dst,0,sizeof(*dst));
    else if(edit==TS_PORTAL_RENAME) {
        if(!name || !*name || strlen(name)>=sizeof(dst->name))
            return fail(error,size,"ENTER A NAME (1 TO 39 CHARACTERS)");
        memset(dst->name,0,sizeof(dst->name));
        snprintf(dst->name,sizeof(dst->name),"%s",name);
    } else if(edit==TS_PORTAL_UPDATE || edit==TS_PORTAL_REPLACE) {
        if(!ts_portal_recipe_validate(recipe,error,size))return 0;
        if(edit==TS_PORTAL_UPDATE) {
            if(strcmp(dst->process_id,recipe->process_id) || dst->version!=recipe->version)
                return fail(error,size,"DIFFERENT PROCESS: USE REPLACE INSTEAD");
            /* Updating controls preserves the destination's personal name. */
            char saved_name[40];memcpy(saved_name,dst->name,sizeof(saved_name));
            *dst=*recipe;memcpy(dst->name,saved_name,sizeof(saved_name));
        } else *dst=*recipe;
    } else return fail(error,size,"UNKNOWN LIBRARY ACTION");
    if(!ts_portal_library_save(&next,path,error,size))return 0;
    *lib=next;
    return 1;
}

int ts_portal_filter_slot(const TsPortalUi *ui,int row,TsPortalRecipe *out)
{
    if(row<0)return -1;
    int scalar_count=(int)(ts_portal_process_count()+ts_cdp_factory_recipe_count());
    int count=ui->tab==0?scalar_count+(int)ts_portal_instrument_count():TS_PORTAL_SLOTS;
    for(int i=0;i<count;++i) {
        TsPortalRecipe r;
        if(ui->tab==0 && i>=scalar_count)ts_portal_instrument_recipe((size_t)(i-scalar_count),&r);
        else if(ui->tab==0) ts_portal_recipe_default(&r,(size_t)i<ts_portal_process_count()?ts_portal_process_at((size_t)i):ts_portal_factory_process_at((size_t)i-ts_portal_process_count()));
        else r=ui->tab==1?ui->library.recipes[i]:ui->library.pins[i];
        const TsPortalProcess *p=ts_portal_process_find(r.process_id);
        if(ui->tab==0 && i>=scalar_count && ui->family && ui->family!=TS_PORTAL_INSTRUMENTS+1)continue;
        if(r.stage_count) {
            int family=!ui->family || ui->family==TS_PORTAL_INSTRUMENTS+1,match=contains(r.name,ui->query) || contains(r.description,ui->query) || contains("CHAIN",ui->query);
            for(unsigned n=0;n<r.stage_count;++n) {
                const TsPortalProcess *step=ts_portal_process_find(r.stages[n].process_id);
                if(step) {if((int)step->family==ui->family-1)family=1;
                    if(contains(step->title,ui->query) || contains(step->description,ui->query) || contains(step->id,ui->query) ||
                       contains(ts_portal_family_name(step->family),ui->query))match=1;}
            }
            if(family && match && row--==0){*out=r;return i;}continue;
        }
        if(!p || (ui->family && (int)p->family!=ui->family-1))continue;
        if(!contains(r.name,ui->query) && !contains(r.process_id,ui->query) &&
           !contains(p->title,ui->query) && !contains(p->description,ui->query) &&
           !contains(ts_portal_family_name(p->family),ui->query))continue;
        if(row--==0) {*out=r;return i;}
    }
    return -1;
}
int ts_portal_filter(const TsPortalUi *ui,int row,TsPortalRecipe *out)
{return ts_portal_filter_slot(ui,row,out)>=0;}
void ts_portal_wave_refresh(TsPortalWave *w,const TsSample *s)
{
    memset(w->minimum,0,sizeof(w->minimum)); memset(w->maximum,0,sizeof(w->maximum));
    if(!s || !s->data || !s->frames) return;
    if(w->last>s->frames || w->first>=w->last) {w->first=0;w->last=s->frames;}
    size_t span=w->last-w->first;
    for(size_t x=0;x<TS_PORTAL_WAVE_COLUMNS;++x) {
        size_t a=w->first+span*x/TS_PORTAL_WAVE_COLUMNS, b=w->first+span*(x+1)/TS_PORTAL_WAVE_COLUMNS;
        if(b<=a) b=a+1;
        if(b>s->frames) b=s->frames;
        float lo=1,hi=-1;
        for(size_t i=a;i<b;++i) {float v=ts_sample_read_mono(s,i);if(v<lo)lo=v;if(v>hi)hi=v;}
        w->minimum[x]=lo;w->maximum[x]=hi;
    }
}
void ts_portal_wave_reset(TsPortalWave *w,const TsSample *s)
{memset(w,0,sizeof(*w));w->last=s?s->frames:0;ts_portal_wave_refresh(w,s);}
size_t ts_portal_wave_frame(const TsPortalWave *w,int x)
{
    if(x<0)x=0;
    if(x>=TS_PORTAL_WAVE_COLUMNS)x=TS_PORTAL_WAVE_COLUMNS-1;
    return w->first+(w->last-w->first)*(size_t)x/TS_PORTAL_WAVE_COLUMNS;
}
void ts_portal_wave_zoom(TsPortalWave *w,const TsSample *s,int x,int direction)
{
    if(!s || !s->frames)return;
    size_t old=w->last-w->first, anchor=ts_portal_wave_frame(w,x);
    size_t span=direction>0?old*3/4:old+old/2+1;
    if(span<16)span=16;
    if(span>s->frames)span=s->frames;
    size_t offset=old?(anchor-w->first)*span/old:0;
    w->first=anchor>offset?anchor-offset:0;
    if(w->first>s->frames-span)w->first=s->frames-span;
    w->last=w->first+span;ts_portal_wave_refresh(w,s);
}
void ts_portal_wave_pan(TsPortalWave *w,const TsSample *s,int direction)
{
    if(!s || !s->frames)return;
    size_t span=w->last-w->first,step=span/8+1;
    if(direction<0)w->first=w->first>step?w->first-step:0;
    else w->first+=step;
    if(w->first>s->frames-span)w->first=s->frames-span;
    w->last=w->first+span;ts_portal_wave_refresh(w,s);
}

#include "ts_cdp_portal_instruments.inc"
