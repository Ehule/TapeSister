#include "tapesister/cdp_portal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Audited against CDP8 dev/distort/ap_distort.c (usage2) and
   dev/cdp2k/tklib1.c (ranges). Expansion factors deliberately capped at 16
   for this first release; group/skip ranges retain CDP's full 32767 limit.
   Each mode has a stable identity. No invented SCRAMBLE macro parameters. */
#define GROUP(lo, def) {"cycles", "CYCLE GROUP", "NUMBER OF WAVECYCLES IN EACH GROUP", "", TS_PORTAL_INTEGER, lo, 32767, def}
#define SKIP {"skip", "SKIP CYCLES", "LEAVE THESE INITIAL WAVECYCLES UNPROCESSED", "-s", TS_PORTAL_INTEGER, 0, 32767, 0}
#define MULT {"multiplier", "REPEATS", "TIMES EACH WAVECYCLE GROUP IS REPEATED", "", TS_PORTAL_INTEGER, 2, 16, 2}
#define CYCLEFLAG {"cycles", "CYCLE GROUP", "NUMBER OF WAVECYCLES IN EACH GROUP", "-c", TS_PORTAL_INTEGER, 1, 32767, 8}
#define ACUITY {"acuity", "ACUITY", "SMALLER VALUES MAKE A NARROWER, MORE RESONANT FILTER", "", TS_PORTAL_REAL, .05, 1, .5}
#define FILTER_GAIN {"gain", "OUTPUT GAIN", "LINEAR OUTPUT MULTIPLIER; RESONANCE CAN STILL BOOST LEVEL", "", TS_PORTAL_REAL, .01, 1, .5}
#define FILTER_FREQ {"frequency", "FREQUENCY HZ", "20 TO 6000 HZ; ALSO LIMITED TO ONE SIXTH OF SOURCE RATE", "", TS_PORTAL_REAL, 20, 6000, 1000}
#define FILTER_TAIL {"tail", "TAIL SECONDS", "APPEND 0.01 TO 2 SECONDS FOR FILTER DECAY", "-t", TS_PORTAL_REAL, .01, 2, .25}
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
         {"frequency", "FREQ SCATTER", "MAXIMUM RANDOM PARTIAL-FREQUENCY RATIO; 1 IS UNCHANGED", "", TS_PORTAL_REAL, 1, 4, 1.01}}, TS_PORTAL_SPECTRAL, "blur"},
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
         FILTER_TAIL}, TS_PORTAL_FILTER, "filter"}
};
#undef GROUP
#undef SKIP
#undef MULT
#undef CYCLEFLAG
#undef ACUITY
#undef FILTER_GAIN
#undef FILTER_FREQ
#undef FILTER_TAIL

