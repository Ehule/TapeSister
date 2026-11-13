/* Canvas gestures use their own drag range; parameter editors already use 10..40. */
static int fx_canvas_key(SDL_Keycode key)
{return key==SDLK_t || key==SDLK_F2 || key==SDLK_ESCAPE || key==SDLK_DELETE || key==SDLK_BACKSPACE || key==SDLK_HOME || key==SDLK_z || key==SDLK_m || key==SDLK_g || key==SDLK_s || key==SDLK_x;}
static void fx_bypass_selection(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    MatrixWindow *w=&matrix_window;int any=0,count=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        TsMatrixEffectControls *c=&w->controls.effect[i];MatrixRow row={.kind=4,.tile=c->id};
        if(c->id && fx_chosen(row)){any|=c->enabled;++count;}
    }
    if(!count){snprintf(w->status,sizeof(w->status),"SELECT AN ADDED EFFECT TO BYPASS. FIXED PROCESSORS USE THEIR OWN POWER CONTROLS.");return;}
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){TsMatrixEffectControls *c=&w->controls.effect[i];
        MatrixRow row={.kind=4,.tile=c->id};if(c->id && fx_chosen(row))c->enabled=!any;}
    mx_commit_controls(device,audio,ui);
    snprintf(w->status,sizeof(w->status),"%d EFFECT%s %s / M: TOGGLE / CTRL+Z: UNDO",count,count==1?"":"S",any?"BYPASSED":"ACTIVE");
}
static int fx_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int x,int y,int wheel)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;
    if(fx_canvas.label_editing)return fx_label_edit_event(e);
    if(fx_canvas.properties)return 1;
    if(fx_canvas.confirm_delete) {
        int choice=-1;
        if(e->type==SDL_KEYDOWN) {
            if(e->key.keysym.sym==SDLK_ESCAPE)choice=2;
            else if(e->key.keysym.sym==SDLK_y)choice=1;
            else if(e->key.keysym.sym==SDLK_n)choice=0;
        }
        if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT && y>=448 && y<476) {
            if(x>=324 && x<514)choice=1;else if(x>=532 && x<722)choice=0;else if(x>=740 && x<930)choice=2;
        }
        if(choice==2 || (choice>=0 && fx_delete_effects(choice,device,audio,ui,pages,bank)))fx_canvas.confirm_delete=0;
        return 1;
    }
    if(fx_label_drag_event(e,x,y))return 1;
    if(fx_canvas.label_selected>=l->label_count)fx_canvas.label_selected=-1;
    if(e->type==SDL_KEYUP){if(fx_canvas_key(e->key.keysym.sym) || ((e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_a || e->key.keysym.sym==SDLK_n || e->key.keysym.sym==SDLK_c || e->key.keysym.sym==SDLK_v)))return 1;
        e->key.windowID=SDL_GetWindowID(main);return 0;}
    if(fx_canvas.movement_popup) {
        if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE)fx_canvas.movement_popup=0;
        if(e->type==SDL_MOUSEBUTTONDOWN) {
            if(e->button.button==SDL_BUTTON_LEFT && x>=20 && x<520 && y>=90 && w->gallery) {
                int row=(y-90)/32;
                if(row<=w->gallery->count) {
                    fx_canvas.movement=row?w->gallery->canvas[row-1].id:0;
                    memset(fx_canvas.revealed,0,sizeof(fx_canvas.revealed));
                    fx_clear_selection();fx_clear_wires();w->effect_open=w->compressor_open=0;
                    fx_build(pages,bank);fx_fit();
                    snprintf(w->status,sizeof(w->status),"VIEW FILTER ONLY / PLAYBACK AND ROUTING UNCHANGED");
                }
            }
            fx_canvas.movement_popup=0;
        }
        return 1;
    }
    if(fx_canvas.confirm_clear) {
        if(e->type==SDL_KEYDOWN && (e->key.keysym.sym==SDLK_ESCAPE || e->key.keysym.sym==SDLK_RETURN))fx_canvas.confirm_clear=0;
        if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
            if(y>=445 && y<473 && x>=710 && x<918) {
                mx_reset_routes(device,audio,ui,pages,bank,0);fx_clear_selection();fx_clear_wires();fx_canvas.confirm_clear=0;
            } else if(y>=445 && y<473 && x>=362 && x<542)fx_canvas.confirm_clear=0;
        }
        return 1;
    }
    if(fx_canvas.popup)return fx_popup_event(e,x,y,wheel,device,audio,ui,pages,bank);
    if(fx_modal()) {
        if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){fx_cancel_gesture();snprintf(w->status,sizeof(w->status),"CANVAS EDIT CANCELLED.");}
        else if((e->type==SDL_KEYDOWN && (e->key.keysym.sym==SDLK_RETURN || e->key.keysym.sym==SDLK_KP_ENTER)) ||
                (e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT)) {
            if(e->type==SDL_MOUSEBUTTONDOWN)fx_transform_motion(x,y);
            w->drag=0;SDL_CaptureMouse(SDL_FALSE);snprintf(w->status,sizeof(w->status),"CANVAS EDIT PLACED / CTRL+Z: UNDO.");
        } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_RIGHT)fx_cancel_gesture();
        else if(e->type==SDL_MOUSEMOTION){fx_transform_motion(x,y);fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;}
        return 1;
    }
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;int mod=e->key.keysym.mod;
        if(e->key.repeat && (fx_canvas_key(key) || (mod&KMOD_CTRL)))return 1;
        if(key==SDLK_t && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) && !fx_canvas.meta) {
            fx_canvas.place_x=(SDL_clamp(fx_canvas.mouse_x,20,fx_right())-20)/l->canvas_zoom-l->canvas_x;
            fx_canvas.place_y=(SDL_clamp(fx_canvas.mouse_y,FX_TOP,FX_BOTTOM)-FX_TOP)/l->canvas_zoom-l->canvas_y;
            fx_label_begin(-1);return 1;
        }
        if(fx_canvas.label_selected>=0 && !fx_canvas.meta) {
            int i=fx_canvas.label_selected;
            if(key==SDLK_F2){fx_label_begin(i);return 1;}
            if(key==SDLK_DELETE || key==SDLK_BACKSPACE || key==SDLK_x) {
                memmove(l->labels+i,l->labels+i+1,(size_t)(l->label_count-i-1)*sizeof(l->labels[0]));
                memset(l->labels+--l->label_count,0,sizeof(l->labels[0]));fx_canvas.label_selected=-1;return 1;
            }
            if(!(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) && (key==SDLK_g || key==SDLK_s)) {
                fx_label_drag(key==SDLK_g?FX_DRAG_LABEL_GRAB:FX_DRAG_LABEL_SCALE,fx_canvas.mouse_x,fx_canvas.mouse_y);return 1;
            }
            if(key==SDLK_ESCAPE){fx_canvas.label_selected=-1;return 1;}
        }
        if(key==SDLK_F2 && w->selection_count==1){fx_properties_begin(w->selection[0]);return 1;}
        if(key==SDLK_ESCAPE) {
            if(fx_canvas.popup){fx_canvas.popup=0;return 1;}
            if(w->drag){fx_cancel_gesture();return 1;}
            if(fx_canvas.meta)fx_canvas.meta=0;else if(w->effect_open)w->effect_open=0;else matrix_hide();return 1;
        }
        if(fx_canvas.popup)return 1;
        if(key==SDLK_HOME || key==SDLK_z){if(!fx_canvas.meta){fx_build(pages,bank);fx_fit_selection(key==SDLK_z && !(mod&KMOD_SHIFT));}return 1;}
        if(key==SDLK_m){if(mx_editable())fx_bypass_selection(device,audio,ui);return 1;}
        if(!fx_canvas.meta && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) && (key==SDLK_g || key==SDLK_s)) {
            fx_build(pages,bank);fx_begin_transform(key==SDLK_g?FX_DRAG_GRAB:FX_DRAG_SCALE);return 1;
        }
        if((mod&KMOD_CTRL) && (key==SDLK_c || key==SDLK_v)) {
            if(!fx_canvas.meta){fx_build(pages,bank);if(key==SDLK_c)fx_copy();else fx_paste(device,audio,ui,pages,bank);}return 1;
        }
        if(key==SDLK_DELETE || key==SDLK_BACKSPACE || key==SDLK_x) {
            if(!fx_canvas.meta){if(fx_selected_effects())fx_delete_request(-1);else fx_edit_selection(1,0,device,audio,ui,pages,bank);}return 1;
        }
        if((mod&KMOD_CTRL) && key==SDLK_a){if(!fx_canvas.meta){fx_clear_wires();fx_clear_selection();fx_build(pages,bank);
            for(int i=0;i<fx_canvas.count;++i)w->selection[w->selection_count++]=fx_canvas.node[i].row;}return 1;}
        if((mod&KMOD_CTRL) && key==SDLK_n){if(!fx_canvas.meta)fx_popup_open(400,220);return 1;}
        e->key.windowID=SDL_GetWindowID(main);return 0;
    }
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(left && !fx_canvas.popup && fx_toolbar_hit(FX_TOOL_CPU,x,y)) {
        ts_audio_health_show_modules(ui);return 1;
    }
    if(e->type==SDL_MOUSEMOTION){fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;}
    if(e->type==SDL_MOUSEBUTTONUP) {
        if(w->drag==FX_DRAG_SHIFT_NODE && !fx_canvas.shift_moved) {
            for(int i=0;i<fx_canvas.count;++i)if(mx_same(fx_canvas.node[i].row,fx_canvas.source)) {
                FxCanvasNode *n=&fx_canvas.node[i];fx_select(n,1);
                w->effect_open=fx_chosen(n->row) && n->row.kind==4?(uint32_t)n->row.tile:0;w->compressor_open=0;break;
            }
        } else if((w->drag==FX_DRAG_WIRE || w->drag==FX_DRAG_SHIFT_NODE) && fx_canvas.wire>=0) {
            int at=fx_source_at(fx_canvas.source);MatrixRow source=fx_canvas.source;
            int body_drag=w->drag==FX_DRAG_SHIFT_NODE;fx_build(pages,bank);int connected=-1;
            for(int i=0;x>=20 && x<fx_right() && y>=FX_TOP && y<FX_BOTTOM && i<fx_canvas.count;++i) {
                FxCanvasNode *n=&fx_canvas.node[i];int xx=fx_x(n->x),yy=fx_y(n->y+fx_node_h(n)*.5f),dest=n->destination;
                int sc=dest>=0?ts_matrix_sidechain_for(dest):-1;
                if(sc>=0 && ts_matrix_destination_active(&w->controls,sc) && abs(y-fx_y(n->y+fx_node_h(n)*.8f))<13 && abs(y-fx_y(n->y+fx_node_h(n)*.8f))<abs(y-yy)){yy=fx_y(n->y+fx_node_h(n)*.8f);dest=sc;}
                int body=body_drag && x>=xx && x<=fx_x(n->x+fx_node_w(n)) && y>=fx_y(n->y) && y<=fx_y(n->y+fx_node_h(n));
                if(at>=0 && dest>=0 && (body || (abs(x-xx)<18 && abs(y-yy)<18))) {
                    mx_select(at,0);w->destination=dest;
                    if(body)mx_toggle(device,audio,ui,pages,bank);else mx_gain(device,audio,ui,pages,bank,1000);
                    if(mx_display_route(mx_route(source,pages,bank)).matrix_gain[dest])connected=dest;
                    break;
                }
            }
            fx_clear_selection();fx_clear_wires();w->effect_open=w->compressor_open=0;
            if(connected>=0)fx_select_wire(source,connected,0);
        }
        if(w->drag==FX_DRAG_BOX || w->drag==FX_DRAG_WIRE_BOX)fx_box_update(x,y,pages,bank);
        w->drag=0;fx_primary_wire();SDL_CaptureMouse(SDL_FALSE);return 1;
    }
    if(e->type==SDL_MOUSEMOTION && (w->drag==FX_DRAG_NODE || w->drag==FX_DRAG_PAN || w->drag==FX_DRAG_BOX || w->drag==FX_DRAG_WIRE_BOX || w->drag==FX_DRAG_WIRE || w->drag==FX_DRAG_SHIFT_NODE)) {
        if(w->drag==FX_DRAG_NODE)fx_transform_motion(x,y);
        else if(w->drag==FX_DRAG_PAN){l->canvas_x=SDL_clamp(fx_canvas.origin_x+(x-fx_canvas.pointer_x)/l->canvas_zoom,-100000.f,100000.f);l->canvas_y=SDL_clamp(fx_canvas.origin_y+(y-fx_canvas.pointer_y)/l->canvas_zoom,-100000.f,100000.f);}
        else if(w->drag==FX_DRAG_BOX || w->drag==FX_DRAG_WIRE_BOX)fx_box_update(x,y,pages,bank);
        else if(w->drag==FX_DRAG_SHIFT_NODE && abs(x-(int)fx_canvas.pointer_x)+abs(y-(int)fx_canvas.pointer_y)>5)fx_canvas.shift_moved=1;
        return 1;
    }
    if(!fx_canvas.meta && !fx_canvas.popup && x>=20 && x<fx_right() && y>=FX_TOP && y<FX_BOTTOM && wheel && !(SDL_GetModState()&KMOD_SHIFT)) {
        float old=l->canvas_zoom,next=SDL_clamp(old*powf(1.12f,wheel),.001f,2.f);
        x=SDL_clamp(x,20,fx_right());y=SDL_clamp(y,FX_TOP,FX_BOTTOM);
        l->canvas_x+=(x-20)*(1/next-1/old);l->canvas_y+=(y-FX_TOP)*(1/next-1/old);l->canvas_zoom=next;l->canvas_view=1;return 1;
    }
    if(!fx_canvas.meta && !fx_canvas.popup && x>=20 && x<fx_right() && y>=FX_TOP && y<FX_BOTTOM && wheel && (SDL_GetModState()&KMOD_SHIFT) && fx_canvas.wires.count) {
        fx_edit_selection(0,wheel,device,audio,ui,pages,bank);return 1;
    }
    if(left)for(int tool=0;tool<FX_TOOL_CPU;++tool)if(fx_toolbar_hit(tool,x,y)) {
        switch(tool) {
        case FX_TOOL_MATRIX:if(fx_canvas.meta)fx_canvas.meta=0;else fx_view(0,pages,bank);break;
        case FX_TOOL_UNDO:case FX_TOOL_REDO:mx_undo(device,audio,ui,pages,bank,tool==FX_TOOL_REDO);break;
        case FX_TOOL_ADD:fx_canvas.meta=0;w->effect_open=0;fx_popup_open(400,220);break;
        case FX_TOOL_FIT:fx_build(pages,bank);fx_fit();break;
        case FX_TOOL_CLOSE:ui->mosaic_open=1;matrix_hide();SDL_RaiseWindow(main);break;
        case FX_TOOL_HELP:fx_canvas.help=!fx_canvas.help;break;
        case FX_TOOL_EDITOR:fx_view(0,pages,bank);break;
        case FX_TOOL_MOVEMENT:
            if(w->controls.enabled && w->gallery)fx_canvas.movement_popup=1;
            else if(!w->controls.enabled)mx_activate(device,audio,ui,pages,bank);break;
        case FX_TOOL_CURVES:l->canvas_straight=!l->canvas_straight;break;
        case FX_TOOL_SNAP:l->canvas_snap=!l->canvas_snap;break;
        case FX_TOOL_CLEAR:if(mx_editable())fx_canvas.confirm_clear=1;break;
        }
        return 1;
    }
    TsMatrixEffectControls *meta=fx_meta();
    if(meta) {
        if(left && y>=784 && y<812 && x<236){meta_fx_open(meta->id);return 1;}
        if(left && e->button.clicks>=2)for(int i=0;i<meta->meta.count;++i) {
            int nx=146+(i<4?i:7-i)*270,ny=i<4?174:418;
            if(x>=nx && x<nx+FX_NODE_W && y>=ny && y<ny+FX_NODE_H){meta_fx_stage_open(meta->id,i);return 1;}}
        if((left || wheel) && y>=700 && y<770 && mx_editable()) {
            int i=(x-24)/310;if(x>=24 && i>=0 && i<4) {
                float v=meta->meta.scene_mode && meta->meta.captured==3?meta->meta.scene[0][i]+(meta->meta.scene[1][i]-meta->meta.scene[0][i])*meta->meta.morph:meta->meta.macro[i];
                ts_meta_macro_set(&meta->meta,i,wheel?v+wheel*((SDL_GetModState()&KMOD_SHIFT)?.002f:.02f):(x-24-i*310)/286.f);
                mx_commit_controls(device,audio,ui);if(left)w->drag=FX_DRAG_MACRO+i;}return 1;}
        if(e->type==SDL_MOUSEMOTION && w->drag>=FX_DRAG_MACRO && w->drag<FX_DRAG_MACRO+4 && mx_editable()) {
            int i=w->drag-FX_DRAG_MACRO;ts_meta_macro_set(&meta->meta,i,(x-24-i*310)/286.f);mx_commit_controls(device,audio,ui);}return 1;
    }
    if(fx_inspector_event(e,x,y,wheel,device,audio,ui))return 1;
    if(!click)return 1; /* Passive motion needs no graph rebuild or hit test. */
    fx_build(pages,bank);
    if(y<FX_TOP || y>=FX_BOTTOM || x<20 || x>=fx_right())return 1;
    if(click && e->button.button==SDL_BUTTON_MIDDLE){w->drag=FX_DRAG_PAN;fx_canvas.origin_x=l->canvas_x;fx_canvas.origin_y=l->canvas_y;fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;return 1;}
    if(wheel)return 1;
    if(!click)return 1;
    for(int i=l->label_count-1;i>=0;--i) {
        SDL_Rect r=fx_label_rect(&l->labels[i]);
        if(left && x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
            fx_clear_selection();fx_clear_wires();fx_canvas.label_selected=i;w->effect_open=w->compressor_open=0;
            if(e->button.clicks>=2)fx_label_begin(i);else fx_label_drag(FX_DRAG_LABEL,x,y);
            snprintf(w->status,sizeof(w->status),"TEXT LABEL / DOUBLE-CLICK OR F2: EDIT / G: MOVE / S: SIZE / DELETE: REMOVE");return 1;
        }
    }
    fx_canvas.label_selected=-1;
    for(int i=fx_canvas.count-1;i>=0;--i) {
        FxCanvasNode *n=&fx_canvas.node[i];int xx=fx_x(n->x),yy=fx_y(n->y),ww=(int)(fx_node_w(n)*l->canvas_zoom),hh=(int)(fx_node_h(n)*l->canvas_zoom);
        int mod=SDL_GetModState();
        if(left && (mod&KMOD_CTRL) && (mod&KMOD_SHIFT) && x>=xx-12 && x<=xx+ww+12 && y>=yy && y<=yy+hh) {
            fx_connect_selected(n->destination,device,audio,ui,pages,bank);return 1;
        }
        if(left && n->at>=0 && abs(x-(xx+ww))<12 && abs(y-(yy+hh/2))<12){fx_canvas.source=n->row;fx_canvas.wire=i;fx_canvas.destination=-1;w->drag=FX_DRAG_WIRE;fx_canvas.move_count=0;fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;return 1;}
        if(x<xx || x>xx+ww || y<yy || y>yy+hh)continue;
        if(e->button.button==SDL_BUTTON_RIGHT){fx_select(n,0);fx_properties_begin(n->row);return 1;}
        if(!left)return 1;
        if(mod&KMOD_SHIFT){fx_canvas.source=n->row;fx_canvas.wire=i;fx_canvas.destination=-1;w->drag=FX_DRAG_SHIFT_NODE;fx_canvas.shift_moved=0;
            fx_canvas.pointer_x=fx_canvas.mouse_x=x;fx_canvas.pointer_y=fx_canvas.mouse_y=y;SDL_CaptureMouse(SDL_TRUE);return 1;}
        if(!(mod&KMOD_CTRL))fx_clear_wires();
        if(mod&KMOD_CTRL)fx_select(n,1);else if(!fx_chosen(n->row))fx_select(n,0);
        w->effect_open=n->row.kind==4?(uint32_t)n->row.tile:0;w->effect_menu=0;
        w->compressor_open=n->row.kind==2 && n->row.index==TS_MATRIX_COMPRESSOR;
        if(e->button.clicks>=2 && n->row.kind==2 && n->row.index==TS_MATRIX_BOARD){sister_board_open(0);return 1;}
        if(e->button.clicks>=2 && n->row.kind==4) {
            TsMatrixEffectControls *c=&w->controls.effect[ts_matrix_effect_index(n->row.index)];
            if(c->type==TS_MATRIX_FX_PEDALBOARD){sister_board_open(c->id);return 1;}
            if(c->type==TS_MATRIX_FX_SUNVOX){svfx_open(c->id);return 1;}
            if(c->type==TS_MATRIX_FX_META){fx_canvas.meta=c->id;w->effect_open=0;return 1;}
            if(c->type==TS_MATRIX_FX_FILTER){character_filter_matrix_open(c->id);return 1;}}
        if(fx_chosen(n->row)) {fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;fx_begin_transform(FX_DRAG_NODE);}
        return 1;
    }
    if(e->button.button==SDL_BUTTON_RIGHT || (left && e->button.clicks>=2)){fx_canvas.wire=-1;fx_popup_open(x,y);return 1;}
    if(!left)return 1;
    int modifiers=SDL_GetModState();
    if(modifiers&KMOD_CTRL){fx_begin_box(x,y,1,(modifiers&KMOD_SHIFT)!=0);return 1;}
    float nearest=64;int found=-1,destination=-1;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
        TsSourceRoute raw=mx_route(n->row,pages,bank);if(n->row.kind==3 && !raw.matrix_enabled)continue;
        TsSourceRoute r=mx_display_route(raw);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
            FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
            float distance=fx_cable(fx_xf(n->x+fx_node_w(n)),fx_yf(n->y+fx_node_h(n)*.5f),fx_xf(to->x),fx_yf(to->y+fx_node_h(to)*(d==to->destination?.5f:.8f)),0,0,x,y);
            if(distance<nearest){nearest=distance;found=i;destination=d;}}}
    if(found>=0) {
        if(!(modifiers&KMOD_SHIFT)){fx_clear_selection();fx_clear_wires();}
        fx_select_wire(fx_canvas.node[found].row,destination,(modifiers&KMOD_SHIFT)!=0);
        w->effect_open=w->compressor_open=0;
        snprintf(w->status,sizeof(w->status),"%d CABLES SELECTED / SHIFT+WHEEL: GAIN / WHEEL: ZOOM / X OR DELETE: REMOVE",fx_canvas.wires.count);
    } else fx_begin_box(x,y,0,(modifiers&KMOD_SHIFT)!=0);
    return 1;
}
