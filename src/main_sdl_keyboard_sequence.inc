/* UI preparation owns allocations; only bounded voices and the clock run in the callback. */
static void keyboard_sequence_ui(AudioState *audio, TsUiState *ui)
{
    if (ui->keyboard_sequence_clear_deadline &&
        (int32_t)(SDL_GetTicks() - ui->keyboard_sequence_clear_deadline) >= 0)
        ui->keyboard_sequence_clear_deadline = 0;
    TsKeyboardSequence *s = &audio->keyboard_sequence;
    if (s->settings.seconds == 0) ts_keyboard_sequence_init(s);
    ts_keyboard_sequence_set_slot_policy(s, ui->config.arp_slot_min_full_pattern);
    ui->keyboard_sequence = s->settings;
    ui->keyboard_sequence_running = ts_keyboard_sequence_active(s);
    ui->keyboard_sequence_current = s->running && s->elapsed < s->settings.seconds * s->settings.gate ? s->current_note : -1;
    ui->keyboard_sequence_step = s->running ? s->cursor + 1 : 0;
    ui->keyboard_sequence_progress = s->running ? fmin(1, s->elapsed / s->settings.seconds) : 0;
    ui->keyboard_sequence_level = s->effective_gain;
    ui->keyboard_sequence_slot = s->active_slot;
    ui->keyboard_slot_sequence = s->bank.sequence;
    ui->keyboard_slot_running = s->slot_running;
    ui->keyboard_slot_remaining = s->slot_running ? fmax(0, s->slot_duration - s->slot_elapsed) : 0;
    ui->keyboard_sequence_occupied = 0;
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i)
        if (s->bank.slot[i].count) ui->keyboard_sequence_occupied |= (uint16_t)(1u << i);
}

static void keyboard_sequence_sample_stamp(uint64_t *hash, const TsSample *sample)
{
    state_hash_bytes(hash, &sample->data, sizeof(sample->data));
    state_hash_bytes(hash, &sample->frames, sizeof(sample->frames));
    state_hash_bytes(hash, &sample->sample_rate, sizeof(sample->sample_rate));
    state_hash_bytes(hash, &sample->visual_revision, sizeof(sample->visual_revision));
}

static int keyboard_sequence_prepare(SDL_AudioDeviceID device, AudioState *audio,
    TsUiState *ui, const TsInstrument *instrument, const TsSample *fm, int rate, int force)
{
    uint16_t mask = 0;
    int running;
    if (device) SDL_LockAudioDevice(device);
    keyboard_sequence_ui(audio, ui);
    running = ts_keyboard_sequence_active(&audio->keyboard_sequence);
    if (!ui->fm_open && audio->sister.enabled &&
        (audio->sister.source_switches & TS_SISTER_SOURCE_TILES))
        mask = ts_sister_runtime_source_mask(&audio->sister);
    if (!mask && audio->performance_group_latched) mask = audio->performance_source_mask;
    if (device) SDL_UnlockAudioDevice(device);
    if (!running && !force) return 1;
    if (!device || rate <= 0) return 0;
    int loop = ui->keyboard_hold || ui->workbench_loop_active;
    uint64_t stamp = 1469598103934665603ull;
#define STAMP(value) state_hash_bytes(&stamp, &(value), sizeof(value))
    STAMP(mask); STAMP(rate); STAMP(loop); STAMP(ui->fm_open); STAMP(ui->play_view);
    STAMP(ui->parent_view_first); STAMP(ui->parent_view_last); STAMP(ui->audition_source);
    STAMP(ui->config.voice_attack_ms); STAMP(instrument->selected_slot);
    STAMP(instrument->tuning); STAMP(instrument->audible_tuning);
    STAMP(instrument->has_loop); STAMP(instrument->loop_first); STAMP(instrument->loop_last);
    STAMP(instrument->loop_mode); STAMP(instrument->loop_crossfade_ms);
    STAMP(instrument->has_selection); STAMP(instrument->selection_first); STAMP(instrument->selection_last);
    STAMP(instrument->view_first); STAMP(instrument->view_last);
    keyboard_sequence_sample_stamp(&stamp, &instrument->current);
    keyboard_sequence_sample_stamp(&stamp, &instrument->parent);
    if (ui->fm_open && fm) keyboard_sequence_sample_stamp(&stamp, fm);
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) if (mask & (1u << i)) {
        const TsBankSlot *slot = &instrument->bank[i];
        keyboard_sequence_sample_stamp(&stamp, &slot->sample);
        STAMP(slot->occupied); STAMP(slot->has_loop); STAMP(slot->loop_first);
        STAMP(slot->loop_last); STAMP(slot->loop_mode); STAMP(slot->loop_crossfade_ms);
        STAMP(slot->audible_tuning);
    }
