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
        char shown[96];
        char channel[24];

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
        if (ui->config.record_input_channel == 0)
            snprintf(channel, sizeof(channel), "MIX");
        else
            snprintf(channel, sizeof(channel), "%d",
                     ui->config.record_input_channel);
        cfg_text(fb, TS_AUDIO_CONFIG_CHANNEL_X + 58,
                 TS_AUDIO_CONFIG_INPUT_Y + 5, channel, text_color);
        cfg_text(fb, 608, TS_AUDIO_CONFIG_INPUT_Y + 5, ">", text_color);

        cfg_frame(fb, 20, TS_AUDIO_CONFIG_OUTPUT_Y,
                  600, TS_AUDIO_CONFIG_ROW_H, row, edge);
        cfg_text(fb, 28, TS_AUDIO_CONFIG_OUTPUT_Y + 5, "<", text_color);
        cfg_text(fb, 44, TS_AUDIO_CONFIG_OUTPUT_Y + 5, "OUTPUT", text_color);
        snprintf(shown, sizeof(shown), "%.78s", output);
        cfg_text(fb, 94, TS_AUDIO_CONFIG_OUTPUT_Y + 5, shown, text_color);
        cfg_text(fb, 608, TS_AUDIO_CONFIG_OUTPUT_Y + 5, ">", text_color);
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

static void ts_overlay_tile_states(TsFramebuffer *fb, const TsUiState *ui,
                                   const TsInstrument *instrument)
{
    uint32_t source_color;
    if (fb == NULL || ui == NULL || instrument == NULL || ui->show_keyboard ||
        ui->show_recipes || ui->show_ingredients) return;
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
    if (!ts_multi_group_latched || ts_multi_source_mask == 0u) return;
    source_color = ui->palette.colors[TS_PALETTE_PATTERN_EFFECT];
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if ((ts_multi_source_mask & (uint16_t)(1u << slot)) != 0u)
            ts_draw_tile_border(fb, &ts_tile_bounds[slot], source_color);
}

static void ts_overlay_selection_grid(TsFramebuffer *fb, const TsUiState *ui,
                                      const TsInstrument *instrument)
{
    uint32_t selection;
    uint32_t grid;
    uint32_t divisions;
    if (fb == NULL || ui == NULL || instrument == NULL || !instrument->has_selection)
        return;
    divisions = instrument->grid_divisions;
    if (divisions < 2u) return;
    selection = ui->palette.colors[TS_PALETTE_WAVE_SELECTION];
    grid = ui->palette.colors[TS_PALETTE_PATTERN_EMPTY];
    for (uint32_t division = 1u; division < divisions; ++division) {
        int x = TS_WAVE_X + (int)(((uint64_t)TS_WAVE_W * division) / divisions);
        if (x < TS_WAVE_X || x >= TS_WAVE_X + TS_WAVE_W) continue;
        for (int y = TS_WAVE_Y; y < TS_WAVE_Y + TS_WAVE_H; ++y) {
            size_t at = (size_t)y * TS_UI_WIDTH + (size_t)x;
            if (fb->pixels[at] == selection) fb->pixels[at] = grid;
        }
    }
}

static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument)
{
    TsUiState *mutable_ui = (TsUiState *)ui;
    ts_live_ui = mutable_ui;
    ts_live_config = mutable_ui != NULL ? &mutable_ui->config : ts_live_config;

    if (mutable_ui != NULL && ts_last_sample_page != mutable_ui->sample_page) {
        if (ts_last_sample_page >= 0 && ts_multi_capture_recorder != NULL &&
            ts_multi_capture_recorder->state == TS_CAPTURE_IDLE)
            ts_multi_clear_group();
        ts_last_sample_page = mutable_ui->sample_page;
    }
    if (ts_multi_capture_recorder != NULL &&
        ts_multi_capture_recorder->state == TS_CAPTURE_IDLE &&
        ts_multi_group_latched)
        ts_multi_clear_group();

    if (mutable_ui != NULL && !mutable_ui->config_open &&
        ts_pending_notice[0] != '\0') {
        snprintf(mutable_ui->status, sizeof(mutable_ui->status),
                 "%s", ts_pending_notice);
        ts_pending_notice[0] = '\0';
    }

    ts_ui_render(fb, ui, instrument);
    ts_overlay_selection_grid(fb, ui, instrument);
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
                     ts_live_ui->config.record_input_channel == 0 ?
                     "INPUT CHANNEL: MIX TO MONO" : "INPUT CHANNEL: %d",
                     ts_live_ui->config.record_input_channel);
            return -1;
        }
    }

    if (y >= TS_AUDIO_CONFIG_OUTPUT_Y &&
        y < TS_AUDIO_CONFIG_OUTPUT_Y + TS_AUDIO_CONFIG_ROW_H &&
        x >= 20 && x < 620) {
        if (x < 48)
            ts_cycle_device_name(ts_live_ui->config.audio_output_device,
                                 sizeof(ts_live_ui->config.audio_output_device),
                                 ts_device_catalog.output_names,
                                 ts_device_catalog.output_count, -1);
        else if (x >= 590)
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
