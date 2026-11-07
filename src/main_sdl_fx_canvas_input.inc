/* Canvas gestures use their own drag range; parameter editors already use 10..40. */
static int fx_canvas_key(SDL_Keycode key)
{return key==SDLK_ESCAPE || key==SDLK_DELETE || key==SDLK_BACKSPACE || key==SDLK_HOME || key==SDLK_z || key==SDLK_m || key==SDLK_g || key==SDLK_s || key==SDLK_x;}
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
    if(e->type==SDL_KEYUP){if(fx_canvas_key(e->key.keysym.sym) || ((e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_a || e->key.keysym.sym==SDLK_n || e->key.keysym.sym==SDLK_c || e->key.keysym.sym==SDLK_v)))return 1;
        e->key.windowID=SDL_GetWindowID(main);return 0;}
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
            if(!fx_canvas.meta)fx_edit_selection(1,0,device,audio,ui,pages,bank);return 1;
        }
        if((mod&KMOD_CTRL) && key==SDLK_a){if(!fx_canvas.meta){fx_clear_wires();fx_clear_selection();fx_build(pages,bank);
            for(int i=0;i<fx_canvas.count;++i)w->selection[w->selection_count++]=fx_canvas.node[i].row;}return 1;}
        if((mod&KMOD_CTRL) && key==SDLK_n){if(!fx_canvas.meta)fx_popup_open(400,220);return 1;}
        e->key.windowID=SDL_GetWindowID(main);return 0;
    }
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
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
            for(int i=0;i<fx_canvas.count;++i) {
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
    if(!fx_canvas.meta && !fx_canvas.popup && wheel && !(SDL_GetModState()&KMOD_SHIFT)) {
        float old=l->canvas_zoom,next=SDL_clamp(old*powf(1.12f,wheel),.001f,2.f);
        x=SDL_clamp(x,20,FX_RIGHT);y=SDL_clamp(y,FX_TOP,FX_BOTTOM);
        l->canvas_x+=(x-20)*(1/next-1/old);l->canvas_y+=(y-FX_TOP)*(1/next-1/old);l->canvas_zoom=next;l->canvas_view=1;return 1;
    }
    if(!fx_canvas.meta && wheel && (SDL_GetModState()&KMOD_SHIFT) && fx_canvas.wires.count) {
        fx_edit_selection(0,wheel,device,audio,ui,pages,bank);return 1;
    }
    if(fx_canvas.popup) {
        if(click){int px=fx_canvas.popup_x,py=fx_canvas.popup_y;
            if(left && x>=px && x<px+274 && y>=py+32 && y<py+32+(TS_MATRIX_FX_TYPES-1)*30) {
                int type=1+(y-py-32)/30;fx_canvas.popup=0;mx_effect_add(device,audio,ui,pages,bank,type);
                if(w->effect_open){w->filter=0;mx_rows(pages,bank);fx_build(pages,bank);
                    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==w->effect_open) {
                        TsFxCanvasPosition *p=fx_position(fx_canvas.node[i].row,1);
                        if(p){p->x=fx_canvas.place_x;p->y=fx_canvas.place_y;}fx_clear_wires();fx_select(&fx_canvas.node[i],0);break;}}
            }else fx_canvas.popup=0;
        }return 1;
    }
    if(left && y>=18 && y<46) {
        if(x>=292 && x<492){if(fx_canvas.meta)fx_canvas.meta=0;else fx_view(0,pages,bank);}
        else if(x>=502 && x<696)mx_undo(device,audio,ui,pages,bank,x>=604);
        else if(x>=706 && x<898){fx_canvas.meta=0;w->effect_open=0;fx_popup_open(400,220);}
        else if(x>=908 && x<1008){fx_build(pages,bank);fx_fit();}
        else if(x>=1018){ui->mosaic_open=1;matrix_hide();SDL_RaiseWindow(main);}return 1;
    }
    if(left && y>=70 && y<98 && !fx_canvas.meta) {
        if(!w->controls.enabled && x>=300 && x<584)mx_activate(device,audio,ui,pages,bank);
        else if(x>=660 && x<856)l->canvas_straight=!l->canvas_straight;
        else if(x>=866 && x<1254){
            mx_reset_routes(device,audio,ui,pages,bank,0);
            fx_clear_selection();fx_clear_wires();
        }return 1;
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
    int slot=mx_effect_slot();
    if(w->drag>=FX_DRAG_CONTROL && w->drag<FX_DRAG_CONTROL+7) {
        if(e->type==SDL_MOUSEMOTION && slot>=0 && mx_editable()){int i=w->drag-FX_DRAG_CONTROL;
            TsMatrixEffectControls *c=&w->controls.effect[slot];float v=(x-1028)/208.f;
            if(c->type==TS_MATRIX_FX_META)ts_meta_macro_set(&c->meta,i,v);else mx_effect_value_set(c,i,v);mx_commit_controls(device,audio,ui);}return 1;
    }
    if(w->effect_open && w->drag>=20 && w->drag<=40){mx_effect_event(e,device,audio,ui,pages,bank,x,y,wheel);return 1;}
    if(w->compressor_open && w->drag>=10 && w->drag<20){mx_compressor_event(e,device,audio,ui,x,y,wheel);return 1;}
    if(x>=1008 && y>=FX_TOP && y<FX_BOTTOM) {
        if(slot>=0 && mx_editable()) {
            TsMatrixEffectControls *c=&w->controls.effect[slot];int count=c->type==TS_MATRIX_FX_META?4:mx_effect_controls(c);
            if(left && y>=178 && y<206){c->enabled=!c->enabled;mx_commit_controls(device,audio,ui);return 1;}
            if(c->type==TS_MATRIX_FX_SUNVOX && left && y>=480 && y<508){svfx_open(c->id);return 1;}
            if(c->type==TS_MATRIX_FX_META && left && y>=480 && y<508){fx_canvas.meta=c->id;return 1;}
            int i=(y-230)/53;
            if(x>=1028 && x<=1236 && y>=230 && i<count && (y-230)%53<49) {
                float v=c->type==TS_MATRIX_FX_META?(c->meta.scene_mode && c->meta.captured==3?c->meta.scene[0][i]+(c->meta.scene[1][i]-c->meta.scene[0][i])*c->meta.morph:c->meta.macro[i]):mx_effect_value(c,i);
                if(wheel)v+=wheel*((SDL_GetModState()&KMOD_SHIFT)?.002f:.02f);else if(left)v=(x-1028)/208.f;
                else if(click && e->button.button==SDL_BUTTON_RIGHT){TsMatrixEffectControls defaults;ts_matrix_effect_default(&defaults,c->type,c->id);v=c->type==TS_MATRIX_FX_META?defaults.meta.macro[i]:mx_effect_value(&defaults,i);}else return 1;
                if(c->type==TS_MATRIX_FX_META)ts_meta_macro_set(&c->meta,i,v);else mx_effect_value_set(c,i,v);
                mx_commit_controls(device,audio,ui);if(left)w->drag=FX_DRAG_CONTROL+i;
            }
        }return 1;
    }
    if(w->effect_open && (y>=692 || (w->drag>=20 && w->drag<=40))){mx_effect_event(e,device,audio,ui,pages,bank,x,y,wheel);return 1;}
    if(w->compressor_open && (y>=692 || (w->drag>=10 && w->drag<20))){mx_compressor_event(e,device,audio,ui,x,y,wheel);return 1;}
    fx_build(pages,bank);
    if(y<FX_TOP || y>=FX_BOTTOM || x<20 || x>FX_RIGHT)return 1;
    if(click && e->button.button==SDL_BUTTON_MIDDLE){w->drag=FX_DRAG_PAN;fx_canvas.origin_x=l->canvas_x;fx_canvas.origin_y=l->canvas_y;fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;return 1;}
    if(wheel)return 1;
    if(!click)return 1;
    for(int i=fx_canvas.count-1;i>=0;--i) {
        FxCanvasNode *n=&fx_canvas.node[i];int xx=fx_x(n->x),yy=fx_y(n->y),ww=(int)(fx_node_w(n)*l->canvas_zoom),hh=(int)(fx_node_h(n)*l->canvas_zoom);
        if(left && n->at>=0 && abs(x-(xx+ww))<12 && abs(y-(yy+hh/2))<12){fx_canvas.source=n->row;fx_canvas.wire=i;fx_canvas.destination=-1;w->drag=FX_DRAG_WIRE;fx_canvas.move_count=0;fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;return 1;}
        if(x<xx || x>xx+ww || y<yy || y>yy+hh)continue;
        if(!left)return 1;int mod=SDL_GetModState();
        if(mod&KMOD_SHIFT){fx_canvas.source=n->row;fx_canvas.wire=i;fx_canvas.destination=-1;w->drag=FX_DRAG_SHIFT_NODE;fx_canvas.shift_moved=0;
            fx_canvas.pointer_x=fx_canvas.mouse_x=x;fx_canvas.pointer_y=fx_canvas.mouse_y=y;SDL_CaptureMouse(SDL_TRUE);return 1;}
        if(!(mod&KMOD_CTRL))fx_clear_wires();
        if(mod&KMOD_CTRL)fx_select(n,1);else if(!fx_chosen(n->row))fx_select(n,0);
        w->effect_open=n->row.kind==4?(uint32_t)n->row.tile:0;w->effect_menu=0;
        w->compressor_open=n->row.kind==2 && n->row.index==TS_MATRIX_COMPRESSOR;
        if(e->button.clicks>=2 && n->row.kind==4) {
            TsMatrixEffectControls *c=&w->controls.effect[ts_matrix_effect_index(n->row.index)];
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
        FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;TsSourceRoute r=mx_display_route(mx_route(n->row,pages,bank));
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
            FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
            float distance=fx_cable(fx_x(n->x+fx_node_w(n)),fx_y(n->y+fx_node_h(n)*.5f),fx_x(to->x),fx_y(to->y+fx_node_h(to)*(d==to->destination?.5f:.8f)),0,0,x,y);
            if(distance<nearest){nearest=distance;found=i;destination=d;}}}
    if(found>=0) {
        if(!(modifiers&KMOD_SHIFT)){fx_clear_selection();fx_clear_wires();}
        fx_select_wire(fx_canvas.node[found].row,destination,(modifiers&KMOD_SHIFT)!=0);
        w->effect_open=w->compressor_open=0;
        snprintf(w->status,sizeof(w->status),"%d CABLES SELECTED / SHIFT+WHEEL: GAIN / WHEEL: ZOOM / X OR DELETE: REMOVE",fx_canvas.wires.count);
    } else fx_begin_box(x,y,0,(modifiers&KMOD_SHIFT)!=0);
    return 1;
}
