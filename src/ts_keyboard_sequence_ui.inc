int ts_ui_keyboard_sequence_visible(const TsUiState *ui)
{
    return ui && !ui->router_open && !ui->master_eq_open && ui->show_keyboard && !ui->mosaic_open && !ui->mosaic_edit_choice &&
        !ui->portal.open && !ui->exit_confirm_open && !ui->project_overwrite_confirm_open &&
        !ui->overdub_confirm_open && !ui->transform_open && !ui->drone_open &&
        !ui->import_preview_open && ui->exchange_dialog == TS_UI_EXCHANGE_NONE &&
        !ui->load_selection_choice_open && !ui->palette_open && !ui->config_open &&
        ui->browser.mode == TS_BROWSER_CLOSED && ui->renaming_bank_slot < 0 &&
        ui->renaming_recipe_slot < 0 && !ui->export_choice_open && !ui->file_busy &&
        !ui->fm_bank_choice_open && !ui->fm_full_choice_open && !ui->midi_learn_active;
}

static int keyboard_sequence_ordinal(const TsUiState *ui, int pitch)
{
    for (int i = 0; i < ui->keyboard_sequence.count; ++i)
        if (ui->keyboard_sequence.notes[i] == pitch) return i + 1;
    return 0;
}

static uint32_t keyboard_sequence_color(const TsUiState *ui, int pitch, int black, uint32_t normal)
{
    if (ui->keyboard_sequence_running && ui->keyboard_sequence_current == pitch)
        return RGB(247, 196, 91);
    if (keyboard_sequence_ordinal(ui, pitch)) return black ? RGB(39, 104, 120) : RGB(91, 161, 167);
    return normal;
}

