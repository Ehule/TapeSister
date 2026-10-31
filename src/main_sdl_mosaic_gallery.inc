/* Gallery owns scene navigation only. Audio sees two published scene pointers;
   allocation/destruction and UI cache disposal stay outside the output lock. */
static void mosaic_gallery_snapshot(TsUiState *ui,const TsMosaicGallery *g)
{
    ui->mosaic_gallery_count=g?g->count:0;
    if(!g)return;
    ui->mosaic_gallery_active=g->active;ui->mosaic_gallery_fading=g->outgoing>=0;
    ui->mosaic_gallery_progress=g->outgoing>=0 && g->fade_length>0?g->fade_position/g->fade_length:1;
    ui->mosaic_gallery_fade=g->fade_seconds;
    for(int i=0;i<g->count;++i) {
        snprintf(ui->mosaic_gallery_names[i],48,"%s",g->canvas[i].name);
        ui->mosaic_gallery_entry[i]=g->canvas[i].entry;ui->mosaic_gallery_marker[i]=g->canvas[i].marker;
        ui->mosaic_gallery_marker_valid[i]=g->canvas[i].marker>=0 && g->canvas[i].marker<TS_MOSAIC_MARKERS && g->canvas[i].scene->markers[g->canvas[i].marker].stored;
    }
}
static int mosaic_gallery_busy(AudioState *a,TsUiState *ui,MosaicController *c)
{
    if((a->loop_capture && atomic_load_explicit(&a->loop_capture->active,memory_order_acquire)) || ui->file_busy || ui->mosaic_recording || ui->mosaic_live_recording || ui->mosaic_live_finishing || ui->quick_capture_active ||
       c->active || c->drag || c->mix_drag || c->volume_drag || ui->mosaic_edit_choice || ui->mosaic_pattern_choice) {
        snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"FINISH THE TILE/LOOP TAKE OR EDIT FIRST / REC FILE CAN CONTINUE");return 1;
    }
    return 0;
}
static int mosaic_gallery_enter(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsInstrument *bank,MosaicController *c,int index)
{
    TsSamplePages *pages=c->pages;TsMosaicGallery *g=pages?pages->gallery:NULL;
    if(!g || index<0 || index>=g->count || mosaic_gallery_busy(a,ui,c))return 0;
    if(index==g->active)return 1;
    if(device)SDL_LockAudioDevice(device);
    int fading=g->outgoing>=0;
    if(device)SDL_UnlockAudioDevice(device);
    if(fading){snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"WAIT FOR THE CURRENT CROSSFADE");return 0;}
    TsMosaicCanvas *to=&g->canvas[index];
    if(to->entry==TS_GALLERY_MARKER && (to->marker<0 || to->marker>=TS_MOSAIC_MARKERS || !to->scene->markers[to->marker].stored)) {
        snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"CHOOSE A STORED MARKER ON THE DESTINATION CANVAS");return 0;
    }
    if(!runtime_note_preserve_workspace(device,a,ui))return 0;
    TsMosaicCanvas *from=&g->canvas[g->active];
    from->scroll=ui->mosaic_scroll;from->xscroll=ui->mosaic_xscroll;
    from->scale=ui->mosaic_scale;from->hscale=ui->mosaic_hscale;
    /* Release UI pins before changing the epoch; old PCM remains owned by its
       scene or the already sounding immutable performance generations. */
    mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);
    if(c->clipboard_epoch==ui->mosaic->epoch && c->clipboard.source && c->clipboard.source->pins)--c->clipboard.source->pins;
    for(int i=0;i<256;++i)if(c->pattern_sources[i] && c->pattern_sources[i]->pins)--c->pattern_sources[i]->pins;
    if(device)SDL_LockAudioDevice(device);
    int ok=ts_gallery_switch(g,index,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
    if(ok) {
        ui->mosaic=a->mosaic=pages->mosaic=ts_gallery_active(g);a->gallery=ui->mosaic_gallery=g;
        a->mosaic_keyboard_sample=NULL;ui->mosaic_live_key_valid=0;
        ui->mosaic_source_keys=ui->mosaic_source_key_count=0;
        mosaic_gallery_snapshot(ui,g);
    }
    if(device)SDL_UnlockAudioDevice(device);
    if(!ok)return 0;
    runtime_mosaic_sources_clear_selection(a);
    mosaic_controller_free(c);c->pages=pages;
    mosaic_select_only(ui,0);ui->mosaic_source_selected=-1;ui->mosaic_live_status[0]=0;
    ui->mosaic_scroll=to->scroll;ui->mosaic_xscroll=to->xscroll;
    ui->mosaic_scale=to->scale>0?to->scale:24;ui->mosaic_hscale=to->hscale>0?to->hscale:1;
    ui->mosaic_loaded_view_epoch=ui->mosaic->epoch;
    mosaic_sources_refresh(ui,bank,c);
    snprintf(ui->status,sizeof(ui->status),"CANVAS %02d / %.47s / %s",index+1,to->name,ui->mosaic_gallery_fading?"CROSSFADING":"ACTIVE");
    snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"%s",ui->status);
    return 1;
}
static int mosaic_gallery_event(const SDL_Event *event,SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,
    TsInstrument *bank,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    if(!ui->mosaic_open || !c->pages)return 0;
    int key=event->type==SDL_KEYDOWN,left=event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    int shortcut=key && !event->key.repeat && (mod&KMOD_CTRL) && k==SDLK_g;
    if(!ui->mosaic_gallery_open) {
        if(ui_dialog_open(ui))return 0;
        if(key && !event->key.repeat && (mod&KMOD_CTRL) && (k==SDLK_PAGEUP || k==SDLK_PAGEDOWN)) {
            TsMosaicGallery *g=c->pages->gallery;
            if(g) {
                int next=g->active+(k==SDLK_PAGEUP?-1:1);
                if(next>=0 && next<g->count)mosaic_gallery_enter(device,a,ui,bank,c,next);
                else snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"END OF GALLERY / CTRL+G TO CHOOSE A CANVAS");
                snprintf(ui->status,sizeof(ui->status),"%s",ui->mosaic_gallery_status);
            }
            return 1;
        }
        if(!(left && x>=164 && x<210 && y>=4 && y<20) && !shortcut)return 0;
        if(!ts_sample_pages_enable_gallery(c->pages,ui->status,sizeof(ui->status)))return 1;
        a->gallery=c->pages->gallery;
        ui->mosaic_gallery_open=1;ui->mosaic_gallery_selected=a->gallery->active;
        ui->mosaic_gallery_rename=ui->mosaic_gallery_delete=0;ui->mosaic_canvas_import_count=0;
        snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"SELECT A CANVAS / ENTRY SETTING / ENTER TO TURN THE PAGE");
        mosaic_gallery_snapshot(ui,a->gallery);return 1;
    }
    TsMosaicGallery *g=c->pages->gallery;if(!g){ui->mosaic_gallery_open=0;return 1;}
    if(ui->mosaic_canvas_import_count) {
        int *selected=&ui->mosaic_canvas_import_selected,count=ui->mosaic_canvas_import_count;
        if((key && k==SDLK_ESCAPE) || (left && y>=319 && y<343 && x>=494 && x<598)) {
            ui->mosaic_canvas_import_count=0;snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"CANVAS IMPORT CANCELLED");return 1;
        }
        if(key && !event->key.repeat && (k==SDLK_UP || k==SDLK_DOWN))*selected=(*selected+(k==SDLK_UP?-1:1)+count)%count;
        if(left && y>=105 && y<265 && x>=126 && x<610){int index=(x>=370)*8+(y-105)/20;if(index<count)*selected=index;}
        if((key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) || (left && y>=319 && y<343 && x>=388 && x<482)) {
            if(!mosaic_gallery_busy(a,ui,c)) {
                if(g->count>=TS_GALLERY_LIMIT || ui->mosaic_gallery_fading)snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"GALLERY FULL OR CROSSFADING / TRY AGAIN AFTER THE FADE");
                else {ui->mosaic_canvas_import_pending=1;ui->file_busy=1;snprintf(ui->file_busy_label,sizeof(ui->file_busy_label),"IMPORTING CANVAS");}
            }
        }
        return event->type==SDL_KEYDOWN || event->type==SDL_KEYUP || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL ||
            event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEMOTION;
    }
    if(key && k==SDLK_ESCAPE) {
        if(ui->mosaic_gallery_rename){ui->mosaic_gallery_rename=0;SDL_StopTextInput();}
        else ui->mosaic_gallery_open=0;
        return 1;
    }
    int selected=ui->mosaic_gallery_selected;
    if(selected<0 || selected>=g->count)selected=ui->mosaic_gallery_selected=g->active;
    if(ui->mosaic_gallery_rename) {
        size_t n=strlen(ui->mosaic_gallery_name);
        if(event->type==SDL_TEXTINPUT)for(const unsigned char *p=(const unsigned char *)event->text.text;*p && n<47;++p)
            if(*p>=32 && *p<=126){ui->mosaic_gallery_name[n++]=(char)*p;ui->mosaic_gallery_name[n]=0;}
        if(key && k==SDLK_BACKSPACE && n)ui->mosaic_gallery_name[n-1]=0;
        if(key && k==SDLK_a && (mod&KMOD_CTRL))ui->mosaic_gallery_name[0]=0;
        if(key && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) {
            if(ts_gallery_rename(g,selected,ui->mosaic_gallery_name,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status))) {
                ui->mosaic_gallery_rename=0;SDL_StopTextInput();mosaic_gallery_snapshot(ui,g);
            }
        }
        return 1;
    }
    if(left && y>=79 && y<103 && x>=430 && x<598) {
        if(mosaic_gallery_busy(a,ui,c))return 1;
        int importing=x<508;
        if(importing && g->count>=TS_GALLERY_LIMIT){snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"GALLERY LIMIT: 16 CANVASES");return 1;}
        ui->mosaic_gallery_open=0;
        browser_open(ui,importing?TS_BROWSER_IMPORT_CANVAS:TS_BROWSER_EXPORT_CANVAS);
        if(!importing) {
            char filename[64];size_t at=0;
            for(const unsigned char *p=(const unsigned char *)g->canvas[selected].name;*p && at<47;++p)
                filename[at++]=isalnum(*p) || *p==' ' || *p=='-' || *p=='_'?(char)*p:'_';
            if(!at)filename[at++]='C';memcpy(filename+at,".tsr",5);ts_browser_set_filename(&ui->browser,filename);
        }
        return 1;
    }
    if(key && !event->key.repeat && (k==SDLK_UP || k==SDLK_DOWN)) {
        selected=(selected+(k==SDLK_UP?-1:1)+g->count)%g->count;
        ui->mosaic_gallery_selected=selected;ui->mosaic_gallery_delete=0;
    }
    if(left && y>=105 && y<265 && x>=126 && x<610) {
        int col=x>=370,row=(y-105)/20;int index=col*8+row;
        if(index<g->count){ui->mosaic_gallery_selected=index;ui->mosaic_gallery_delete=0;}
    }
    if((key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) || (left && y>=319 && y<343 && x>=388 && x<482)) {
        if(mosaic_gallery_enter(device,a,ui,bank,c,ui->mosaic_gallery_selected))ui->mosaic_gallery_open=0;
        return 1;
    }
    if(left && y>=319 && y<343 && x>=494 && x<598){ui->mosaic_gallery_open=0;return 1;}
    if(left && y>=319 && y<343 && x>=128 && x<200) {
        if(mosaic_gallery_busy(a,ui,c))return 1;
        TsMosaic *made=ts_mosaic_create();if(!made){snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"NOT ENOUGH MEMORY FOR ANOTHER CANVAS");return 1;}
        if(device)SDL_LockAudioDevice(device);
        int ok=ts_gallery_append_prepared(g,made,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
        if(ok)ui->mosaic_gallery_selected=g->count-1;
        mosaic_gallery_snapshot(ui,g);
        if(device)SDL_UnlockAudioDevice(device);
        if(!ok)ts_mosaic_free(made);
    }
    if(left && y>=319 && y<343 && x>=212 && x<288) {
        ui->mosaic_gallery_rename=1;
        snprintf(ui->mosaic_gallery_name,48,"%s",g->canvas[selected].name);SDL_StartTextInput();
    }
    if(left && y>=319 && y<343 && x>=300 && x<376) {
        if(mosaic_gallery_busy(a,ui,c))return 1;
        if(!ui->mosaic_gallery_delete){ui->mosaic_gallery_delete=1;snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"CLICK DELETE AGAIN TO REMOVE THE SELECTED INACTIVE CANVAS");return 1;}
        TsMosaic *retired=NULL;
        if(device)SDL_LockAudioDevice(device);
        int ok=ts_gallery_detach(g,selected,&retired,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
        if(ok)ui->mosaic_gallery_selected=g->active;
        mosaic_gallery_snapshot(ui,g);
        if(device)SDL_UnlockAudioDevice(device);
        ts_mosaic_free(retired);ui->mosaic_gallery_delete=0;
    }
    if(left && y>=273 && y<297 && x>=128 && x<275) {
        g->canvas[selected].entry=(g->canvas[selected].entry+1)%3;mosaic_gallery_snapshot(ui,g);
    }
    if((left && y>=273 && y<297 && x>=282 && x<425) || event->type==SDL_MOUSEWHEEL) {
        int delta=event->type==SDL_MOUSEWHEEL?(event->wheel.y<0?-1:1):1;
        if(x>=282 && x<425 && y>=273 && y<297)g->canvas[selected].marker=(g->canvas[selected].marker+delta+16)%16;
        else if(x>=438 && x<598 && y>=273 && y<297)g->fade_seconds=fmax(0,fmin(30,g->fade_seconds+delta*.25));
        mosaic_gallery_snapshot(ui,g);
    }
    if(left && y>=273 && y<297 && x>=438 && x<598) {
        g->fade_seconds=fmax(0,fmin(30,g->fade_seconds+(x<518?-.25:.25)));mosaic_gallery_snapshot(ui,g);
    }
    return event->type==SDL_KEYDOWN || event->type==SDL_KEYUP || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL ||
        event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEMOTION;
}
