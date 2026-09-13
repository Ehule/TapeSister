/* Native instrument diagram. Geometry comes from the audio owner's snapshot;
   no wall clock, animation oscillator, or audio-buffer access on the UI thread. */
static void prism_lens_outline(TsFramebuffer *fb, int x, int y, int height,
                                uint32_t color)
{
    for (int j = -height; j < height; ++j) {
        float t = (float)j / height;
        float next = (float)(j + 1) / height;
        int width = (int)lrintf(13 * (1 - t * t));
        int width_next = (int)lrintf(13 * (1 - next * next));
        rect(fb, x - width, y + j, 2 * width + 1, 1, RGB(18, 27, 31));
        ui_line(fb, x - width, y + j, x - width_next, y + j + 1, color);
        ui_line(fb, x + width, y + j, x + width_next, y + j + 1, color);
    }
}

static void sister_prism_render(TsFramebuffer *fb, const TsSisterUiModel *model)
{
    const TsPrismControls *p = &model->parameters.prism;
    TsPrismView v = model->routing.prism.valid ? model->routing.prism :
        ts_prism_control_view(p);
    const uint32_t background = RGB(7, 11, 15);
    const uint32_t ink = RGB(140, 179, 192);
    const uint32_t colors[12] = {
        RGB(236,224,159), RGB(241,148,108), RGB(239,193,101),
        RGB(195,215,120), RGB(126,215,171), RGB(92,207,212),
        RGB(106,168,238), RGB(159,146,229), RGB(207,139,206),
        RGB(205,177,122), RGB(187,153,110), RGB(227,188,124)
    };
    char label[96];
    rect(fb, 10, 40, 620, 232, background);
    button(fb, 16, 48, 96, p->enabled ? "PRISM ON" : "PRISM OFF", p->enabled);
    button(fb, 122, 48, 116, ts_prism_mode_name(p->mode), 1);
    snprintf(label, sizeof(label), "%d LENSES / ONE SOURCE", p->lenses);
    text(fb, 250, 55, label, ink, 1);
    text(fb, 544, 55, v.valid ? "LIVE DSP" : "SETTINGS", ink, 1);
    text(fb, 28, 85, "SOURCE", ink, 1);
    text(fb, 118, 85, "REFRACT", ink, 1);
    text(fb, 290, 85, "ALTER", ink, 1);
    text(fb, 457, 85, "RECOMBINE", ink, 1);
    text(fb, 574, 85, "OUT", ink, 1);

    prism_lens_outline(fb, 148, 167, 64, RGB(91,144,161));
    prism_lens_outline(fb, 462, 167, 64, RGB(91,144,161));
    /* The dry path stays straight and belongs to the same input/output. */
    uint32_t dry = blend_color(background, RGB(235,237,219),
                               8 + (int)lrintf(92 * fminf(1, v.dry)));
    ui_line(fb, 30, 167, 608, 167, dry);
    ui_line(fb, 30, 166, 147, 166, RGB(229,232,212));
    ui_line(fb, 92, 162, 99, 167, RGB(229,232,212));
    ui_line(fb, 92, 172, 99, 167, RGB(229,232,212));
    text(fb, 52, 178, "ORIGINAL", blend_color(background, dry, 75), 1);

    for (int i = TS_PRISM_LENSES - 1; i >= 0; --i) {
        TsPrismLensView ray = v.lens[i];
        if (i >= p->lenses && ray.level <= .001f) continue;
        int x, y;
        ts_sister_ui_prism_point(ray, &x, &y);
        float strength = ray.level / (1 + ray.level);
        uint32_t color = blend_color(background, colors[i],
            (int)lrintf(8 + strength * (12 + 80 * v.wet)));
        int exit_y = 167 + (y - 167) / 3;
        int rejoin_y = 167 + (y - 167) / 2;
        ui_line(fb, 148, 167, 172, exit_y, color);
        ui_line(fb, 172, exit_y, x, y, color);
        ui_line(fb, x, y, 462, rejoin_y, color);
        ui_line(fb, 462, rejoin_y, 527, 167, color);
        if (i == 0) rect(fb, x - 2, y - 2, 5, 5, color);
        else {
            rect(fb, x - 3, y - 3, 7, 7, color);
            rect(fb, x - 1, y - 1, 3, 3, background);
        }
        if (model->prism_selected == i + 1) {
            snprintf(label, sizeof(label), "%02d %+.0FDB", i + 1, p->trim_db[i]);
            text(fb, x + 7, y - 3, label, RGB(240,240,220), 1);
        }
        if ((p->mute_mask | p->solo_mask) & (1 << i))
            text(fb,x-3,y+6,(p->mute_mask & (1 << i)) ? "M" : "S",ink,1);
    }
    ui_line(fb, 527, 167, 608, 167, RGB(229,232,212));
    ui_line(fb, 600, 162, 608, 167, RGB(229,232,212));
    ui_line(fb, 600, 172, 608, 167, RGB(229,232,212));
    text(fb, 24, 252, "DRAG: PITCH/PAN  WHEEL: TRIM  RIGHT CLICK: RESET", ink, 1);
    text(fb,24,262,"SHIFT CLICK: MUTE  CTRL CLICK: SOLO  PRESET RIGHT CLICK: STOCK",ink,1);
    static const char *const names[8] = {
        "LENSES", "SPREAD", "DRIFT", "FOCUS", "STEREO", "BODY", "WET", "OUTPUT"
    };
    float amounts[8] = {(p->lenses - 2) / 10.f, p->spread / 2, p->drift / 2, p->focus,
                        p->stereo, p->body / 3, p->mix, (p->output_db + 12) / 24};
    for (int i = 0; i < 8; ++i) {
        int x = 16 + (i % 4) * 154, y = 280 + (i / 4) * 28;
        int locked = ts_sister_ui_parameter_locked(model, TS_SISTER_UI_PARAM_PRISM_LENSES + i);
        sister_percent_parameter_state(fb, x, y, 144, names[i], amounts[i],
                                       i==1 || i==2 ? 200 : i==5 ? 300 : 100, colors[i], locked);
        if (i == 0 || i == 7) {
            rect(fb, x + 2, y + 2, 140, 11, RGB(24,23,25));
            if (i == 0) snprintf(label, sizeof(label), "LENSES %02d", p->lenses);
            else snprintf(label, sizeof(label), "OUTPUT %+.1F DB", p->output_db);
            text(fb, x + 3, y + 3, label, locked ? sister_dim_color(colors[i]) : colors[i], 1);
        }
    }
    sister_percent_parameter_state(fb,16,332,144,"DRY LEVEL",p->dry_level/2,200,colors[0],
        ts_sister_ui_parameter_locked(model,TS_SISTER_UI_PARAM_PRISM_DRY));
    text(fb,180,338,model->routing.enabled ? "PRISM > TAPE + FX" : "PRISM > FX / TAPE OFF",ink,1);
    button(fb, TS_SISTER_UI_FX_REC_X, TS_SISTER_UI_FX_REC_Y, TS_SISTER_UI_FX_REC_W,
        model->file_capture_state == TS_PERFORMANCE_FILE_STOPPING ? "FILE WAIT" :
        model->file_capture_state == TS_PERFORMANCE_FILE_RECORDING ? "STOP FILE" : "REC FILE",
        model->file_capture_state == TS_PERFORMANCE_FILE_RECORDING && model->text_cursor_visible);
}