#undef STAMP
    if (!force && stamp == audio->keyboard_sequence_source_stamp) return 1;
    TsKeyboardSequenceSource *source = calloc(1, sizeof(*source));
    if (!source) return 0;
    TsNoteEvent root;
    ts_note_event_qwerty(&root, 0, TS_KEYBOARD_BASE_NOTE);
    source->fm = ui->fm_open;
    if (mask && !source->fm) {
        source->group = calloc(1, sizeof(*source->group));
        if (source->group) {
            ts_performance_init(source->group);
            source->group->keyboard_loop = loop;
            ts_performance_set_attack_ms(source->group, ui->config.voice_attack_ms);
            ts_performance_trigger_group_event(source->group, instrument, mask, &root, 1, rate);
            for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
                const TsPerformanceVoice *p = &source->group->voices[i];
                if (!p->active || source->count == TS_BANK_SLOT_COUNT) continue;
                TsNoteVoice *v = &source->voices[source->count++];
                v->sample = p->sample; v->position = p->position; v->step = p->step;
                v->range_first = p->range_first; v->range_last = p->range_last;
                v->crossfade_frames = p->crossfade_frames; v->attack_frames = p->attack_frames;
                v->loop_mode = p->loop_mode; v->looping = p->looping;
                v->direction = p->direction; v->loop_intro = p->loop_intro;
                v->gain = p->gain * p->group_gain; v->active = 1;
            }
        }
    } else {
        TsNoteBank bank;
        ts_note_bank_init(&bank);
        bank.workbench_loop = loop; bank.play_view = ui->play_view;
        bank.parent_view_first = ui->parent_view_first; bank.parent_view_last = ui->parent_view_last;
        ts_note_bank_set_attack_ms(&bank, ui->config.voice_attack_ms);
        const TsTuning unity = {TS_KEYBOARD_BASE_NOTE, 0};
        TsNoteStartResult result = source->fm ?
            ts_note_bank_start_sample_event(&bank, fm, &unity, &root, 1, rate) :
            ts_note_bank_start_tuned_event(&bank, instrument, ts_ui_audition_tuning(ui, instrument),
                                           ui->audition_source, &root, 1, rate);
        if (result == TS_NOTE_STARTED && ts_sample_clone(&source->sample, bank.voices[0].sample, NULL, 0)) {
            source->voices[0] = bank.voices[0];
            source->voices[0].sample = &source->sample; source->count = 1;
        }
    }
    if (!source->count) { ts_keyboard_sequence_source_free(source); source = NULL; }
    if (device) SDL_LockAudioDevice(device);
    TsKeyboardSequenceSource *old = ts_keyboard_sequence_source(&audio->keyboard_sequence, source);
    audio->keyboard_sequence_source_stamp = stamp;
    keyboard_sequence_ui(audio, ui);
    if (device) SDL_UnlockAudioDevice(device);
    ts_keyboard_sequence_source_free(old);
    if (!source) snprintf(ui->status, sizeof(ui->status), "ARP STOPPED - NO PLAYABLE SOURCE");
    return source != NULL;
}

