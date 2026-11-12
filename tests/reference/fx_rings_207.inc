/* Frozen ring readers and grain/delay/reverb kernels from PR #207,
   63ea32c84047cb40c03f8839eb02373c25424b91. Deliberately retain floor and
   remainder operations. Shared helpers outside these kernels are unchanged. */
static float reference_delay_read(const float *data, size_t capacity, size_t write,
                        float delay_frames, size_t channel)
{
    double position;
    size_t i0, i1;
    float fraction;
    if (data == NULL || capacity < 2u) return 0.0f;
    delay_frames = clampf(delay_frames, 1.0f, (float)(capacity - 1u));
    position = (double)write - (double)delay_frames;
    while (position < 0.0) position += (double)capacity;
    while (position >= (double)capacity) position -= (double)capacity;
    i0 = (size_t)floor(position);
    i1 = (i0 + 1u) % capacity;
    fraction = (float)(position - (double)i0);
    return data[i0 * 2u + channel] +
        (data[i1 * 2u + channel] - data[i0 * 2u + channel]) * fraction;
}

static float reference_grain_read(const TsSisterGrainState *state, double position,
                        size_t channel)
{
    size_t i0, i1;
    float fraction;
    if (state->data == NULL || state->capacity_frames < 2u) return 0.0f;
    while (position < 0.0) position += (double)state->capacity_frames;
    while (position >= (double)state->capacity_frames)
        position -= (double)state->capacity_frames;
    i0 = (size_t)position;
    i1 = (i0 + 1u) % state->capacity_frames;
    fraction = (float)(position - (double)i0);
    return state->data[i0 * 2u + channel] +
        (state->data[i1 * 2u + channel] -
         state->data[i0 * 2u + channel]) * fraction;
}

