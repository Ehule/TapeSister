/* Modeless rack editor. The Matrix owns stable identities, preparation,
   topology crossfades and Undo/Redo. This window only edits control data. */
static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    TsFramebuffer fb;
    Uint32 id, painted;
    uint32_t node;
    int visible, maps, page, selected, drag, origin_x, edit, first, count;
    float origin;
    char path[1024], status[128];
    TsMosaic *context;
    uint64_t epoch;
    MatrixSnapshot *before;
} meta_fx_window = {.drag = -1, .count = 8};
static void meta_fx_history_finish(SDL_AudioDeviceID device, AudioState *audio, TsSamplePages *pages,
                                   TsInstrument *bank) {
    if (!meta_fx_window.before)
        return;
    MatrixSnapshot *after = mx_snapshot(device, audio, pages, bank);
    if (after && !mx_equal(meta_fx_window.before, after))
        mx_push(meta_fx_window.before);
    else
        free(meta_fx_window.before);
    free(after);
    meta_fx_window.before = NULL;
}
static void meta_fx_hide(void) {
    meta_fx_window.visible = 0;
    meta_fx_window.drag = -1;
    meta_fx_window.edit = 0;
    free(meta_fx_window.before);
    meta_fx_window.before = NULL;
    if (meta_fx_window.window)
        SDL_HideWindow(meta_fx_window.window);
}
static void meta_fx_open(uint32_t id) {
    if (!meta_fx_window.window) {
        meta_fx_window.window =
            SDL_CreateWindow("TapeSister / Meta FX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1120,
                             700, TS_WORKSPACE_WINDOW_FLAGS);
        if (!meta_fx_window.window)
            return;
        meta_fx_window.renderer = SDL_CreateRenderer(meta_fx_window.window, -1, SDL_RENDERER_SOFTWARE);
        if (meta_fx_window.renderer)
            meta_fx_window.texture = SDL_CreateTexture(meta_fx_window.renderer, SDL_PIXELFORMAT_ARGB8888,
                                                       SDL_TEXTUREACCESS_STREAMING, 640, 400);
        if (!meta_fx_window.texture) {
            if (meta_fx_window.renderer)
                SDL_DestroyRenderer(meta_fx_window.renderer);
            SDL_DestroyWindow(meta_fx_window.window);
            meta_fx_window.window = NULL;
            meta_fx_window.renderer = NULL;
            return;
        }
        meta_fx_window.id = SDL_GetWindowID(meta_fx_window.window);
    }
    meta_fx_window.node = id;
    meta_fx_window.visible = 1;
    meta_fx_window.context = NULL;
    meta_fx_window.drag = -1;
    meta_fx_window.selected = 0;
    meta_fx_window.edit = 0;
    if (!*meta_fx_window.path)
        snprintf(meta_fx_window.path, sizeof(meta_fx_window.path), "meta-fx.tsmfx");
    snprintf(meta_fx_window.status, sizeof(meta_fx_window.status),
             "CHAIN: EDIT STAGES | MAPS: LINK ONE MACRO TO MULTIPLE CONTROLS");
    SDL_ShowWindow(meta_fx_window.window);
    SDL_RaiseWindow(meta_fx_window.window);
}
static TsMatrixEffectControls *meta_fx_resolve(TsUiState *ui) {
    if (!meta_fx_window.visible)
        return NULL;
    if (!meta_fx_window.context) {
        meta_fx_window.context = ui->mosaic;
        meta_fx_window.epoch = ui->mosaic ? ui->mosaic->epoch : 0;
    } else if (meta_fx_window.context != ui->mosaic ||
               (ui->mosaic && meta_fx_window.epoch != ui->mosaic->epoch)) {
        meta_fx_hide();
        return NULL;
    }
    for (int i = 0; i < TS_MATRIX_EFFECT_CAPACITY; ++i)
        if (matrix_window.controls.effect[i].id == meta_fx_window.node &&
            matrix_window.controls.effect[i].type == TS_MATRIX_FX_META)
            return &matrix_window.controls.effect[i];
    meta_fx_hide();
    return NULL;
}
static void meta_fx_stage_open(uint32_t id,int stage) {
    meta_fx_open(id);meta_fx_window.selected=stage;meta_fx_window.maps=0;
}
static void meta_fx_paint(TsUiState *ui) {
    if (!meta_fx_window.visible || !window_drawable(meta_fx_window.window) || SDL_GetTicks() - meta_fx_window.painted < 33)
        return;
    TsMatrixEffectControls *c = meta_fx_resolve(ui);
    if (!c)
        return;
    ts_meta_render(&meta_fx_window.fb, &c->meta, meta_fx_window.maps, meta_fx_window.page,
                   meta_fx_window.selected, c->mix, c->gain_db, meta_fx_window.path, meta_fx_window.status,
                   &ui->palette);
    SDL_UpdateTexture(meta_fx_window.texture, NULL, meta_fx_window.fb.pixels, 640 * sizeof(uint32_t));
    SDL_RenderClear(meta_fx_window.renderer);
    SDL_RenderCopy(meta_fx_window.renderer, meta_fx_window.texture, NULL, NULL);
    SDL_RenderPresent(meta_fx_window.renderer);
    meta_fx_window.painted = SDL_GetTicks();
}
static void meta_fx_map_remove(TsMetaControls *c, int i) {
    memmove(&c->map[i], &c->map[i + 1], (c->map_count - i - 1) * sizeof(c->map[0]));
    memset(&c->map[--c->map_count], 0, sizeof(c->map[0]));
}
static int meta_fx_target_available(const TsMetaControls *c, int index, int stage, int field) {
    if (stage < 0 || stage >= c->count || !c->stage[stage].type)
        return 0;
    int type = c->stage[stage].type;
    if (field < 0 || field >= 5 + (type == TS_SISTER_FX_FILTER ? TS_CF_FOCUS : 0) ||
        (field < 3 && !*ts_meta_parameter_name(type, field)))
        return 0;
    for (int i = 0; i < c->map_count; ++i)
        if (i != index && c->map[i].stage == stage &&
            ts_meta_target_field(type, c->map[i].field) == ts_meta_target_field(type, field))
            return 0;
    return 1;
}
static int meta_fx_next_target(const TsMetaControls *c, int index, int stage, int from, int direction) {
    int fields = c->stage[stage].type == TS_SISTER_FX_FILTER ? 5 + TS_CF_FOCUS : 5;
    for (int step = 1; step <= fields; ++step) {
        int f = ((from + step * direction) % fields + fields) % fields;
        if (meta_fx_target_available(c, index, stage, f))
            return f;
    }
    return -1;
}
static void meta_fx_stage_remove(TsMetaControls *c, int i) {
    for (int j = c->map_count - 1; j >= 0; --j) {
        if (c->map[j].stage == i)
            meta_fx_map_remove(c, j);
        else if (c->map[j].stage > i)
            --c->map[j].stage;
    }
    memmove(&c->stage[i], &c->stage[i + 1], (c->count - i - 1) * sizeof(c->stage[0]));
    ts_meta_stage_default(&c->stage[--c->count], 0);
}
static float meta_fx_get(const TsMatrixEffectControls *c, int field) {
    if (field < 4) {
        float v = c->meta.macro[field];
        if (c->meta.scene_mode && c->meta.captured == 3)
            v = c->meta.scene[0][field] + (c->meta.scene[1][field] - c->meta.scene[0][field]) * c->meta.morph;
        return v;
    }
    if (field == 4)
        return c->meta.morph;
    if (field == 5)
        return c->mix;
    if (field == 6)
        return (c->gain_db + 12) / 24;
    if (field >= 100 && field < 140)
        return ts_meta_stage_value(&c->meta.stage[(field - 100) / 5], (field - 100) % 5);
    if (field >= 200 && field < 248)
        return (field & 1) ? c->meta.map[(field - 200) / 2].high : c->meta.map[(field - 200) / 2].low;
    return 0;
}
static void meta_fx_set(TsMatrixEffectControls *c, int field, float value) {
    float v = SDL_clamp(value, 0, 1);
    if (field < 4)
        ts_meta_macro_set(&c->meta, field, v);
    else if (field == 4) {
        if (c->meta.captured == 3) {
            c->meta.scene_mode = 1;
            c->meta.morph = v;
        }
    } else if (field == 5)
        c->mix = v;
    else if (field == 6)
        c->gain_db = v * 24 - 12;
    else if (field >= 100 && field < 140)
        ts_meta_stage_set(&c->meta.stage[(field - 100) / 5], (field - 100) % 5, v);
    else if (field >= 200 && field < 248) {
        TsMetaMap *m = &c->meta.map[(field - 200) / 2];
        if (field & 1)
            m->high = v;
        else
            m->low = v;
    }
}
static int meta_fx_event(const SDL_Event *e, SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                         SisterWindow *sister, TsSamplePages *pages, TsInstrument *bank) {
    if (!meta_fx_window.visible || event_window_id(e) != meta_fx_window.id)
        return 0;
    if (e->type == SDL_WINDOWEVENT &&
        (e->window.event == SDL_WINDOWEVENT_CLOSE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
        meta_fx_history_finish(device, audio, pages, bank);
        meta_fx_window.drag = -1;
        meta_fx_window.edit = 0;
        if (e->window.event == SDL_WINDOWEVENT_CLOSE)
            meta_fx_hide();
        return 1;
    }
    TsMatrixEffectControls *control = meta_fx_resolve(ui);
    if (!control)
        return 1;
    TsMetaControls *c = &control->meta;
    if (e->type == SDL_MOUSEBUTTONUP) {
        meta_fx_history_finish(device, audio, pages, bank);
        meta_fx_window.drag = -1;
        return 1;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_m && (e->key.keysym.mod & KMOD_CTRL) &&
        (e->key.keysym.mod & KMOD_SHIFT)) {
        midi_learn_set_active(ui, sister, !ui->midi_learn_active);
        return 1;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
        meta_fx_history_finish(device, audio, pages, bank);
        meta_fx_hide();
        return 1;
    }
    if (e->type == SDL_TEXTINPUT || (e->type == SDL_KEYDOWN && meta_fx_window.edit)) {
        char *text = meta_fx_window.edit == 1   ? meta_fx_window.path
                     : meta_fx_window.edit == 2 ? c->name
                     : meta_fx_window.edit >= 3 ? c->label[meta_fx_window.edit - 3]
                                                : NULL;
        size_t size = meta_fx_window.edit == 1   ? sizeof(meta_fx_window.path)
                      : meta_fx_window.edit == 2 ? sizeof(c->name)
                                                 : 16;
        if (!text)
            return 1;
        if (e->type == SDL_KEYDOWN) {
            if (e->key.keysym.sym == SDLK_RETURN) {
                meta_fx_window.edit = 0;
                meta_fx_history_finish(device, audio, pages, bank);
                return 1;
            }
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL))
                *text = 0;
            else if (e->key.keysym.sym == SDLK_BACKSPACE && *text)
                text[strlen(text) - 1] = 0;
        } else
            for (const char *p = e->text.text; *p; ++p)
                if ((unsigned char)*p >= 32 && (unsigned char)*p < 127 && *p != '=' &&
                    strlen(text) + 1 < size) {
                    size_t n = strlen(text);
                    text[n] = *p;
                    text[n + 1] = 0;
                }
        if (meta_fx_window.edit != 1 && mx_editable()) {
            mx_commit_controls(device, audio, ui);
            ui->project_modified = 1;
        }
        return 1;
    }
    if (e->type == SDL_KEYDOWN && (e->key.keysym.mod & KMOD_CTRL) &&
        (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_y)) {
        meta_fx_history_finish(device, audio, pages, bank);
        if (!e->key.repeat)
            mx_undo(device, audio, ui, pages, bank,
                    e->key.keysym.sym == SDLK_y || (e->key.keysym.mod & KMOD_SHIFT));
        return 1;
    }
    /* Audition keys and their releases continue to the application's input
       owner when no text field is being edited. */
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP)
        return 0;
    int down = e->type == SDL_MOUSEBUTTONDOWN, left = down && e->button.button == SDL_BUTTON_LEFT,
        wheel = e->type == SDL_MOUSEWHEEL, motion = e->type == SDL_MOUSEMOTION;
    if (!down && !wheel && !motion)
        return 1;
    int rawx, rawy, x, y;
    if (down) {
        rawx = e->button.x;
        rawy = e->button.y;
    } else if (motion) {
        rawx = e->motion.x;
        rawy = e->motion.y;
    } else
        wheel_event_mouse(e, &rawx, &rawy);
    if (!sister_event_mouse(meta_fx_window.window, rawx, rawy, &x, &y))
        return 1;
    if (left && y < 30 && x >= 550) {
        meta_fx_history_finish(device, audio, pages, bank);
        meta_fx_hide();
        return 1;
    }
    if (left && y >= 334 && y < 356 && x < 500) {
        meta_fx_window.edit = 1;
        SDL_StartTextInput();
        snprintf(meta_fx_window.status, 128,
                 "FILE PATH: CTRL+A REPLACES | ENTER FINISHES | .TSMFX IS PORTABLE");
        return 1;
    }
    if (left && y >= 304 && y < 328 && x < 160) {
        meta_fx_window.maps = x >= 88;
        meta_fx_window.edit = 0;
        return 1;
    }
    if (left && meta_fx_window.maps && y >= 304 && y < 328 && x >= 266 && x < 362) {
        meta_fx_window.page = (meta_fx_window.page + 1) % 3;
        return 1;
    }
    if (ui->midi_learn_active) {
        if (left && y >= 36 && y < 60 && x >= 12 && x < 636) {
            char target[80];
            snprintf(target, sizeof(target), "matrix.meta.%u.%d", meta_fx_window.node, (x - 12) / 156);
            midi_learn_click(ui, sister, target);
        }
        return 1;
    }
    if (!mx_editable())
        return 1;
    if (motion && meta_fx_window.drag < 0)
        return 1;
    if (motion && !(e->motion.state & SDL_BUTTON_LMASK)) {
        meta_fx_history_finish(device, audio, pages, bank);
        meta_fx_window.drag = -1;
        return 1;
    }
    if (!meta_fx_window.before) {
        meta_fx_window.before = mx_snapshot(device, audio, pages, bank);
        if (!meta_fx_window.before)
            return 1;
    }
    TsMatrixEffectControls old = *control;
    int changed = 0, field = -1;
    if (motion)
        field = meta_fx_window.drag;
    else if (y >= 36 && y < 60 && x >= 12 && x < 636) {
        int macro = (x - 12) / 156;
        if (down && e->button.button == SDL_BUTTON_RIGHT) {
            meta_fx_window.edit = 3 + macro;
            SDL_StartTextInput();
            snprintf(meta_fx_window.status, 128, "RENAME MACRO: CTRL+A REPLACES | ENTER FINISHES");
            return 1;
        }
        field = macro;
    } else if (y < 30 && x < 400 && down && e->button.button == SDL_BUTTON_RIGHT) {
        meta_fx_window.edit = 2;
        SDL_StartTextInput();
        return 1;
    } else if (left && y < 30 && x >= 462 && x < 524) {
        ts_meta_preset(c,
                       (c->preset + (x < 490 ? TS_META_PRESETS - 1 : 1) + TS_META_PRESETS) % TS_META_PRESETS);
        changed = 1;
    } else if (y >= 274 && y < 298) {
        if (left && x < 142) {
            ts_meta_capture(c, x >= 80);
            changed = 1;
        } else if (x >= 150 && x < 330)
            field = 4;
        else if (x >= 340 && x < 476)
            field = 5;
        else if (x >= 486 && x < 624)
            field = 6;
    } else if (y >= 82 && y < 266) {
        int row = (y - 82) / 23;
        if (!meta_fx_window.maps) {
            TsMetaStage *s = &c->stage[row];
            meta_fx_window.selected = row;
            if (left && x >= 12 && x < 36 && row < c->count) {
                s->enabled = !s->enabled;
                changed = 1;
            } else if ((left || wheel) && x >= 40 && x < 140) {
                int type = row < c->count ? s->type : 0;
                type = (type + (wheel ? (int)lrintf(wheel_event_delta(e, 0)) : 1) + TS_SISTER_FX_TYPE_COUNT) %
                       TS_SISTER_FX_TYPE_COUNT;
                ts_meta_stage_default(s, type);
                for (int j = c->map_count - 1; j >= 0; --j)
                    if (c->map[j].stage == row &&
                        (!type || (c->map[j].field >= 5 && type != TS_SISTER_FX_FILTER) ||
                         (c->map[j].field < 3 && !*ts_meta_parameter_name(type, c->map[j].field))))
                        meta_fx_map_remove(c, j);
                if (row >= c->count)
                    c->count = row + 1;
                changed = 1;
            } else if (row < c->count && x >= 146 && x < 526 && (x - 146) % 76 < 70)
                field = 100 + row * 5 + (x - 146) / 76;
            else if (left && row < c->count && x >= 530) {
                if (x >= 594)
                    meta_fx_stage_remove(c, row);
                else {
                    int dest = row + (x < 558 ? -1 : 1);
                    if (dest >= 0 && dest < c->count) {
                        TsMetaStage temp = c->stage[row];
                        c->stage[row] = c->stage[dest];
                        c->stage[dest] = temp;
                        for (int i = 0; i < c->map_count; ++i) {
                            if (c->map[i].stage == row)
                                c->map[i].stage = dest;
                            else if (c->map[i].stage == dest)
                                c->map[i].stage = row;
                        }
                    }
                }
                changed = 1;
            }
        } else {
            int index = meta_fx_window.page * 8 + row;
            if (index < c->map_count) {
                TsMetaMap *m = &c->map[index];
                if (left && x < 148) {
                    m->macro = (m->macro + 1) % 4;
                    changed = 1;
                } else if (left && x >= 154 && x < 286 && c->count) {
                    for (int step = 1; step <= c->count; ++step) {
                        int stage = (m->stage + step) % c->count;
                        int f = meta_fx_target_available(c, index, stage, m->field)
                                    ? m->field
                                    : meta_fx_next_target(c, index, stage, -1, 1);
                        if (f >= 0) {
                            m->stage = stage;
                            m->field = f;
                            changed = 1;
                            break;
                        }
                    }

                } else if ((left || wheel) && x >= 292 && x < 402) {
                    int f = meta_fx_next_target(c, index, m->stage, m->field,
                                                wheel && wheel_event_delta(e, 0) < 0 ? -1 : 1);
                    if (f >= 0) {
                        m->field = f;
                        changed = 1;
                    }
                } else if (x >= 410 && x < 496)
                    field = 200 + index * 2;
                else if (x >= 504 && x < 590)
                    field = 201 + index * 2;
                else if (left && x >= 596) {
                    meta_fx_map_remove(c, index);
                    changed = 1;
                }
            }
        }
    } else if (left && y >= 304 && y < 328 && meta_fx_window.maps && x >= 168 && x < 258) {
        if (c->count && c->map_count < TS_META_MAPS) {
            int stage = SDL_clamp(meta_fx_window.selected, 0, c->count - 1);
            int found = meta_fx_next_target(c, -1, stage, -1, 1);
            if (found >= 0) {
                c->map[c->map_count++] = (TsMetaMap){0, (uint8_t)stage, (uint8_t)found, 0, 1};
                meta_fx_window.page = (c->map_count - 1) / 8;
                changed = 1;
            }
        }
    } else if (y >= 304 && y < 328 && x >= 462) {
        if (wheel) {
            if (SDL_GetModState() & KMOD_SHIFT)
                meta_fx_window.count = SDL_clamp(meta_fx_window.count + wheel_event_delta(e, 0), 1, 8);
            else
                meta_fx_window.first = SDL_clamp(meta_fx_window.first + wheel_event_delta(e, 0), 0, 19);
        } else if (left) {
            ts_meta_from_board(c, &sister->model.parameters.fx, meta_fx_window.first, meta_fx_window.count);
            changed = 1;
        }
        snprintf(meta_fx_window.status, 128,
                 "BOARD SLOTS %d..%d | WHEEL: FIRST | SHIFT+WHEEL: COUNT | CLICK: COPY SETTINGS",
                 meta_fx_window.first + 1, meta_fx_window.first + meta_fx_window.count);
    } else if (left && y >= 334 && y < 356) {
        if (x >= 568) {
            ts_meta_save(c, meta_fx_window.path, meta_fx_window.status, 128);
        } else if (x >= 504)
            changed = ts_meta_load(c, meta_fx_window.path, meta_fx_window.status, 128);
    }
    if (field >= 100 && field < 140) {
        int stage = (field - 100) / 5, target = (field - 100) % 5;
        if (target < 3 && !*ts_meta_parameter_name(c->stage[stage].type, target))
            field = -1;
        for (int i = 0; i < c->map_count; ++i)
            if (c->map[i].stage == stage && ts_meta_target_field(c->stage[stage].type, c->map[i].field) ==
                                                ts_meta_target_field(c->stage[stage].type, target)) {
                snprintf(meta_fx_window.status, 128,
                         "THIS PARAMETER FOLLOWS A MACRO. EDIT ITS LOW/HIGH RANGE IN MAPS.");
                field = -1;
                meta_fx_window.drag = -1;
                break;
            }
    }
    if (field >= 0) {
        float value = meta_fx_get(control, field);
        if (motion)
            value = meta_fx_window.origin +
                    (x - meta_fx_window.origin_x) / ((SDL_GetModState() & KMOD_SHIFT) ? 2500.f : 250.f);
        else if (wheel)
            value += wheel_event_delta(e, 0) * ((SDL_GetModState() & KMOD_SHIFT) ? .002f : .02f);
        else if (down && e->button.button == SDL_BUTTON_RIGHT)
            value = field == 5 ? 1 : .5f;
        else if (left) {
            meta_fx_window.drag = field;
            meta_fx_window.origin = value;
            meta_fx_window.origin_x = x;
        }
        if (motion || wheel || (down && e->button.button == SDL_BUTTON_RIGHT)) {
            meta_fx_set(control, field, value);
            changed = 1;
        }
    }
    if (changed) {
        if (ts_meta_valid(c)) {
            mx_commit_controls(device, audio, ui);
            ui->project_modified = 1;
        } else {
            *control = old;
            snprintf(meta_fx_window.status, 128, "UNCHANGED: EACH TARGET MAY BE ASSIGNED TO ONE MACRO");
        }
    }
    if (meta_fx_window.drag < 0)
        meta_fx_history_finish(device, audio, pages, bank);
    return 1;
}
static int meta_fx_matrix_midi(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, const char *target,
                               float value) {
    unsigned id;
    int field;
    char tail;
    if (sscanf(target, "matrix.meta.%u.%d%c", &id, &field, &tail) != 2)
        return 0;
    if (field < 0 || field > 6)
        return 0;
    matrix_window.controls = audio->sister.matrix.controls;
    for (int i = 0; i < TS_MATRIX_EFFECT_CAPACITY; ++i) {
        TsMatrixEffectControls *c = &matrix_window.controls.effect[i];
        if (c->id != id || c->type != TS_MATRIX_FX_META)
            continue;
        meta_fx_set(c, field, value);
        mx_commit_controls(device, audio, ui);
        ui->project_modified = 1;
        return 1;
    }
    return 1;
}
static void meta_fx_close(void) {
    free(meta_fx_window.before);
    if (meta_fx_window.texture)
        SDL_DestroyTexture(meta_fx_window.texture);
    if (meta_fx_window.renderer)
        SDL_DestroyRenderer(meta_fx_window.renderer);
    if (meta_fx_window.window)
        SDL_DestroyWindow(meta_fx_window.window);
    memset(&meta_fx_window, 0, sizeof(meta_fx_window));
    meta_fx_window.drag = -1;
}