/* The panel and shortcut share transport, including capture staging. */
static void keyboard_sequence_toggle_playback(SDL_AudioDeviceID device, AudioState *audio,
    TsUiState *ui, const TsInstrument *instrument, const TsSample *fm, int rate)
{
    if (device) SDL_LockAudioDevice(device);
    keyboard_sequence_ui(audio, ui);
    TsKeyboardSequence *s = &audio->keyboard_sequence;
    int stopping = ts_keyboard_sequence_active(s);
    if (stopping) ts_keyboard_sequence_stop(s);
    keyboard_sequence_ui(audio, ui);
    if (device) SDL_UnlockAudioDevice(device);
    if (stopping) {
        snprintf(ui->status, sizeof(ui->status), "ARP STOPPED / SHIFT+SPACE TO PLAY");
        return;
    }
    if (!ui->keyboard_sequence.count) {
        snprintf(ui->status, sizeof(ui->status), "ARP: SELECT KEYS OR COPY HELD NOTES FIRST");
        return;
    }
    if (!keyboard_sequence_prepare(device, audio, ui, instrument, fm, rate, 1)) return;
    SDL_LockAudioDevice(device);
    ts_keyboard_sequence_play(s); keyboard_sequence_ui(audio, ui);
    if (audio->capture.state != TS_CAPTURE_RECORDING) audio->playing = 0;
    audio->bank_slot = -1;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) {
        if (s->source->fm) {
            (void)runtime_capture_set_source(audio, TS_CAPTURE_SOURCE_SYNTH, NULL, 0);
            (void)runtime_capture_trigger(audio, NULL, 0);
        } else if (runtime_capture_source_matches(audio, instrument->selected_slot)) {
            if (audio->performance_group_latched) audio->capture_trigger_ready = 1;
            (void)runtime_capture_trigger(audio, NULL, 0);
        }
    }
    SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "ARP PLAYING / TEAL SELECTED, GOLD SOUNDING / STOP RELEASES ARP ONLY");
}

static int keyboard_sequence_transport_event(const SDL_Event *event, SDL_Window *window,
    SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, SisterWindow *sister,
    const TsInstrument *instrument, const TsSample *fm, int rate)
{
    if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_SPACE ||
        !(event->key.keysym.mod & KMOD_SHIFT) ||
        (event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return 0;
    if (event->key.windowID != SDL_GetWindowID(window) &&
        !(sister->window && event->key.windowID == sister->window_id)) return 0;
    if (!sister_performance_keys_allowed(ui) || ui->midi_learn_active ||
        sister->model.preset_manage_open || sister->model.fallout_lfo_open) return 0;
    if (event->key.repeat) return 1;
    keyboard_sequence_toggle_playback(device, audio, ui, instrument, fm, rate);
    snprintf(ui->fm_message, sizeof(ui->fm_message), "%.*s", (int)sizeof(ui->fm_message) - 1, ui->status);
    snprintf(sister->model.status, sizeof(sister->model.status), "%.*s", (int)sizeof(sister->model.status) - 1, ui->status);
    return 1;
}

static void keyboard_sequence_capture_held(AudioState *audio)
{
    TsKeyboardSequenceSettings next = audio->keyboard_sequence.settings;
    unsigned char pitches[128] = {0};
    for (int i = 0; i < TS_NOTE_BANK_VOICE_CAPACITY; ++i) {
        const TsNoteVoice *v = &audio->notes.voices[i];
        if (v->active && !v->preview && v->origin == TS_NOTE_ORIGIN_QWERTY && v->midi_note >= 0 && v->midi_note < 128)
            pitches[v->midi_note] = 1;
    }
    const TsPerformanceBank *banks[] = {&audio->performance, &audio->sister.performance};
    for (int b = 0; b < 2; ++b) for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        const TsPerformanceVoice *v = &banks[b]->voices[i];
        if (v->active && !v->tile_launched && v->origin == TS_NOTE_ORIGIN_QWERTY && v->midi_note >= 0 && v->midi_note < 128)
            pitches[v->midi_note] = 1;
    }
    next.count = 0;
    for (int n = 0; n < 128 && next.count < TS_KEYBOARD_SEQUENCE_NOTES; ++n)
        if (pitches[n]) next.notes[next.count++] = n;
    ts_keyboard_sequence_set(&audio->keyboard_sequence, &next);
}

static void keyboard_sequence_slider(TsKeyboardSequenceSettings *s, int slider, int x)
{
    double amount = slider == 1 ? (x - 20) / 248.0 : slider == 2 ? (x - 282) / 136.0 :
                    slider == 3 ? (x - 20) / 188.0 : slider == 4 ? (x - 296) / 164.0 :
                    (x - 476) / 144.0;
    amount = fmax(0, fmin(1, amount));
    if (slider == 1) s->seconds = TS_KEYBOARD_SEQUENCE_MIN_SECONDS *
        pow(TS_KEYBOARD_SEQUENCE_MAX_SECONDS / TS_KEYBOARD_SEQUENCE_MIN_SECONDS, amount);
    else if (slider == 2) s->gate = .05 + .95 * amount;
    else if (slider == 3) s->volume = 2 * amount;
    else if (slider == 4) s->lfo_seconds = TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS *
        pow(TS_KEYBOARD_SEQUENCE_MAX_SECONDS / TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS, amount);
    else s->lfo_depth = amount;
}

