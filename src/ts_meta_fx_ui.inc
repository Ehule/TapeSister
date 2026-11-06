/* Rendered at the same pixel geometry used by the native editor's hit tests. */
void ts_meta_render(TsFramebuffer *fb, const TsMetaControls *c, int maps, int page, int selected, float mix,
                    float gain, const char *path, const char *status, const TsPalette *palette) {
    render_palette = palette;
    render_group = TS_THEME_PEDALBOARD;
    clear(fb, PAL_DESKTOP);
    text(fb, 12, 12, c->name, PAL_NOTE, 1);
    sister_compact_button(fb, 462, 6, 28, "<", 0);
    sister_compact_button(fb, 496, 6, 28, ">", 0);
    sister_compact_button(fb, 550, 6, 76, "CLOSE", 0);
    for (int i = 0; i < 4; ++i) {
        char s[32];
        float v = c->scene_mode && c->captured == 3
                      ? c->scene[0][i] + (c->scene[1][i] - c->scene[0][i]) * c->morph
                      : c->macro[i];
        snprintf(s, sizeof(s), "%s %.0F%%", c->label[i], v * 100);
        sister_value_parameter_state(fb, 12 + i * 156, 36, 148, s, v, PAL_EFFECT, 0);
    }
    if (!maps) {
        text(fb, 12, 66, "STAGE", PAL_MOUSE, 1);
        text(fb, 146, 66, "PARAMETERS / * = MACRO", PAL_MOUSE, 1);
        text(fb, 374, 66, "MIX", PAL_MOUSE, 1);
        text(fb, 450, 66, "OUTPUT", PAL_MOUSE, 1);
        for (int i = 0; i < 8; ++i) {
            int top = 82 + i * 23;
            TsMetaStage shown = c->stage[i];
            unsigned assigned = 0;
            for (int m = 0; m < c->map_count; ++m)
                if (c->map[m].stage == i) {
                    const TsMetaMap *map = &c->map[m];
                    float macro = c->scene_mode && c->captured == 3
                                      ? c->scene[0][map->macro] +
                                            (c->scene[1][map->macro] - c->scene[0][map->macro]) * c->morph
                                      : c->macro[map->macro];
                    float value = map->low + (map->high - map->low) * macro;
                    if (map->field < 5)
                        ts_meta_stage_set(&shown, map->field, value);
                    else
                        shown.filter[map->field - 5] = value;
                    for (int f = 0; f < 5; ++f)
                        if (ts_meta_target_field(shown.type, f) ==
                            ts_meta_target_field(shown.type, map->field))
                            assigned |= 1u << f;
                }
            const TsMetaStage *s = &shown;
            char number[8];
            snprintf(number, 8, "%d", i + 1);
            if (selected == i)
                rect(fb, 10, top - 1, 618, 22,
                     ts_theme_color(palette, render_group, TS_COLOR_BACKGROUND, RGB(18, 25, 28)));
            sister_compact_button(fb, 12, top, 24, number, i < c->count && s->enabled);
            sister_compact_button(fb, 40, top, 100, i < c->count ? ts_sister_fx_type_name(s->type) : "+ ADD",
                                  0);
            if (i < c->count) {
                for (int j = 0; j < 5; ++j) {
                    char label[24];
                    if (j == 4)
                        snprintf(label, 24, "%+.1F DB", ts_meta_stage_value(s, j) * 24 - 12);
                    else
                        snprintf(label, 24, "%s %.0F%s", j < 3 ? ts_meta_parameter_name(s->type, j) : "",
                                 ts_meta_stage_value(s, j) * 100, (assigned & (1u << j)) ? "*" : "%");
                    if (j < 3 && !*ts_meta_parameter_name(s->type, j)) {
                        text(fb, 146 + j * 76, top + 5, "--", PAL_MOUSE, 1);
                        continue;
                    }
                    sister_value_parameter_state(fb, 146 + j * 76, top, 70, label, ts_meta_stage_value(s, j),
                                                 PAL_WAVE_LEFT, 0);
                }
                sister_compact_button(fb, 530, top, 28, "UP", 0);
                sister_compact_button(fb, 562, top, 28, "DN", 0);
                sister_compact_button(fb, 594, top, 30, "X", 0);
            }
        }
    } else {
        text(fb, 12, 66, "MACRO", PAL_MOUSE, 1);
        text(fb, 154, 66, "STAGE", PAL_MOUSE, 1);
        text(fb, 292, 66, "TARGET", PAL_MOUSE, 1);
        text(fb, 410, 66, "LOW", PAL_MOUSE, 1);
        text(fb, 504, 66, "HIGH", PAL_MOUSE, 1);
        for (int row = 0; row < 8; ++row) {
            int i = page * 8 + row, top = 82 + row * 23;
            if (i >= c->map_count)
                continue;
            const TsMetaMap *m = &c->map[i];
            char s[48];
            sister_compact_button(fb, 12, top, 136, c->label[m->macro], 0);
            snprintf(s, sizeof(s), "%d %s", m->stage + 1, ts_sister_fx_type_name(c->stage[m->stage].type));
            sister_compact_button(fb, 154, top, 132, s, 0);
            if (m->field < 5)
                snprintf(s, sizeof(s), "%s", ts_meta_parameter_name(c->stage[m->stage].type, m->field));
            else {
                TsCharacterFilterControls filter;
                ts_character_filter_default(&filter);
                ts_character_filter_label(&filter, m->field - 5, s, sizeof(s));
            }
            sister_compact_button(fb, 292, top, 110, s, 0);
            snprintf(s, sizeof(s), "%.0F%%", m->low * 100);
            sister_value_parameter_state(fb, 410, top, 86, s, m->low, PAL_WAVE_LEFT, 0);
            snprintf(s, sizeof(s), "%.0F%%", m->high * 100);
            sister_value_parameter_state(fb, 504, top, 86, s, m->high, PAL_WAVE_RIGHT, 0);
            sister_compact_button(fb, 596, top, 28, "X", 0);
        }
    }
    sister_compact_button(fb, 12, 274, 62, "SAVE A", c->captured & 1);
    sister_compact_button(fb, 80, 274, 62, "SAVE B", c->captured & 2);
    sister_value_parameter_state(fb, 150, 274, 180, "A / B MORPH", c->morph, PAL_TUNING, 0);
    sister_percent_parameter_state(fb, 340, 274, 136, "MIX", mix, 100, PAL_EFFECT, 0);
    sister_db_parameter_state(fb, 486, 274, 138, "OUTPUT", gain, PAL_VOLUME, 0);
    sister_compact_button(fb, 12, 304, 70, "CHAIN", !maps);
    sister_compact_button(fb, 88, 304, 70, "MAPS", maps);
    if (maps) {
        sister_compact_button(fb, 168, 304, 90, "+ MAP", 0);
        char s[20];
        snprintf(s, 20, "PAGE %d/3", page + 1);
        sister_compact_button(fb, 266, 304, 96, s, 0);
    } else {
        char s[64];
        if (selected >= 0 && selected < c->count)
            snprintf(s, 64, "SELECTED %d / %s", selected + 1,
                     ts_sister_fx_type_name(c->stage[selected].type));
        else
            snprintf(s, 64, "MAXIMUM 8 STAGES");
        text(fb, 168, 310, s, PAL_MOUSE, 1);
    }
    sister_compact_button(fb, 462, 304, 162, "COPY BOARD RANGE", 0);
    rect(fb, 12, 336, 484, 20, PAL_DESKTOP);
    text(fb, 16, 341, strlen(path) > 78 ? path + strlen(path) - 78 : path, PAL_TEXT, 1);
    sister_compact_button(fb, 504, 334, 56, "LOAD", 0);
    sister_compact_button(fb, 568, 334, 56, "SAVE", 0);
    text(fb, 12, 363, "DRAG/WHEEL: ADJUST | RIGHT NAME: RENAME | TYPE: CYCLE | SHIFT: FINE", PAL_MOUSE, 1);
    char short_status[103];
    snprintf(short_status, sizeof(short_status), "%.102s", status);
    text(fb, 12, 384, short_status, PAL_NOTE, 1);
}
