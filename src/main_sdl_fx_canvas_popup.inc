/* The compact add menu stays in the canvas; choosing a factory patch stages
   exactly that patch once, outside audio exclusion. */
enum { FX_POPUP_W=274, FX_POPUP_ROWS=9, FX_POPUP_SUNVOX_H=424 };
static int fx_popup_catalog(int matches[512],const char *categories[32],int *category_count)
{
    int nc=1,n=0;categories[0]="ALL CATEGORIES";
    for(int i=0;i<ts_sunvox_factory_count();++i) {
        const char *category=ts_sunvox_factory_category(i);int j=1;
        for(;j<nc && strcmp(categories[j],category);++j);
        if(j==nc && nc<32)categories[nc++]=category;
    }
    fx_canvas.popup_category=SDL_clamp(fx_canvas.popup_category,0,nc-1);
    for(int i=0;i<ts_sunvox_factory_count() && n<512;++i)
        if(!fx_canvas.popup_category || !strcmp(ts_sunvox_factory_category(i),categories[fx_canvas.popup_category]))matches[n++]=i;
    fx_canvas.popup_scroll=SDL_clamp(fx_canvas.popup_scroll,0,SDL_max(0,n-FX_POPUP_ROWS));
    fx_canvas.popup_selected=SDL_clamp(fx_canvas.popup_selected,0,SDL_max(0,n-1));
    *category_count=nc;return n;
}
static void fx_popup_open(int x,int y)
{
    fx_canvas.popup=1;fx_canvas.popup_category=fx_canvas.popup_scroll=fx_canvas.popup_selected=0;
    fx_canvas.popup_x=SDL_clamp(x,24,fx_right()-FX_POPUP_W-4);
    fx_canvas.popup_y=SDL_clamp(y,FX_TOP+4,FX_BOTTOM-FX_POPUP_SUNVOX_H);
    TsMatrixLayout *l=matrix_window.layout;
    fx_canvas.place_x=(x-20)/l->canvas_zoom-l->canvas_x;
    fx_canvas.place_y=(y-FX_TOP)/l->canvas_zoom-l->canvas_y;
    if(l->canvas_snap && !(SDL_GetModState()&KMOD_ALT)) {
        fx_canvas.place_x=fx_snap(fx_canvas.place_x);fx_canvas.place_y=fx_snap(fx_canvas.place_y);
    }
    matrix_window.drag=0;
}
static void fx_popup_paint(void)
{
    int x=fx_canvas.popup_x,y=fx_canvas.popup_y,h=fx_canvas.popup==2?FX_POPUP_SUNVOX_H:40+TS_MATRIX_FX_TYPES*30;
    mx_rect(x,y,FX_POPUP_W,h,FX_CABLE);mx_rect(x+2,y+2,FX_POPUP_W-4,h-4,FX_PANEL);
    fx_panel_text(x+12,y+8,174,24,fx_canvas.popup==2?"SUNVOX FX":"ADD TO CANVAS",FX_TEXT,2,1);
    /* A separate, clipped back target keeps a real inset at every UI scale. */
    fx_panel_text(x+190,y+8,68,24,"< BACK",FX_DIM,1,1);
    if(fx_canvas.popup==1) {
        for(int type=1;type<TS_MATRIX_FX_TYPES;++type){int yy=y+32+(type-1)*30;
            mx_rect(x+9,yy+4,5,18,fx_color(type));fx_panel_text(x+24,yy+6,234,24,ts_matrix_effect_name(type),FX_TEXT,2,1);}
        fx_panel_text(x+24,y+38+(TS_MATRIX_FX_TYPES-1)*30,234,24,"TEXT LABEL",FX_FOCUS,2,1);
        return;
    }
    int list[512],nc;const char *categories[32];int n=fx_popup_catalog(list,categories,&nc);char label[96];
    fx_panel_text(x+12,y+38,250,24,categories[fx_canvas.popup_category],FX_CABLE,1,1);
    for(int row=0;row<FX_POPUP_ROWS && row+fx_canvas.popup_scroll<n;++row) {
        int at=row+fx_canvas.popup_scroll,yy=y+64+row*36;
        if(at==fx_canvas.popup_selected)mx_rect(x+6,yy,262,35,FX_BG);
        fx_panel_text(x+12,yy+2,250,34,ts_sunvox_factory_name(list[at]),at==fx_canvas.popup_selected?FX_FOCUS:FX_TEXT,1,2);
    }
    fx_panel_text(x+12,y+398,50,20,"PREV",FX_TEXT,1,1);
    snprintf(label,sizeof(label),"%d-%d / %d",n?fx_canvas.popup_scroll+1:0,SDL_min(n,fx_canvas.popup_scroll+FX_POPUP_ROWS),n);
    fx_panel_text(x+68,y+398,142,20,label,FX_DIM,1,1);
    fx_panel_text(x+216,y+398,44,20,"NEXT",FX_TEXT,1,1);
}
static void fx_popup_add(int type,int factory,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;uint32_t before=w->controls.next_effect_id;
    TsSunVoxControls patch;
    if(factory>=0 && !ts_sunvox_select(&patch,factory))return;
    mx_effect_add_patch(device,audio,ui,pages,bank,type,factory>=0?&patch:NULL);
    if(w->controls.next_effect_id==before)return; /* Failed staging leaves the menu and graph intact. */
    fx_canvas.popup=0;w->filter=0;mx_rows(pages,bank);fx_build(pages,bank);
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==w->effect_open) {
        TsFxCanvasPosition *p=fx_position(fx_canvas.node[i].row,1);
        if(p){p->x=fx_canvas.place_x;p->y=fx_canvas.place_y;}
        fx_clear_wires();fx_select(&fx_canvas.node[i],0);break;
    }
}
static int fx_popup_event(SDL_Event *e,int x,int y,int wheel,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    int list[512],nc;const char *categories[32];int n=fx_popup_catalog(list,categories,&nc);
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;
        if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){fx_canvas.popup=fx_canvas.popup==2?1:0;return 1;}
        if(fx_canvas.popup==2) {
            int step=key==SDLK_UP?-1:key==SDLK_DOWN?1:key==SDLK_PAGEUP?-FX_POPUP_ROWS:key==SDLK_PAGEDOWN?FX_POPUP_ROWS:0;
            fx_canvas.popup_selected=SDL_clamp(fx_canvas.popup_selected+step,0,SDL_max(0,n-1));
            if(key==SDLK_HOME)fx_canvas.popup_selected=0;
            if(key==SDLK_END)fx_canvas.popup_selected=SDL_max(0,n-1);
            if(step || key==SDLK_HOME || key==SDLK_END)
                fx_canvas.popup_scroll=SDL_clamp(fx_canvas.popup_scroll,fx_canvas.popup_selected-FX_POPUP_ROWS+1,fx_canvas.popup_selected);
            if((key==SDLK_RETURN || key==SDLK_KP_ENTER) && n)fx_popup_add(TS_MATRIX_FX_SUNVOX,list[fx_canvas.popup_selected],device,audio,ui,pages,bank);
        }
        return 1;
    }
    int px=fx_canvas.popup_x,py=fx_canvas.popup_y;
    if(fx_canvas.popup==2 && wheel) {
        fx_canvas.popup_scroll=SDL_clamp(fx_canvas.popup_scroll-wheel*3,0,SDL_max(0,n-FX_POPUP_ROWS));return 1;
    }
    if(e->type!=SDL_MOUSEBUTTONDOWN)return 1;
    if(e->button.button!=SDL_BUTTON_LEFT || x<px || x>=px+FX_POPUP_W || y<py) {fx_canvas.popup=0;return 1;}
    if(y<py+32){if(x>=px+184)fx_canvas.popup=fx_canvas.popup==2?1:0;return 1;}
    if(fx_canvas.popup==1) {
        int type=1+(y-py-32)/30;
        if(type==TS_MATRIX_FX_TYPES){fx_label_begin(-1);return 1;}
        if(type>TS_MATRIX_FX_TYPES){fx_canvas.popup=0;return 1;}
        if(type==TS_MATRIX_FX_SUNVOX)fx_canvas.popup=2;
        else fx_popup_add(type,-1,device,audio,ui,pages,bank);
    } else if(y<py+64) {
        fx_canvas.popup_category=(fx_canvas.popup_category+1)%nc;fx_canvas.popup_scroll=fx_canvas.popup_selected=0;
    } else if(y<py+388) {
        int at=fx_canvas.popup_scroll+(y-py-64)/36;
        if(at<n)fx_popup_add(TS_MATRIX_FX_SUNVOX,list[at],device,audio,ui,pages,bank);
    } else if(y<py+FX_POPUP_SUNVOX_H) {
        if(x<px+66)fx_canvas.popup_scroll-=FX_POPUP_ROWS;
        else if(x>=px+210)fx_canvas.popup_scroll+=FX_POPUP_ROWS;
    } else fx_canvas.popup=0;
    return 1;
}