static TsStereoFrame reference_grain_process(const TsSisterFxTiming *engine,
                                   TsSisterGrainState *state,
                                   const TsSisterFxSlotControls *control,
                                   TsStereoFrame input, float gate, float *direct, int fast_silence)
{
    TsStereoFrame wet = {0.0f, 0.0f};
    float energy = 0.0f;
    float active;
    if (state->gain_db_cached != control->gain_db) {
        state->gain_db_cached = control->gain_db;
        state->gain_target = makeup_gain_linear(control->gain_db);
    }
    float target[3]={control->parameter_a,control->parameter_b,control->parameter_c};
    if(state->state_serial!=control->grain_states.recall_serial) {
        state->state_serial=control->grain_states.recall_serial;
        state->state_from[0]=state->size_current;state->state_from[1]=state->density_current;state->state_from[2]=state->pitch_current;
        memcpy(state->state_to,target,sizeof(target));
        state->state_density_from=state->density_hz_current;
        state->state_density_to=ts_sister_grain_density_hz(target[1]);
        state->state_total=state->state_remaining=state->state_serial && !control->grain_states.editing?
            (uint32_t)(control->grain_states.seconds*engine->sample_rate):0;
    }
    /* Manual parameter edits cancel the current recall. A subsequent recall
       starts at the audible position, including when the old ramp is unfinished. */
    if(state->state_remaining && (control->grain_states.editing || memcmp(state->state_to,target,sizeof(target))))state->state_remaining=0;
    if(state->state_remaining) {
        float t=1.0f-(float)(--state->state_remaining)/(float)state->state_total;
        for(int p=0;p<3;++p)target[p]=state->state_from[p]+(state->state_to[p]-state->state_from[p])*t;
        state->density_hz_target=state->state_density_from+(state->state_density_to-state->state_density_from)*t;
        state->density_cached=target[1];
    } else if(state->density_cached!=target[1]) {
        state->density_cached=target[1];state->density_hz_target=ts_sister_grain_density_hz(target[1]);
    }
    state->size_current=approach(state->size_current,target[0],engine->smooth_30ms);
    state->density_current=approach(state->density_current,target[1],engine->smooth_30ms);
    state->density_hz_current=approach(state->density_hz_current,state->density_hz_target,engine->smooth_30ms);
    state->pitch_current=approach(state->pitch_current,target[2],engine->smooth_24ms);
    state->mix_current = approach(state->mix_current,
        control->mix, engine->smooth_20ms);
    state->gain_current = approach(state->gain_current,
        state->gain_target, engine->smooth_20ms);
    state->route_current = approach(state->route_current,
        clampf(gate, 0.0f, 1.0f), engine->smooth_12ms);
    active = clampf(gate, 0.0f, 1.0f);

    state->spawn_phase += state->density_hz_current /
                          (double)engine->sample_rate;
    if (state->spawn_phase >= state->spawn_threshold) {
        state->spawn_phase -= state->spawn_threshold;
        /* Built-in asynchronous scheduling prevents sparse clouds from
           becoming a rigid pulse train. The mean interval remains one grain
           period, and the deterministic target-local PRNG keeps recalls
           reproducible. */
        state->spawn_threshold = 0.70 +
            0.60 * (double)grain_random_unit(state);
        if (active > 1.0e-5f &&
            (state->mix_current > FLT_EPSILON || control->mix > 0.0f))
            grain_spawn(state);
    }
    /* All reads precede this frame's write. Exact empty history means every
       interpolation is zero, even when input resumes on this sample. */
    int empty=fast_silence && !state->nonzero_samples;
    if(empty) {
        /* Advance voice positions/ages exactly; never freeze the schedule. */
        for(size_t i=0;i<TS_SISTER_GRAIN_VOICES;++i) {
            TsSisterGrainVoice *voice=&state->voice[i];
            if(!voice->active)continue;
            if(voice->age>=voice->total){voice->active=0;continue;}
            voice->read_position+=voice->read_step;
            while(voice->read_position>=(double)state->capacity_frames)
                voice->read_position-=(double)state->capacity_frames;
            ++voice->age;
            if(voice->age>=voice->total)voice->active=0;
        }
    } else {
    for (size_t i = 0u; i < TS_SISTER_GRAIN_VOICES; ++i) {
        TsSisterGrainVoice *voice = &state->voice[i];
        float phase;
        float window;
        if (!voice->active) continue;
        if (voice->age >= voice->total) {
            voice->active = 0;
            continue;
        }
        phase = voice->total > 1u ?
            (float)voice->age / (float)(voice->total - 1u) : 1.0f;
        /* A squared parabolic window is a close, deliberately cheap Hann
           analogue. At maximum density this avoids millions of sinf calls
           per second on the X220/X230 while retaining a smooth zero-valued
           attack and release. */
        window = 4.0f * phase * (1.0f - phase);
        window *= window;
        wet.l += reference_grain_read(state, voice->read_position, 0u) *
                 window * voice->pan_l;
        wet.r += reference_grain_read(state, voice->read_position, 1u) *
                 window * voice->pan_r;
        energy += window * window;
        voice->read_position += voice->read_step;
        while (voice->read_position >= (double)state->capacity_frames)
            voice->read_position -= (double)state->capacity_frames;
        ++voice->age;
        if (voice->age >= voice->total) voice->active = 0;
    }
    }
    if (energy > 1.0f) {
        float normalization = 1.0f / sqrtf(energy);
        wet.l *= normalization;
        wet.r *= normalization;
    }
    wet.l = feedback_condition(wet.l * 0.86f);
    wet.r = feedback_condition(wet.r * 0.86f);
    wet = ts_stereo_frame_sanitize_inline(wet);

    state->nonzero_samples-=(state->data[state->write_index*2]!=0)+(state->data[state->write_index*2+1]!=0);
    state->data[state->write_index * 2u] = input.l * active;
    state->data[state->write_index * 2u + 1u] = input.r * active;
    state->nonzero_samples+=(state->data[state->write_index*2]!=0)+(state->data[state->write_index*2+1]!=0);
    state->write_index = (state->write_index + 1u) % state->capacity_frames;
    if (state->history_frames < state->capacity_frames)
        ++state->history_frames;

    return slot_mix(input,wet,clampf(state->mix_current*active,0,1),
        &state->mix_cache,state->gain_current,active,0,direct);
}