static void keyboard_sequence_panel(TsFramebuffer *fb, const TsUiState *ui)
{
    if (!ts_ui_keyboard_sequence_visible(ui) || !ui->keyboard_sequence_open) return;
    const TsKeyboardSequenceSettings *s = &ui->keyboard_sequence;
    int y = ui->fm_open ? TS_ARP_PANEL_FM_Y : TS_ARP_PANEL_Y;
    int h = ui->fm_open ? TS_ARP_PANEL_FM_H : TS_ARP_PANEL_H;
    int offset = ui->fm_open ? TS_ARP_PANEL_FM_OFFSET : 0;
    char label[80];
    frame(fb, TS_ARP_PANEL_X, y, TS_ARP_PANEL_W, h,
          RGB(24, 29, 36), RGB(91, 161, 167));
    text(fb, 20, 122 + offset, "ARP / KEYBOARD SEQUENCE", RGB(137, 210, 211), 1);
    snprintf(label, sizeof(label), "%02d NOTES   %s %02d", s->count,
             ui->keyboard_sequence_step ? "STEP" : ui->keyboard_slot_running ? "WAIT" : "READY", ui->keyboard_sequence_running ? ui->keyboard_sequence_step : 0);
    text(fb, 356, 122 + offset, label, RGB(247, 196, 91), 1);
    button(fb, 20, 134 + offset, 64, "COPY", 0);
    button(fb, 90, 134 + offset, 64, "PASTE", ui->keyboard_sequence_clipboard_valid);
    button(fb, 164, 134 + offset, 100,
           ui->keyboard_sequence_clear_deadline ? "CONFIRM" : "CLEAR ALL",
           ui->keyboard_sequence_clear_deadline != 0);
    text(fb, 278, 142 + offset, ui->keyboard_sequence_clear_deadline ?
         "CLEAR ALL 16 SLOTS?" : ui->keyboard_sequence_clipboard_valid ?
         "PATTERN COPIED / PASTE INTO SLOT" : "COPY / PASTE NOTES + SETTINGS", RGB(180, 201, 204), 1);
    const TsKeyboardSlotSequence *q = &ui->keyboard_slot_sequence;
    button(fb, 20, 164 + offset, 90, q->enabled ? "SLOT SEQ ON" : "SLOT SEQ OFF", q->enabled);
    button(fb, 116, 164 + offset, 100, ts_keyboard_sequence_mode_name(q->mode), 0);
    if (q->seconds >= 60) snprintf(label, sizeof(label), "SLOT TIME %.2F MIN", q->seconds / 60);
    else snprintf(label, sizeof(label), "SLOT TIME %.2F SEC", q->seconds);
    double slot_max = fmax(q->seconds, fmax(1, ui->config.arp_slot_max_seconds));
    double slot_amount = log(fmax(TS_KEYBOARD_SLOT_MIN_SECONDS, q->seconds) / TS_KEYBOARD_SLOT_MIN_SECONDS) /
                         log(slot_max / TS_KEYBOARD_SLOT_MIN_SECONDS);
    slider(fb, 226, 164 + offset, 200, label, (float)slot_amount, RGB(91, 161, 167));
    button(fb, 436, 164 + offset, 80, q->loop ? "LOOP ON" : "LOOP OFF", q->loop);
    if (ui->keyboard_slot_running) {
        int seconds = (int)ceil(ui->keyboard_slot_remaining);
        snprintf(label, sizeof(label), "%02d:%02d LEFT", seconds / 60, seconds % 60);
    } else snprintf(label, sizeof(label), "%s", q->enabled ? "ARMED" : "MANUAL");
    text(fb, 530, 172 + offset, label, RGB(247, 196, 91), 1);
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        snprintf(label, sizeof(label), "%02d", i + 1);
        mini_button(fb, 20 + i * 37, 198 + offset, 34, label, ui->keyboard_sequence_slot == i);
        if (ui->keyboard_sequence_occupied & (1u << i))
            rect(fb, 47 + i * 37, 209 + offset, 3, 3, RGB(91, 161, 167));
    }
    snprintf(label, sizeof(label), "ARP VOL %.0F%%", s->volume * 100);
    slider(fb, 20, 220 + offset, 188, label, (float)(s->volume / 2), RGB(91, 161, 167));
    /* Gold marker shows the modulated gain; the fader keeps its base position. */
    if (ui->keyboard_sequence_running)
        rect(fb, 20 + (int)(182 * fmin(1, fmax(0, ui->keyboard_sequence_level / 2))),
             242 + offset, 6, 2, RGB(247, 196, 91));
    button(fb, 220, 220 + offset, 64, s->lfo_enabled ? "LFO ON" : "LFO OFF", s->lfo_enabled);
    if (s->lfo_seconds >= 60) snprintf(label, sizeof(label), "CYCLE %.2F MIN", s->lfo_seconds / 60);
    else snprintf(label, sizeof(label), "CYCLE %.2F SEC", s->lfo_seconds);
    double cycle = log(fmax(TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS, s->lfo_seconds) /
                       TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS) /
                   log(TS_KEYBOARD_SEQUENCE_MAX_SECONDS / TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS);
    slider(fb, 296, 220 + offset, 164, label, (float)cycle, RGB(91, 161, 167));
    snprintf(label, sizeof(label), "DEPTH %.0F%%", s->lfo_depth * 100);
    slider(fb, 476, 220 + offset, 144, label, (float)s->lfo_depth, RGB(247, 196, 91));
    button(fb, 20, 252 + offset, 64, ui->keyboard_sequence_running ? "STOP" : "PLAY", ui->keyboard_sequence_running);
    button(fb, 90, 252 + offset, 100, ts_keyboard_sequence_mode_name(s->mode), 0);
    button(fb, 196, 252 + offset, 58, "RESET", 0);
    button(fb, 260, 252 + offset, 58, "CLEAR", 0);
    button(fb, 324, 252 + offset, 100, "FROM HELD", 0);
    button(fb, 430, 252 + offset, 80, ui->keyboard_sequence_edit ? "EDIT ON" : "EDIT OFF", ui->keyboard_sequence_edit);
    button(fb, 516, 252 + offset, 64, s->loop ? "LOOP" : "ONCE", s->loop);
    button(fb, 586, 252 + offset, 34, "X", 0);
    if (s->seconds >= 60) snprintf(label, sizeof(label), "STEP %.2F MIN", s->seconds / 60);
    else snprintf(label, sizeof(label), "STEP %.3F SEC", s->seconds);
    double amount = log(fmax(TS_KEYBOARD_SEQUENCE_MIN_SECONDS, s->seconds) / TS_KEYBOARD_SEQUENCE_MIN_SECONDS) /
        log(TS_KEYBOARD_SEQUENCE_MAX_SECONDS / TS_KEYBOARD_SEQUENCE_MIN_SECONDS);
    slider(fb, 20, 281 + offset, 248, label, (float)amount, RGB(91, 161, 167));
    snprintf(label, sizeof(label), "GATE %.0F%%", s->gate * 100);
    slider(fb, 282, 281 + offset, 136, label, (float)((s->gate - .05) / .95), RGB(247, 196, 91));
    text(fb, 432, 283 + offset, "CTRL+CLICK: SELECT", RGB(180, 201, 204), 1);
    text(fb, 432, 296 + offset, "SHIFT+SPACE: PLAY/STOP", RGB(180, 201, 204), 1);
    rect(fb, TS_ARP_PANEL_X + 1, y + h - 3, (int)((TS_ARP_PANEL_W - 2) * ui->keyboard_sequence_progress), 2, RGB(247, 196, 91));
}
