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

static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument)
{
    TsUiState *mutable_ui = (TsUiState *)ui;
    ts_live_ui = mutable_ui;
    ts_live_config = mutable_ui != NULL ? &mutable_ui->config : ts_live_config;

    if (mutable_ui != NULL && !mutable_ui->config_open &&
        ts_pending_notice[0] != '\0') {
        snprintf(mutable_ui->status, sizeof(mutable_ui->status),
                 "%s", ts_pending_notice);
        ts_pending_notice[0] = '\0';
    }

    ts_ui_render(fb, ui, instrument);

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
