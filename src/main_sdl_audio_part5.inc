                  field == (int)ui->config_field ? mouse_color : edge);
        cfg_text(fb, 24, y + 5,
                 ts_config_field_name((TsConfigField)field),
                 field == (int)ui->config_field ? mouse_color : text_color);
        cfg_text(fb, TS_AUDIO_CONFIG_VALUE_X, y + 5, shown, effect_color);
        if (field == (int)ui->config_field && ui->text_cursor_visible &&
            cursor >= first && cursor - first <= visible) {
            int cx = TS_AUDIO_CONFIG_VALUE_X + (int)(cursor - first) * 6;
            cfg_rect(fb, cx, y + 3, 1, 11, mouse_color);
        }
    }

    {
        const char *input = ui->config.record_input_device[0] != '\0' ?
                            ui->config.record_input_device : "SYSTEM DEFAULT";
        const char *output = ui->config.audio_output_device[0] != '\0' ?
                             ui->config.audio_output_device : "SYSTEM DEFAULT";
        const char *midi = ui->config.midi_input_device[0] != '\0' ?
                           ui->config.midi_input_device : "AUTO FIRST";
        char shown[96];
        char channel[24];
        char buffer[24];

        cfg_frame(fb, 20, TS_AUDIO_CONFIG_INPUT_Y,
                  TS_AUDIO_CONFIG_INPUT_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, 28, TS_AUDIO_CONFIG_INPUT_Y + 5, "<", text_color);
        cfg_text(fb, 44, TS_AUDIO_CONFIG_INPUT_Y + 5, "INPUT", text_color);
        snprintf(shown, sizeof(shown), "%.52s", input);
        cfg_text(fb, 88, TS_AUDIO_CONFIG_INPUT_Y + 5, shown, text_color);
        cfg_text(fb, 432, TS_AUDIO_CONFIG_INPUT_Y + 5, ">", text_color);

        cfg_frame(fb, TS_AUDIO_CONFIG_CHANNEL_X, TS_AUDIO_CONFIG_INPUT_Y,
                  TS_AUDIO_CONFIG_CHANNEL_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 5,
                 TS_AUDIO_CONFIG_INPUT_Y + 5, "<", text_color);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 22,
                 TS_AUDIO_CONFIG_INPUT_Y + 5, "CH", text_color);
        snprintf(channel, sizeof(channel), "%s",
                 ts_input_channel_mode_name(
                     ui->config.record_input_channel));
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 58,
                 TS_AUDIO_CONFIG_INPUT_Y + 5, channel, text_color);
        cfg_text(fb, 608, TS_AUDIO_CONFIG_INPUT_Y + 5, ">", text_color);

        cfg_frame(fb, 20, TS_AUDIO_CONFIG_OUTPUT_Y,
                  TS_AUDIO_CONFIG_INPUT_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, 28, TS_AUDIO_CONFIG_OUTPUT_Y + 5, "<", text_color);
        cfg_text(fb, 44, TS_AUDIO_CONFIG_OUTPUT_Y + 5, "OUTPUT", text_color);
        snprintf(shown, sizeof(shown), "%.52s", output);
        cfg_text(fb, 94, TS_AUDIO_CONFIG_OUTPUT_Y + 5, shown, text_color);
        cfg_text(fb, 432, TS_AUDIO_CONFIG_OUTPUT_Y + 5, ">", text_color);

        cfg_frame(fb, TS_AUDIO_CONFIG_CHANNEL_X, TS_AUDIO_CONFIG_OUTPUT_Y,
                  TS_AUDIO_CONFIG_CHANNEL_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 5,
                 TS_AUDIO_CONFIG_OUTPUT_Y + 5, "<", text_color);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 22,
                 TS_AUDIO_CONFIG_OUTPUT_Y + 5, "BUF", text_color);
        snprintf(buffer, sizeof(buffer), "%d", ui->config.audio_buffer_frames);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 58,
                 TS_AUDIO_CONFIG_OUTPUT_Y + 5, buffer, text_color);
        cfg_text(fb, 608, TS_AUDIO_CONFIG_OUTPUT_Y + 5, ">", text_color);

        cfg_frame(fb, 20, TS_MIDI_CONFIG_INPUT_Y,
                  TS_AUDIO_CONFIG_INPUT_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, 28, TS_MIDI_CONFIG_INPUT_Y + 5, "<", text_color);
        cfg_text(fb, 44, TS_MIDI_CONFIG_INPUT_Y + 5, "MIDI", text_color);
        snprintf(shown, sizeof(shown), "%.54s", midi);
        cfg_text(fb, 82, TS_MIDI_CONFIG_INPUT_Y + 5, shown, text_color);
        cfg_text(fb, 432, TS_MIDI_CONFIG_INPUT_Y + 5, ">", text_color);

        cfg_frame(fb, TS_AUDIO_CONFIG_CHANNEL_X, TS_MIDI_CONFIG_INPUT_Y,
                  TS_AUDIO_CONFIG_CHANNEL_W, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 5,
                 TS_MIDI_CONFIG_INPUT_Y + 5, "<", text_color);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 22,
                 TS_MIDI_CONFIG_INPUT_Y + 5, "CH", text_color);
        if (ui->config.midi_input_channel == 0)
            snprintf(channel, sizeof(channel), "OMNI");
        else
            snprintf(channel, sizeof(channel), "%d", ui->config.midi_input_channel);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 52,
                 TS_MIDI_CONFIG_INPUT_Y + 5, channel, text_color);
        cfg_text(fb, 608, TS_MIDI_CONFIG_INPUT_Y + 5, ">", text_color);
    }
}