static int fail(char *error, size_t size, const char *message)
{ if (error && size) snprintf(error, size, "%s", message); return 0; }
size_t ts_portal_process_count(void) { return sizeof(processes)/sizeof(processes[0]); }
const TsPortalProcess *ts_portal_process_at(size_t index)
{ return index < ts_portal_process_count() ? &processes[index] : NULL; }
const TsPortalProcess *ts_portal_process_find(const char *id)
{
    if (id) for (size_t i=0; i<ts_portal_process_count(); ++i)
        if (!strcmp(id, processes[i].id)) return &processes[i];
    return NULL;
}
void ts_portal_recipe_default(TsPortalRecipe *r, const TsPortalProcess *p)
{
    memset(r, 0, sizeof(*r));
    if (!p) return;
    snprintf(r->process_id, sizeof(r->process_id), "%s", p->id);
    snprintf(r->name, sizeof(r->name), "%s", p->title);
    r->version=p->version;
    r->exposed=(1u<<p->parameter_count)-1u;
    for (unsigned i=0;i<p->parameter_count;++i) r->values[i]=p->parameters[i].initial;
}
int ts_portal_recipe_validate(const TsPortalRecipe *r, char *error, size_t size)
{
    const TsPortalProcess *p;
    if (!r || !memchr(r->process_id,0,sizeof(r->process_id)) ||
        !memchr(r->name,0,sizeof(r->name))) return fail(error,size,"MALFORMED RECIPE");
    p=ts_portal_process_find(r->process_id);
    if (!p || r->version!=p->version) return fail(error,size,"UNKNOWN PROCESS OR RECIPE VERSION");
    if (r->exposed >> p->parameter_count) return fail(error,size,"INVALID MACRO MASK");
    for (const char *s=r->name;*s;++s)
        if ((unsigned char)*s<32 || (unsigned char)*s>126 || *s=='|')
            return fail(error,size,"INVALID RECIPE NAME");
    for (unsigned i=0;i<TS_PORTAL_PARAMS;++i) {
        double v=r->values[i];
        if (!isfinite(v)) return fail(error,size,"NONFINITE PARAMETER");
        if (i>=p->parameter_count) { if(v!=0) return fail(error,size,"UNUSED PARAMETER IS NOT ZERO"); continue; }
        const TsPortalParam *s=&p->parameters[i];
        if(v<s->minimum || v>s->maximum || (s->type!=TS_PORTAL_REAL && v!=floor(v)))
            return fail(error,size,"PARAMETER OUTSIDE CDP RANGE");
    }
    if (!strcmp(p->command,"omit") && r->values[0]>=r->values[1])
        return fail(error,size,"OMIT A MUST BE LESS THAN EVERY B");
    if (p->family==TS_PORTAL_FILTER && !strcmp(p->command,"sweeping") && r->values[2]>=r->values[3])
        return fail(error,size,"LOW HZ MUST BE BELOW HIGH HZ");
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
    if(p->family!=TS_PORTAL_WAVESET)return fail(error,size,"USE THE PROCESS COMMAND PLAN FOR THIS FAMILY");
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
    memset(commands,0,sizeof(*commands)*TS_CDP_MAX_STAGES);
    if(p->family==TS_PORTAL_WAVESET) {
        if(!ts_portal_build_command(r,input,&commands[0],error,size))return 0;
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
            if((!strcmp(id,"frequency") || !strcmp(id,"low") || !strcmp(id,"high")) &&
               r->values[i]>(double)input->sample_rate/6)
                return fail(error,size,"FILTER FREQUENCY MUST NOT EXCEED SOURCE RATE / 6");
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
    if(p->family==TS_PORTAL_TIME) {
        if(!input || !input->data || input->channels!=1 || !input->sample_rate ||
           input->frames<2 || input->frames>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"TIME PROCESSES REQUIRE A MONO SOURCE WITHIN THE PORTAL LIMIT");
        if(input->frames<(size_t)input->sample_rate/25)
            return fail(error,size,"TIME SOURCE NEEDS AT LEAST 40 MS");
        for(size_t i=0;i<input->frames;++i)if(!isfinite(input->data[i]))
            return fail(error,size,"SOURCE CONTAINS NONFINITE AUDIO");
        double expansion=1;
        if(!strcmp(p->command,"speed")) {
            if(p->mode==1)expansion=1/r->values[0];
            else if(p->mode==2)expansion=exp2(-r->values[0]/12);
            else expansion=exp2(r->values[1]/12); /* Conservative slowest vibrato speed. */
        }
        if(p->changes_duration && (double)input->frames*expansion+1024>TS_PORTAL_MAX_FRAMES)
            return fail(error,size,"REQUESTED TIME PROCESS EXCEEDS PORTAL LIMIT");
        TsCdpCommand *c=&commands[0];
        snprintf(c->executable,sizeof(c->executable),"%s",p->executable);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%s",p->command);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%u",p->mode);
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"input.wav");
        snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"output.wav");
        for(unsigned i=0;i<p->parameter_count;++i)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%.9g",r->values[i]);
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
    if(!strcmp(p->id,"blur.blur") && r->values[0]>(double)(input->frames/128))
        return fail(error,size,"BLUR WINDOWS EXCEED SOURCE; LOWER BLUR OR LOAD A LONGER SOUND");
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
    for(unsigned i=0;i<p->parameter_count;++i)snprintf(c->arguments[c->argc++],TS_CDP_TEXT_MAX,"%.9g",r->values[i]);
    snprintf(c->expected_output,sizeof(c->expected_output),"effect.ana");c->expected_output_type=TS_CDP_IO_ANALYSIS;
    snprintf(s->executable,sizeof(s->executable),"pvoc");
    const char *synth[]={"synth","effect.ana","output.wav"};
    for(int i=0;i<3;++i)snprintf(s->arguments[s->argc++],TS_CDP_TEXT_MAX,"%s",synth[i]);
    snprintf(s->expected_output,sizeof(s->expected_output),"output.wav");s->expected_output_type=TS_CDP_IO_WAV;
    *count=3;return 1;
}