static TsStereoFrame reference_delay_process(const TsSisterFxTiming *engine,
                                   TsSisterDelayState *state,
                                   const TsSisterFxSlotControls *control,
                                   TsStereoFrame input, float gate, float *direct, int fast_silence)
{
    TsStereoFrame wet = {0.0f, 0.0f};
    TsStereoFrame feedback_read = {0.0f, 0.0f};
    static const float tap_ratio[TS_SISTER_DELAY_TAPS] = {
        0.29f, 0.47f, 0.71f, 1.0f
    };
    static const float tap_weight[TS_SISTER_DELAY_TAPS] = {
        0.30f, 0.38f, 0.48f, 0.72f
    };
    static const float tap_pan_l[TS_SISTER_DELAY_TAPS] = {
        0.89442719f, 0.48989795f, 0.8f, 0.63245553f
    };
    static const float tap_pan_r[TS_SISTER_DELAY_TAPS] = {
        0.44721360f, 0.87177979f, 0.6f, 0.77459667f
    };
    float requested = ts_sister_delay_time_ms(control->parameter_a) *
                      0.001f * engine->sample_rate;
    float difference;
    float follow;
    float wow_frames;
    float flutter_frames;
    float mod_l;
    float mod_r;
    float active;
    if (state->gain_db_cached != control->gain_db) {
        state->gain_db_cached = control->gain_db;
        state->gain_target = makeup_gain_linear(control->gain_db);
    }
    state->delay_target = requested;
    difference = state->delay_target - state->delay_current;
    follow = difference * state->follow_coefficient;
    /* A lengthening delay reads the tape as slowly as half speed; a
       shortening delay reads it as fast as double speed. The resulting
       octave-down/octave-up limits make abrupt wheel moves dramatic but
       continuous, while slow LFO movement follows naturally. */
    follow = clampf(follow, -1.0f, 0.5f);
    if (fabsf(difference) <= fabsf(follow))
        state->delay_current = state->delay_target;
    else
        state->delay_current += follow;
    state->feedback_current = approach(state->feedback_current,
        control->parameter_b, engine->smooth_20ms);
    state->mix_current = approach(state->mix_current,
        control->mix, engine->smooth_20ms);
    state->gain_current = approach(state->gain_current,
        state->gain_target, engine->smooth_20ms);
    state->route_current = approach(state->route_current,
        clampf(gate, 0.0f, 1.0f), engine->smooth_12ms);
    active = clampf(gate, 0.0f, 1.0f);
    if (!state->has_history &&
        ((state->mix_current <= FLT_EPSILON && control->mix <= 0.0f) ||
         active <= FLT_EPSILON))
        { *direct=1+(state->gain_current-1)*active;
          return effect_makeup(input, state->gain_current, active); }
    if (active *
        fmaxf(fabsf(input.l), fabsf(input.r)) > 1.0e-12f)
        state->has_history = 1;
    /* Ring occupancy is exact: a quiet gap is not proof that an echo ended.
       Keep smoothing, phase, ring cursors and transitions advancing when empty. */
    int empty=fast_silence && !state->nonzero_samples && !input.l && !input.r;
    for(int t=0;empty && t<TS_SISTER_DELAY_TAPS;++t)empty=!state->tap_tone[t][0] && !state->tap_tone[t][1];
    empty=empty && !state->feedback_tone[0] && !state->feedback_tone[1];
    if(empty)goto delay_advance;
    wow_frames = (0.03f + 0.16f * control->parameter_a) * 0.001f *
                 engine->sample_rate;
    flutter_frames = 0.012f * 0.001f * engine->sample_rate;
    mod_l = state->wow_sin * wow_frames +
            state->flutter_sin * flutter_frames;
    mod_r = (-0.216440f * state->wow_sin +
              0.976296f * state->wow_cos) * wow_frames +
            (0.613746f * state->flutter_sin +
             0.789504f * state->flutter_cos) * flutter_frames;
    for (size_t tap = 0u; tap < TS_SISTER_DELAY_TAPS; ++tap) {
        float delay_frames = state->delay_current * tap_ratio[tap];
        float read_l = reference_delay_read(state->data, state->capacity_frames,
            state->write_index, delay_frames + mod_l * tap_ratio[tap], 0u);
        float read_r = reference_delay_read(state->data, state->capacity_frames,
            state->write_index, delay_frames + mod_r * tap_ratio[tap], 1u);
        float alpha = state->tap_alpha[tap];
        float center;
        float side;
        state->tap_tone[tap][0] +=
            (read_l - state->tap_tone[tap][0]) * alpha;
        state->tap_tone[tap][1] +=
            (read_r - state->tap_tone[tap][1]) * alpha;
        state->tap_tone[tap][0] = flush_tiny(state->tap_tone[tap][0]);
        state->tap_tone[tap][1] = flush_tiny(state->tap_tone[tap][1]);
        center = (state->tap_tone[tap][0] +
                  state->tap_tone[tap][1]) * 0.5f;
        side = (state->tap_tone[tap][0] -
                state->tap_tone[tap][1]) * 0.5f;
        wet.l += tap_weight[tap] *
            (center * tap_pan_l[tap] + side * 0.55f);
        wet.r += tap_weight[tap] *
            (center * tap_pan_r[tap] - side * 0.55f);
        if (tap == TS_SISTER_DELAY_TAPS - 1u) {
            feedback_read.l = read_l * 0.94f + read_r * 0.06f;
            feedback_read.r = read_r * 0.94f + read_l * 0.06f;
        }
    }
    {
        float feedback_alpha = state->feedback_alpha_bright +
            (state->feedback_alpha_dark - state->feedback_alpha_bright) *
            state->feedback_current;
        state->feedback_tone[0] +=
            (feedback_read.l - state->feedback_tone[0]) * feedback_alpha;
        state->feedback_tone[1] +=
            (feedback_read.r - state->feedback_tone[1]) * feedback_alpha;
        state->feedback_tone[0] = flush_tiny(state->feedback_tone[0]);
        state->feedback_tone[1] = flush_tiny(state->feedback_tone[1]);
    }
    state->nonzero_samples-=(state->data[state->write_index*2]!=0)+(state->data[state->write_index*2+1]!=0);
    state->data[state->write_index * 2u] = feedback_condition(tape_saturate(
        input.l * active +
        state->feedback_tone[0] * (state->feedback_current * 1.08f)));
    state->data[state->write_index * 2u + 1u] = feedback_condition(tape_saturate(
        input.r * active +
        state->feedback_tone[1] * (state->feedback_current * 1.08f)));
    state->nonzero_samples+=(state->data[state->write_index*2]!=0)+(state->data[state->write_index*2+1]!=0);
delay_advance:
    state->write_index = (state->write_index + 1u) % state->capacity_frames;
    {
        float wow_sin = state->wow_sin;
        float wow_cos = state->wow_cos;
        float flutter_sin = state->flutter_sin;
        float flutter_cos = state->flutter_cos;
        state->wow_sin = wow_sin * state->wow_step_cos +
                         wow_cos * state->wow_step_sin;
        state->wow_cos = wow_cos * state->wow_step_cos -
                         wow_sin * state->wow_step_sin;
        state->flutter_sin = flutter_sin * state->flutter_step_cos +
                             flutter_cos * state->flutter_step_sin;
        state->flutter_cos = flutter_cos * state->flutter_step_cos -
                             flutter_sin * state->flutter_step_sin;
    }
    if (++state->modulation_renormalize >= 4096u) {
        float wow_magnitude = sqrtf(state->wow_sin * state->wow_sin +
                                    state->wow_cos * state->wow_cos);
        float flutter_magnitude = sqrtf(
            state->flutter_sin * state->flutter_sin +
            state->flutter_cos * state->flutter_cos);
        state->modulation_renormalize = 0u;
        if (wow_magnitude > 0.0f && isfinite(wow_magnitude)) {
            state->wow_sin /= wow_magnitude;
            state->wow_cos /= wow_magnitude;
        }
        if (flutter_magnitude > 0.0f && isfinite(flutter_magnitude)) {
            state->flutter_sin /= flutter_magnitude;
            state->flutter_cos /= flutter_magnitude;
        }
    }
    wet.l = tape_saturate(wet.l);
    wet.r = tape_saturate(wet.r);
    return slot_mix(input,wet,clampf(state->mix_current*active,0,1),
        &state->mix_cache,state->gain_current,active,0,direct);
}