static void ts_discover_tile_bounds(void)
{
    if (ts_tile_bounds_ready) return;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        ts_tile_bounds[slot].x0 = TS_UI_WIDTH;
        ts_tile_bounds[slot].y0 = TS_UI_HEIGHT;
        ts_tile_bounds[slot].x1 = -1;
        ts_tile_bounds[slot].y1 = -1;
        ts_tile_bounds[slot].valid = 0;
    }
    for (int y = 0; y < TS_UI_HEIGHT; ++y) {
        for (int x = 0; x < TS_UI_WIDTH; ++x) {
            int slot = ts_ui_bank_slot_from_point(x, y);
            TsTileBounds *bounds;
            if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) continue;
            bounds = &ts_tile_bounds[slot];
            if (x < bounds->x0) bounds->x0 = x;
            if (x > bounds->x1) bounds->x1 = x;
            if (y < bounds->y0) bounds->y0 = y;
            if (y > bounds->y1) bounds->y1 = y;
            bounds->valid = 1;
        }
    }
    ts_tile_bounds_ready = 1;
}

static uint32_t ts_dim_color(uint32_t color, int percent)
{
    unsigned factor = (unsigned)(100 - percent);
    unsigned a = color & 0xff000000u;
    unsigned r = ((color >> 16) & 0xffu) * factor / 100u;
    unsigned g = ((color >> 8) & 0xffu) * factor / 100u;
    unsigned b = (color & 0xffu) * factor / 100u;
    return a | (r << 16) | (g << 8) | b;
}

static void ts_draw_tile_border(TsFramebuffer *fb, const TsTileBounds *bounds,
                                uint32_t color)
{
    if (fb == NULL || bounds == NULL || !bounds->valid) return;
    for (int thickness = 0; thickness < 2; ++thickness) {
        int x0 = bounds->x0 + thickness;
        int y0 = bounds->y0 + thickness;
        int x1 = bounds->x1 - thickness;
        int y1 = bounds->y1 - thickness;
        if (x1 <= x0 || y1 <= y0) continue;
        for (int x = x0; x <= x1; ++x) {
            fb->pixels[y0 * TS_UI_WIDTH + x] = color;
            fb->pixels[y1 * TS_UI_WIDTH + x] = color;
        }
        for (int y = y0; y <= y1; ++y) {
            fb->pixels[y * TS_UI_WIDTH + x0] = color;
            fb->pixels[y * TS_UI_WIDTH + x1] = color;
        }
    }
}

static void runtime_prune_sources(AudioState *audio,
                                  const TsInstrument *instrument)
{
    uint16_t valid = 0u;
    TsCaptureRecorder *recorder;
    if (audio == NULL || !audio->performance_group_latched ||
        instrument == NULL) return;
    recorder = &audio->capture;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        uint16_t bit = (uint16_t)(1u << slot);
        if ((audio->performance_source_mask & bit) == 0u) continue;
        if (instrument->bank[slot].occupied &&
            (slot != recorder->destination_slot || recorder->overdub))
            valid |= bit;
    }
    if (valid == audio->performance_source_mask) return;
    audio->performance_source_mask = valid;
    if (valid == 0u) {
        audio->performance_group_latched = 0;
        if (recorder->state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER)
            recorder->source_slot = -1;
    } else if (recorder->state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
               (recorder->source_slot < 0 ||
                (valid & (uint16_t)(1u << recorder->source_slot)) == 0u)) {
        recorder->source_slot = ts_first_source(valid);
    }
}

