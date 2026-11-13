/* Handle the splitter before Matrix undo snapshots: pane resizing only writes
   UI metadata. It never locks audio, stages effects, or changes graph zoom. */
static int fx_divider_event(SDL_Event *e)
{
    MatrixWindow *w=&matrix_window;
    int own=w->visible && event_window_id(e)==w->id;
    if(!own || !w->canvas || sunvox_editor.node || fx_canvas.meta || fx_canvas.popup || fx_canvas.confirm_clear ||
       fx_canvas.confirm_delete || fx_canvas.properties || fx_canvas.label_editing) {
        fx_divider_cursor(0);return 0;
    }
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_LEAVE && w->drag!=FX_DRAG_DIVIDER)fx_divider_cursor(0);
        return 0; /* Window close/focus loss use the shared gesture cancellation. */
    }
    if(w->drag==FX_DRAG_DIVIDER && (e->type==SDL_KEYDOWN || e->type==SDL_KEYUP)) {
        if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){fx_cancel_gesture();w->dirty=1;}
        else if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_F9){fx_cancel_gesture();return 0;}
        return 1;
    }
    int rx,ry,width,height;SDL_GetWindowSize(w->window,&width,&height);
    if(e->type==SDL_MOUSEMOTION){rx=e->motion.x;ry=e->motion.y;}
    else if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP){rx=e->button.x;ry=e->button.y;}
    else if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&rx,&ry);
    else return 0;
    int x=rx*MATRIX_W/SDL_max(1,width),y=ry*MATRIX_H/SDL_max(1,height);
    if(w->drag==FX_DRAG_DIVIDER) {
        if(e->type==SDL_MOUSEMOTION || (e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT))
            w->layout->canvas_split=SDL_clamp(fx_canvas.divider_start+x-fx_canvas.divider_pointer,
                TS_FX_CANVAS_SPLIT_MIN,TS_FX_CANVAS_SPLIT_MAX);
        if(e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT){
            w->drag=0;SDL_CaptureMouse(SDL_FALSE);
            fx_divider_cursor(x>=fx_split() && x<fx_split()+8 && y>=FX_TOP && y<FX_BOTTOM);
        }
        if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_RIGHT)fx_cancel_gesture();
        w->dirty=1;return 1;
    }
    int hit=!w->drag && !w->pending && x>=fx_split() && x<fx_split()+8 && y>=FX_TOP && y<FX_BOTTOM;
    if(hit!=fx_canvas.divider_hover)w->dirty=1;
    fx_divider_cursor(hit);
    if(!hit)return 0;
    if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        if(e->button.clicks>=2){
            w->layout->canvas_split=TS_FX_CANVAS_SPLIT_DEFAULT;
            fx_divider_cursor(x>=fx_split() && x<fx_split()+8);
        }
        else {
            fx_canvas.divider_start=w->layout->canvas_split;fx_canvas.divider_pointer=x;
            w->drag=FX_DRAG_DIVIDER;SDL_CaptureMouse(SDL_TRUE);
        }
        snprintf(w->status,sizeof(w->status),"DRAG DIVIDER: RESIZE PANES / DOUBLE-CLICK: RESET / ESC: CANCEL / SAVED WITH PROJECT");
        w->dirty=1;
    }
    return 1;
}