static int keyboard_sequence_event(const SDL_Event *event, SDL_Window *window,
    SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
    const TsInstrument *instrument, const TsSample *fm, int rate)
{
    if (event->type == SDL_MOUSEBUTTONUP && ui->keyboard_sequence_drag) {
        ui->keyboard_sequence_drag = 0; return 1;
    }
    if (!ts_ui_keyboard_sequence_visible(ui) || !sister_performance_keys_allowed(ui)) {
        ui->keyboard_sequence_clear_deadline = 0;
        ui->keyboard_sequence_drag = 0; return 0;
    }
    Uint32 id = event->type == SDL_KEYDOWN ? event->key.windowID :
        event->type == SDL_MOUSEWHEEL ? event->wheel.windowID :
        event->type == SDL_MOUSEMOTION ? event->motion.windowID : event->button.windowID;
    if (id != SDL_GetWindowID(window)) return 0;
    int x = -1, y = -1, note = -1, slider = 0;
    double wheel = 0;
    SDL_Keymod mod = SDL_GetModState();
    if (event->type == SDL_KEYDOWN) {
        if (!ui->keyboard_sequence_open || !ui->keyboard_sequence_edit ||
            (event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return 0;
        if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) return 0;
        note = note_for_key(event->key.keysym.sym);
        if (note < 0) return 0;
        if (event->key.repeat) return 1;
    } else if (event->type == SDL_MOUSEBUTTONDOWN) {
        logical_mouse(window, event->button.x, event->button.y, &x, &y);
        if (x >= 10 && x < 56 && y >= 313 && y < 330) {
            ui->keyboard_sequence_clear_deadline = 0;
            ui->keyboard_sequence_open = !ui->keyboard_sequence_open;
            if (ui->keyboard_sequence_open) ui->keyboard_sequence_edit = 1;
            return 1;
        }
        note = ts_ui_key_from_point_for_base(x, y, ts_ui_keyboard_base_note(ui));
        if (note >= 0 && event->button.button == SDL_BUTTON_LEFT &&
            ((mod & KMOD_CTRL) || (ui->keyboard_sequence_open && ui->keyboard_sequence_edit))) {
            /* Capture staging keeps its established Shift-click ownership. */
            if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER) return 0;
        } else note = -1;
    } else if (event->type == SDL_MOUSEWHEEL) {
        int raw_x, raw_y; wheel_event_mouse(event, &raw_x, &raw_y);
        logical_mouse(window, raw_x, raw_y, &x, &y);
        wheel = wheel_event_delta(event, 0);
    } else if (event->type == SDL_MOUSEMOTION && ui->keyboard_sequence_drag) {
        if (!(event->motion.state & SDL_BUTTON_LMASK)) { ui->keyboard_sequence_drag = 0; return 0; }
        logical_mouse(window, event->motion.x, event->motion.y, &x, &y);
        slider = ui->keyboard_sequence_drag;
    } else return 0;
    int panel_y = ui->fm_open ? TS_ARP_PANEL_FM_Y : TS_ARP_PANEL_Y;
    int panel_h = ui->fm_open ? TS_ARP_PANEL_FM_H : TS_ARP_PANEL_H;
    int panel = ui->keyboard_sequence_open && x >= TS_ARP_PANEL_X &&
        x < TS_ARP_PANEL_X + TS_ARP_PANEL_W && y >= panel_y && y < panel_y + panel_h;
    if (note < 0 && !panel && !slider) return 0;
    /* Horizontal/zero wheel events are not pointer clicks and cannot arm a drag. */
    if (event->type == SDL_MOUSEWHEEL && wheel == 0) return 1;
    /* Controls share their main-view hit geometry after translating FM's inset. */
    if (ui->fm_open) y -= TS_ARP_PANEL_FM_OFFSET;
    int clear_click = panel && event->type == SDL_MOUSEBUTTONDOWN &&
        event->button.button == SDL_BUTTON_LEFT && y >= 134 && y < 157 && x >= 164 && x < 264;
    if (!clear_click && (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_KEYDOWN ||
                         event->type == SDL_MOUSEWHEEL)) ui->keyboard_sequence_clear_deadline = 0;
    if (device) SDL_LockAudioDevice(device);
    keyboard_sequence_ui(audio, ui);
    TsKeyboardSequence *s = &audio->keyboard_sequence;
    TsKeyboardSequenceSettings next = s->settings;
    int toggle_playback = 0;
    if (note >= 0) {
        int ok = ts_keyboard_sequence_toggle(s, ts_ui_keyboard_base_note(ui) + note);
        snprintf(ui->status, sizeof(ui->status), ok ?
            "ARP: %d NOTES / CTRL+CLICK OR EDIT KEYS TO CHANGE ORDER" : "ARP: %d NOTE LIMIT", s->settings.count);
    } else if (panel || slider) {
        if (!slider && event->type == SDL_MOUSEBUTTONDOWN &&
            event->button.button == SDL_BUTTON_LEFT && y >= 134 && y < 157) {
            if (x >= 20 && x < 84) {
                ui->keyboard_sequence_clipboard = s->settings;
                ui->keyboard_sequence_clipboard_valid = 1;
                snprintf(ui->status, sizeof(ui->status), "COPIED ARP %02d NOTES AND SETTINGS", s->active_slot + 1);
            } else if (x >= 90 && x < 154) {
                if (ui->keyboard_sequence_clipboard_valid) {
                    ts_keyboard_sequence_paste(s, &ui->keyboard_sequence_clipboard);
                    ui->keyboard_sequence_edit = 1;
                    snprintf(ui->status, sizeof(ui->status), "PASTED INTO ARP %02d / EDIT ON", s->active_slot + 1);
                } else snprintf(ui->status, sizeof(ui->status), "ARP CLIPBOARD EMPTY - COPY A SLOT FIRST");
            } else if (clear_click) {
                if (ui->keyboard_sequence_clear_deadline) {
                    ts_keyboard_sequence_clear_all(s);
                    ui->keyboard_sequence_clear_deadline = 0;
                    ui->keyboard_sequence_edit = 1;
                    snprintf(ui->status, sizeof(ui->status), "ALL 16 ARP SLOTS CLEARED / ARP STOPPED / CLIPBOARD KEPT");
                } else {
                    ui->keyboard_sequence_clear_deadline = SDL_GetTicks() + 3000;
                    snprintf(ui->status, sizeof(ui->status), "CLICK CONFIRM WITHIN 3 SECONDS TO CLEAR ALL 16 ARP SLOTS");
                }
            }
            keyboard_sequence_ui(audio, ui);
            if (device) SDL_UnlockAudioDevice(device);
            return 1;
        }
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT &&
            y >= 198 && y < 215 && x >= 20 && x < 612) {
            int slot = (x - 20) / 37;
            int copied = slot != s->active_slot && !s->bank.slot[slot].count &&
                         ts_keyboard_sequence_active(s) && s->settings.count;
            if ((x - 20) % 37 < 34 && ts_keyboard_sequence_select_slot(s, slot)) {
                if (copied || !s->settings.count) ui->keyboard_sequence_edit = 1;
                snprintf(ui->status, sizeof(ui->status), "ARP %02d / %s / SAVE PROJECT TO KEEP", slot + 1,
                         copied ? "COPIED LIVE - EDIT YOUR VARIATION" :
                         ts_keyboard_sequence_active(s) ? "PLAYING" : s->settings.count ? "READY - PLAY TO START" : "EMPTY - SELECT NOTES THEN PLAY");
            }
            keyboard_sequence_ui(audio, ui);
            if (device) SDL_UnlockAudioDevice(device);
            return 1;
        }
        if ((!slider && y >= 164 && y < 190) || slider == 6) {
            TsKeyboardSlotSequence q = s->bank.sequence;
            double maximum = fmax(q.seconds, fmax(1, ui->config.arp_slot_max_seconds));
            if (slider == 6 || (x >= 226 && x < 426)) {
                if (event->type == SDL_MOUSEWHEEL) q.seconds *= pow((mod & KMOD_SHIFT) ? 1.01 : 1.1, -wheel);
                else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT) q.seconds = 10;
                else if (event->type == SDL_MOUSEMOTION ||
                         (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
                    double amount = fmax(0, fmin(1, (x - 226) / 200.0));
                    q.seconds = TS_KEYBOARD_SLOT_MIN_SECONDS * pow(maximum / TS_KEYBOARD_SLOT_MIN_SECONDS, amount);
                    ui->keyboard_sequence_drag = 6;
                }
                q.seconds = fmax(TS_KEYBOARD_SLOT_MIN_SECONDS, fmin(maximum, q.seconds));
            } else if (event->type == SDL_MOUSEBUTTONDOWN) {
                if (x >= 20 && x < 110) q.enabled = !q.enabled;
                else if (x >= 116 && x < 216)
                    q.mode = (q.mode + (event->button.button == SDL_BUTTON_RIGHT ? 4 : 1)) % TS_KEYBOARD_SEQUENCE_MODE_COUNT;
                else if (x >= 436 && x < 516) q.loop = !q.loop;
            }
            ts_keyboard_sequence_set_slot_sequence(s, &q);
            keyboard_sequence_ui(audio, ui);
            if (device) SDL_UnlockAudioDevice(device);
            return 1;
        }
        if (!slider && y >= 281 && y < 307) slider = x >= 20 && x < 268 ? 1 : x >= 282 && x < 418 ? 2 : 0;
        if (!slider && y >= 220 && y < 246) slider = x >= 20 && x < 208 ? 3 :
            x >= 296 && x < 460 ? 4 : x >= 476 && x < 620 ? 5 : 0;
        if (slider) {
            if (event->type == SDL_MOUSEWHEEL) {
                if (slider == 1) next.seconds *= pow((mod & KMOD_SHIFT) ? 1.01 : 1.1, -wheel);
                else if (slider == 2) next.gate += wheel * ((mod & KMOD_SHIFT) ? .01 : .05);
                else if (slider == 3) next.volume += wheel * ((mod & KMOD_SHIFT) ? .01 : .05);
                else if (slider == 4) next.lfo_seconds *= pow((mod & KMOD_SHIFT) ? 1.01 : 1.1, -wheel);
                else next.lfo_depth += wheel * ((mod & KMOD_SHIFT) ? .01 : .05);
            } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT) {
                if (slider == 1) next.seconds = .25;
                else if (slider == 2) next.gate = .8;
                else if (slider == 3) next.volume = 1;
                else if (slider == 4) next.lfo_seconds = 4;
                else next.lfo_depth = .5;
            } else if (event->type == SDL_MOUSEMOTION ||
                       (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
                keyboard_sequence_slider(&next, slider, x);
                ui->keyboard_sequence_drag = slider;
            }
            ts_keyboard_sequence_set(s, &next);
        } else if (event->type == SDL_MOUSEBUTTONDOWN && y >= 220 && y < 243 && x >= 220 && x < 284) {
            next.lfo_enabled = !next.lfo_enabled;
            ts_keyboard_sequence_set(s, &next);
        } else if (event->type == SDL_MOUSEBUTTONDOWN && y >= 252 && y < 275) {
            if (x >= 20 && x < 84) {
                toggle_playback = 1;
            } else if (x >= 90 && x < 190) {
                next.mode = (next.mode + (event->button.button == SDL_BUTTON_RIGHT ? 4 : 1)) % TS_KEYBOARD_SEQUENCE_MODE_COUNT;
                ts_keyboard_sequence_set(s, &next);
            } else if (x >= 196 && x < 254) ts_keyboard_sequence_reset(s);
            else if (x >= 260 && x < 318) { next.count = 0; ts_keyboard_sequence_set(s, &next); }
            else if (x >= 324 && x < 424) keyboard_sequence_capture_held(audio);
            else if (x >= 430 && x < 510) ui->keyboard_sequence_edit = !ui->keyboard_sequence_edit;
            else if (x >= 516 && x < 580) { next.loop = !next.loop; ts_keyboard_sequence_set(s, &next); }
            else if (x >= 586) ui->keyboard_sequence_open = 0;
        }
    }
    keyboard_sequence_ui(audio, ui);
    if (device) SDL_UnlockAudioDevice(device);
    if (toggle_playback) keyboard_sequence_toggle_playback(device, audio, ui, instrument, fm, rate);
    return 1;
}