static void ts_overlay_tile_states(TsFramebuffer *fb, const TsUiState *ui,
                                   const TsInstrument *instrument)
{
    const AudioState *audio = (const AudioState *)ts_output_userdata;
    uint32_t source_color;
    if (fb == NULL || ui == NULL || instrument == NULL || ui->show_keyboard ||
        ui->show_recipes || ui->show_ingredients ||
        ts_ui_foreground_panel_open(ui)) return;
    ts_discover_tile_bounds();
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        const TsTileBounds *bounds = &ts_tile_bounds[slot];
        if (!bounds->valid || !instrument->bank[slot].occupied ||
            !instrument->bank[slot].locked || ts_locked_tile_dim_percent <= 0)
            continue;
        for (int y = bounds->y0; y <= bounds->y1; ++y)
            for (int x = bounds->x0; x <= bounds->x1; ++x) {
                size_t at = (size_t)y * TS_UI_WIDTH + (size_t)x;
                fb->pixels[at] = ts_dim_color(fb->pixels[at],
                                              ts_locked_tile_dim_percent);
            }
    }
    /* State borders are redrawn after protected-tile dimming so lock state
       never obscures routing or active-canvas identity. */
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        ts_ui_draw_tile_state_borders(
            fb, slot, instrument->selected_slot == slot,
            (ui->sister_source_mask & (uint16_t)(1u << slot)) != 0u,
            &ui->palette);
    if (audio == NULL || !audio->performance_group_latched ||
        audio->performance_source_mask == 0u) return;
    source_color = ui->palette.colors[TS_PALETTE_PATTERN_EFFECT];
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if ((audio->performance_source_mask & (uint16_t)(1u << slot)) != 0u)
            ts_draw_tile_border(fb, &ts_tile_bounds[slot], source_color);
}

static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument)
{
    TsUiState *mutable_ui = (TsUiState *)ui;
    AudioState *audio = (AudioState *)ts_output_userdata;
    ts_live_ui = mutable_ui;
    ts_live_config = mutable_ui != NULL ? &mutable_ui->config : ts_live_config;

    if (mutable_ui != NULL && ts_last_sample_page != mutable_ui->sample_page) {
        if (ts_last_sample_page >= 0 && audio != NULL &&
            audio->capture.state == TS_CAPTURE_IDLE)
            runtime_clear_group(audio);
        ts_last_sample_page = mutable_ui->sample_page;
    }
    if (audio != NULL && audio->capture.state == TS_CAPTURE_IDLE &&
        audio->performance_group_latched)
        runtime_clear_group(audio);
    ts_audio_lock_device(TS_AUDIO_LOGICAL_OUTPUT);
    runtime_prune_sources(audio, instrument);

    if (mutable_ui != NULL && !mutable_ui->config_open &&
        ts_pending_notice[0] != '\0') {
        snprintf(mutable_ui->status, sizeof(mutable_ui->status),
                 "%s", ts_pending_notice);
        ts_pending_notice[0] = '\0';
    }
    if (mutable_ui != NULL && audio != NULL &&
        audio->performance_group_latched) {
        mutable_ui->capture_source_slot = -1;
        mutable_ui->active_notes |= ts_performance_visible_mask(
            &audio->performance, ts_ui_keyboard_base_note(mutable_ui));
    }
    if (mutable_ui != NULL && audio != NULL)
        mutable_ui->active_notes |= ts_performance_visible_mask(
            &audio->sister.performance,
            ts_ui_keyboard_base_note(mutable_ui));
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);

    ts_ui_render(fb, ui, instrument);
    ts_overlay_tile_states(fb, ui, instrument);

    if (ui != NULL && ui->config_open) {
        if (!ts_config_was_open) ts_audio_catalog_refresh();
        ts_audio_render_config(fb, ui);
        ts_config_was_open = 1;
    } else {
        ts_config_was_open = 0;
    }
}