int ts_portal_library_save(const TsPortalLibrary *lib,const char *path,char *error,size_t size)
{
    char tmp[TS_CDP_PATH_MAX]; FILE *f; int ok=1;
    if(!lib || !path || snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp))
        return fail(error,size,"INVALID PORTAL LIBRARY PATH");
    for(int bank=0;bank<2;++bank) for(int i=0;i<TS_PORTAL_SLOTS;++i) {
        const TsPortalRecipe *r=bank?&lib->pins[i]:&lib->recipes[i];
        if(r->process_id[0] && !ts_portal_recipe_validate(r,error,size)) return 0;
    }
    f=fopen(tmp,"wb"); if(!f) return fail(error,size,"CANNOT SAVE PORTAL LIBRARY");
    ok=fprintf(f,"TSCDPPORTAL 1\n")>0;
    for(int bank=0;bank<2;++bank) for(int i=0;i<TS_PORTAL_SLOTS;++i) {
        const TsPortalRecipe *r=bank?&lib->pins[i]:&lib->recipes[i];
        if(!r->process_id[0]) continue;
        if(fprintf(f,"%c %d %s %u %u",bank?'P':'R',i,r->process_id,r->version,r->exposed)<0) ok=0;
        for(int n=0;n<TS_PORTAL_PARAMS;++n) if(fprintf(f," %.17g",r->values[n])<0) ok=0;
        if(fprintf(f," |%s\n",r->name)<0) ok=0;
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
    TsPortalLibrary next={0}; char line[2048]; FILE *f; int ok=1,lines=0;
    if(!lib || !path) return fail(error,size,"INVALID PORTAL LIBRARY PATH");
    f=fopen(path,"rb");
    if(!f) { if(errno==ENOENT) return 1; return fail(error,size,"CANNOT READ PORTAL LIBRARY"); }
    if(!fgets(line,sizeof(line),f) || strcmp(line,"TSCDPPORTAL 1\n")) ok=0;
    while(ok && fgets(line,sizeof(line),f)) {
        TsPortalRecipe r={0}; char bank=0, extra; int index=-1,used=0; char *name=strchr(line,'|');
        if(++lines>TS_PORTAL_SLOTS*2 || !strchr(line,'\n') || !name ||
           sscanf(line,"%c %d %63s %u %u %n",&bank,&index,r.process_id,&r.version,&r.exposed,&used)!=5 ||
           (bank!='R' && bank!='P') || index<0 || index>=TS_PORTAL_SLOTS) {ok=0;break;}
        char *at=line+used;
        for(int n=0;n<TS_PORTAL_PARAMS;++n) {
            char *end; errno=0; r.values[n]=strtod(at,&end);
            if(end==at || end>name || errno) {ok=0;break;} at=end;
        }
        while(*at==' ') ++at;
        if(!ok || at!=name) {ok=0;break;}
        ++name; name[strcspn(name,"\r\n")]=0;
        if(strlen(name)>=sizeof(r.name)) {ok=0;break;}
        snprintf(r.name,sizeof(r.name),"%s",name);
        (void)extra;
        TsPortalRecipe *dst=bank=='P'?&next.pins[index]:&next.recipes[index];
        if(dst->process_id[0] || !ts_portal_recipe_validate(&r,error,size)) {ok=0;break;}
        *dst=r;
    }
    if(ferror(f)) ok=0;
    fclose(f);
    if(!ok) return fail(error,size,"PORTAL LIBRARY INVALID; NOTHING LOADED");
    *lib=next; if(error && size) error[0]=0; return 1;
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
{ return family==TS_PORTAL_WAVESET?"WAVESET":family==TS_PORTAL_SPECTRAL?"SPECTRAL":family==TS_PORTAL_TIME?"TIME / TAPE":family==TS_PORTAL_FILTER?"FILTER":"UNKNOWN"; }

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
    int count=ui->tab==0?(int)ts_portal_process_count():TS_PORTAL_SLOTS;
    for(int i=0;i<count;++i) {
        TsPortalRecipe r;
        if(ui->tab==0) ts_portal_recipe_default(&r,ts_portal_process_at((size_t)i));
        else r=ui->tab==1?ui->library.recipes[i]:ui->library.pins[i];
        const TsPortalProcess *p=ts_portal_process_find(r.process_id);
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