static TsStereoFrame reference_reverb_process(const TsSisterFxTiming *engine,
                                    TsSisterReverbState *state,
                                    const TsSisterFxSlotControls *control,
                                    TsStereoFrame input, float gate, float *direct, int fast_silence)
{
    float read_l[TS_SISTER_REVERB_LINES];
    float read_r[TS_SISTER_REVERB_LINES];
    float mean_l = 0.0f, mean_r = 0.0f;
    float damping;
    float active;
    TsStereoFrame wet = {0.0f, 0.0f};
    static const float output_l[TS_SISTER_REVERB_LINES] = {
         1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f
    };
    static const float output_r[TS_SISTER_REVERB_LINES] = {
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f
    };
    if (state->gain_db_cached != control->gain_db) {
        state->gain_db_cached = control->gain_db;
        state->gain_target = makeup_gain_linear(control->gain_db);
    }
    if (control->parameter_a != state->size_target) {
        int interrupted = 0;
        state->size_target = control->parameter_a;
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line)
            interrupted |= state->line[line].transition_remaining > 0u;
        if (interrupted)
            read_handoff_begin(&state->read_handoff,
                (uint32_t)(0.060f * engine->sample_rate));
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
            state->line[line].old_delay_frames =
                state->line[line].new_delay_frames;
            state->line[line].new_delay_frames = reverb_delay_ms(
                state->size_target, line) * 0.001f * engine->sample_rate;
            state->line[line].transition_total =
                (uint32_t)(0.060f * engine->sample_rate);
            state->line[line].transition_remaining =
                state->line[line].transition_total;
        }
    }
    state->mix_current = approach(state->mix_current,
        control->mix, engine->smooth_24ms);
    state->gain_current = approach(state->gain_current,
        state->gain_target, engine->smooth_20ms);
    state->decay_current = approach(state->decay_current,
        control->parameter_b, engine->smooth_35ms);
    state->size_current = approach(state->size_current,
        control->parameter_a, engine->smooth_55ms);
    state->route_current = approach(state->route_current,
        clampf(gate, 0.0f, 1.0f), engine->smooth_18ms);
    active = clampf(gate, 0.0f, 1.0f);
    if (!state->has_history &&
        ((state->mix_current <= FLT_EPSILON && control->mix <= 0.0f) ||
         active <= FLT_EPSILON))
        { *direct=1+(state->gain_current-1)*active;
          return effect_makeup(input, state->gain_current, active); }
    if (active *
        fmaxf(fabsf(input.l), fabsf(input.r)) > 1.0e-12f)
        state->has_history = 1;
    if (!state->feedback_valid || state->feedback_decay_cached != state->decay_current ||
        state->feedback_size_cached != state->size_target) {
        float decay_seconds = ts_sister_reverb_decay_seconds(state->decay_current);
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
            float seconds = state->line[line].new_delay_frames / engine->sample_rate;
            state->feedback_gain[line] = powf(0.001f, seconds / decay_seconds);
        }
        state->feedback_decay_cached = state->decay_current;
        state->feedback_size_cached = state->size_target;
        state->feedback_valid = 1;
    }
    /* Ring occupancy is exact: a quiet gap is not proof that an echo ended.
       Keep smoothing, phase, ring cursors and transitions advancing when empty. */
    int empty=fast_silence && !state->nonzero_samples && !input.l && !input.r;
    for(int i=0;empty && i<TS_SISTER_REVERB_LINES;++i)empty=!state->line[i].damping[0] && !state->line[i].damping[1];
    damping = 0.90f + (1.0f - state->size_current) * 0.05f +
              (1.0f - state->decay_current) * 0.03f;
    for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
        TsSisterReverbLine *delay = &state->line[line];
        float depth_frames = (0.08f + state->size_current * 0.48f) *
                             0.001f * engine->sample_rate;
        float oscillator_sin = state->modulation_sin[line];
        float oscillator_cos = state->modulation_cos[line];
        float mod_l = oscillator_sin * depth_frames;
        float mod_r = (-0.159519f * oscillator_sin +
                        0.987195f * oscillator_cos) * depth_frames;
        if(empty) {
            read_l[line]=read_r[line]=0;
            if(delay->transition_remaining)--delay->transition_remaining;
        } else {
        read_l[line] = reference_delay_read(delay->data, delay->capacity_frames,
                                  delay->write_index,
                                  delay->new_delay_frames + mod_l, 0u);
        read_r[line] = reference_delay_read(delay->data, delay->capacity_frames,
                                  delay->write_index,
                                  delay->new_delay_frames + mod_r, 1u);
        if (delay->transition_remaining > 0u) {
            float amount = 1.0f - (float)delay->transition_remaining /
                                      (float)delay->transition_total;
            read_l[line] = read_l[line] * amount +
                reference_delay_read(delay->data, delay->capacity_frames,
                           delay->write_index,
                           delay->old_delay_frames + mod_l, 0u) *
                (1.0f - amount);
            read_r[line] = read_r[line] * amount +
                reference_delay_read(delay->data, delay->capacity_frames,
                           delay->write_index,
                           delay->old_delay_frames + mod_r, 1u) *
                (1.0f - amount);
            --delay->transition_remaining;
        }
        mean_l += read_l[line] * (1.0f / (float)TS_SISTER_REVERB_LINES);
        mean_r += read_r[line] * (1.0f / (float)TS_SISTER_REVERB_LINES);
        wet.l += (read_l[line] * output_l[line] +
                  read_r[line] * output_r[line] * 0.16f) * 0.35355339f;
        wet.r += (read_r[line] * output_r[line] -
                  read_l[line] * output_l[line] * 0.16f) * 0.35355339f;
        }
        state->modulation_sin[line] =
            oscillator_sin * state->modulation_step_cos[line] +
            oscillator_cos * state->modulation_step_sin[line];
        state->modulation_cos[line] =
            oscillator_cos * state->modulation_step_cos[line] -
            oscillator_sin * state->modulation_step_sin[line];
    }
    if (++state->modulation_renormalize >= 4096u) {
        state->modulation_renormalize = 0u;
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
            float magnitude = sqrtf(
                state->modulation_sin[line] * state->modulation_sin[line] +
                state->modulation_cos[line] * state->modulation_cos[line]);
            if (magnitude > 0.0f && isfinite(magnitude)) {
                state->modulation_sin[line] /= magnitude;
                state->modulation_cos[line] /= magnitude;
            } else {
                state->modulation_sin[line] = 0.0f;
                state->modulation_cos[line] = 1.0f;
            }
        }
    }
    /* Retain the spacious common mode as well as the decorrelated output
       vectors. Omitting it makes a centered sustained source appear wide but
       lets its reverb body vanish through matrix cancellation. */
    wet.l += mean_l * 1.15f + mean_r * 0.08f;
    wet.r += mean_r * 1.15f + mean_l * 0.08f;
    for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
        TsSisterReverbLine *delay = &state->line[line];
        if(empty){delay->write_index=(delay->write_index+1u)%delay->capacity_frames;continue;}
        float gain = state->feedback_gain[line];
        float matrix_l = 2.0f * mean_l - read_l[line];
        float matrix_r = 2.0f * mean_r - read_r[line];
        float polarity = output_l[line];
        float inject_l = active *
            (input.l * (0.155f + 0.006f * (float)line +
                        0.035f * polarity) +
             input.r * polarity * 0.045f);
        float inject_r = active *
            (input.r * (0.155f + 0.006f * (float)line +
                        0.035f * output_r[line]) -
             input.l * output_r[line] * 0.045f);
        delay->damping[0] += (matrix_l - delay->damping[0]) * damping;
        delay->damping[1] += (matrix_r - delay->damping[1]) * damping;
        delay->damping[0] = flush_tiny(delay->damping[0]);
        delay->damping[1] = flush_tiny(delay->damping[1]);
        state->nonzero_samples-=(delay->data[delay->write_index*2]!=0)+(delay->data[delay->write_index*2+1]!=0);
        delay->data[delay->write_index * 2u] = reverb_saturate(
            inject_l + delay->damping[0] * gain);
        delay->data[delay->write_index * 2u + 1u] = reverb_saturate(
            inject_r + delay->damping[1] * gain);
        state->nonzero_samples+=(delay->data[delay->write_index*2]!=0)+(delay->data[delay->write_index*2+1]!=0);
        delay->write_index = (delay->write_index + 1u) % delay->capacity_frames;
    }
    wet.l = feedback_condition(wet.l);
    wet.r = feedback_condition(wet.r);
    wet = ts_stereo_frame_sanitize_inline(wet);
    wet = read_handoff_apply(&state->read_handoff, wet);
    return slot_mix(input,wet,clampf(state->mix_current*active,0,1),
        &state->mix_cache,state->gain_current,active,1,direct);
}