static int ts_audio_config_field_from_point(int x, int y)
{
    if (ts_live_ui == NULL || !ts_live_ui->config_open)
        return ts_ui_config_field_from_point(x, y);

    for (int field = 0; field < TS_CONFIG_FIELD_COUNT; ++field) {
        int top = TS_AUDIO_CONFIG_PATH_Y + field * TS_AUDIO_CONFIG_PATH_STEP;
        if (x >= 20 && x < 620 &&
            y >= top && y < top + TS_AUDIO_CONFIG_ROW_H)
            return field;
    }

    if (y >= TS_AUDIO_CONFIG_INPUT_Y &&
        y < TS_AUDIO_CONFIG_INPUT_Y + TS_AUDIO_CONFIG_ROW_H) {
        if (x >= 20 && x < 20 + TS_AUDIO_CONFIG_INPUT_W) {
            if (x < 48)
                ts_cycle_device_name(ts_live_ui->config.record_input_device,
                                     sizeof(ts_live_ui->config.record_input_device),
                                     ts_device_catalog.input_names,
                                     ts_device_catalog.input_count, -1);
            else if (x >= 420)
                ts_cycle_device_name(ts_live_ui->config.record_input_device,
                                     sizeof(ts_live_ui->config.record_input_device),
                                     ts_device_catalog.input_names,
                                     ts_device_catalog.input_count, 1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "INPUT DEVICE: %.120s",
                     ts_live_ui->config.record_input_device[0] != '\0' ?
                     ts_live_ui->config.record_input_device : "SYSTEM DEFAULT");
            return -1;
        }
        if (x >= TS_AUDIO_CONFIG_CHANNEL_X && x < 620) {
            if (x < TS_AUDIO_CONFIG_CHANNEL_X + 28)
                ts_cycle_input_channel(-1);
            else if (x >= 594)
                ts_cycle_input_channel(1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "INPUT MODE: %s",
                     ts_input_channel_mode_name(
                         ts_live_ui->config.record_input_channel));
            return -1;
        }
    }

    if (y >= TS_AUDIO_CONFIG_OUTPUT_Y &&
        y < TS_AUDIO_CONFIG_OUTPUT_Y + TS_AUDIO_CONFIG_ROW_H) {
        if (x >= 20 && x < 20 + TS_AUDIO_CONFIG_INPUT_W) {
            if (x < 48)
                ts_cycle_device_name(ts_live_ui->config.audio_output_device,
                                     sizeof(ts_live_ui->config.audio_output_device),
                                     ts_device_catalog.output_names,
                                     ts_device_catalog.output_count, -1);
            else if (x >= 420)
                ts_cycle_device_name(ts_live_ui->config.audio_output_device,
                                     sizeof(ts_live_ui->config.audio_output_device),
                                     ts_device_catalog.output_names,
                                     ts_device_catalog.output_count, 1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "OUTPUT DEVICE: %.120s",
                     ts_live_ui->config.audio_output_device[0] != '\0' ?
                     ts_live_ui->config.audio_output_device : "SYSTEM DEFAULT");
            return -1;
        }
        if (x >= TS_AUDIO_CONFIG_CHANNEL_X && x < 620) {
            if (x < TS_AUDIO_CONFIG_CHANNEL_X + 28)
                ts_cycle_audio_buffer(-1);
            else if (x >= 594)
                ts_cycle_audio_buffer(1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "AUDIO BUFFER: %d FRAMES - SAVE TO RESTART AUDIO",
                     ts_live_ui->config.audio_buffer_frames);
            return -1;
        }
    }

    if (y >= TS_MIDI_CONFIG_INPUT_Y &&
        y < TS_MIDI_CONFIG_INPUT_Y + TS_AUDIO_CONFIG_ROW_H) {
        if (x >= 20 && x < 20 + TS_AUDIO_CONFIG_INPUT_W) {
            if (x < 48) ts_cycle_midi_device(-1);
            else if (x >= 420) ts_cycle_midi_device(1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "MIDI INPUT: %.120s",
                     ts_live_ui->config.midi_input_device[0] != '\0' ?
                     ts_live_ui->config.midi_input_device : "AUTO FIRST");
            return -1;
        }
        if (x >= TS_AUDIO_CONFIG_CHANNEL_X && x < 620) {
            if (x < TS_AUDIO_CONFIG_CHANNEL_X + 28)
                ts_cycle_midi_channel(-1);
            else if (x >= 594)
                ts_cycle_midi_channel(1);
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     ts_live_ui->config.midi_input_channel == 0 ?
                     "MIDI CHANNEL: OMNI" : "MIDI CHANNEL: %d",
                     ts_live_ui->config.midi_input_channel);
            return -1;
        }
    }

    return -1;
}

static size_t ts_audio_config_cursor_from_point(const TsUiState *ui,
                                                TsConfigField field, int x)
{
    const char *value;
    size_t length;
    size_t cursor;
    size_t visible = 69u;
    size_t first = 0u;
    size_t clicked;
    if (ui == NULL || (int)field < 0 || (int)field >= TS_CONFIG_FIELD_COUNT)
        return 0u;
    value = ts_config_field_const(&ui->config, field);
    length = value != NULL ? strlen(value) : 0u;
    cursor = field == ui->config_field ? ui->config_cursor : length;
    if (cursor > length) cursor = length;
    if (length > visible) {
        first = length - visible;
        if (cursor < first) first = cursor;
        if (cursor > first + visible) first = cursor - visible;
    }
    if (x <= TS_AUDIO_CONFIG_VALUE_X) return first;
    clicked = first + (size_t)((x - TS_AUDIO_CONFIG_VALUE_X + 3) / 6);
    return clicked > length ? length : clicked;
}
