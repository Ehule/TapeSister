/* Browser/editor for the same stable Matrix instance used by FX Canvas. */
static int svfx_slot(void)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(matrix_window.controls.effect[i].id==sunvox_editor.node &&
           matrix_window.controls.effect[i].type==TS_MATRIX_FX_SUNVOX)return i;
    return -1;
}
static TsSunVoxControls *svfx_controls(void)
{
    if(!sunvox_editor.child){int at=svfx_slot();return at<0?NULL:&matrix_window.controls.effect[at].sunvox;}
    TsSisterFxSlotControls *slots=NULL;int count=0;
    if(!sunvox_editor.node){slots=matrix_window.fixed.parameters.fx.slot;count=matrix_window.fixed.parameters.fx.slot_count;}
    else for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        TsMatrixEffectControls *c=&matrix_window.controls.effect[i];
        if(c->id==sunvox_editor.node && c->type==TS_MATRIX_FX_PEDALBOARD){slots=c->board.slot;count=c->board.count;break;}
    }
    for(int i=0;i<count;++i)if(slots[i].id==sunvox_editor.child && slots[i].type==TS_SISTER_FX_SUNVOX)return &slots[i].sunvox;
    return NULL;
}
static void svfx_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!sunvox_editor.child || sunvox_editor.node){mx_commit_controls(device,audio,ui);return;}
    TsSisterParameters next=audio->sister.parameters;next.fx=matrix_window.fixed.parameters.fx;
    TsSisterSunVoxPlan plan;
    if(sister_stage_fx(device,audio,&next.fx,&plan)) {
        if(device)SDL_LockAudioDevice(device);
        ts_sister_sunvox_commit(&audio->sister.post_fx,&plan);
        ts_sister_runtime_set_parameters(&audio->sister,&next);
        ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
        if(device)SDL_UnlockAudioDevice(device);
        ts_sister_sunvox_discard(&plan);ui->project_modified=1;
    }
    matrix_window.fixed.parameters=audio->sister.parameters;
}
static void svfx_close(void)
{
    ts_browser_close(&sunvox_files);
    sunvox_editor.node=sunvox_editor.child=0;sunvox_editor.sister=NULL;sunvox_editor.focus=0;matrix_window.drag=0;
    SDL_StopTextInput();
}
static void svfx_open(uint32_t id)
{
    ts_browser_close(&sunvox_files);
    sunvox_editor.node=id;sunvox_editor.child=0;sunvox_editor.sister=NULL;sunvox_editor.scroll=0;sunvox_editor.page=0;
    sunvox_editor.focus=0;sunvox_editor.status[0]=0;matrix_window.drag=0;
    SDL_StopTextInput();
}
static void svfx_back(void)
{
    if(sunvox_editor.child)sister_board_open(sunvox_editor.node);
    svfx_close();
}
static void svfx_browse(TsUiState *ui)
{
    (void)ui; /* ts_browser_open shares the session folder with other windows. */
    if(!sunvox_files.directory[0]) {
        ts_browser_init(&sunvox_files);
    }
    ts_browser_open(&sunvox_files,TS_BROWSER_LOAD_SUNSYNTH,NULL);
    sunvox_editor.focus=0;matrix_window.drag=0;SDL_StopTextInput();
    snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Choose a file, then OPEN or double-click. Esc returns to factory patches.");
}
static void svfx_files_paint(void)
{
    TsBrowser *b=&sunvox_files;char label[280];
    mx_rect(16,64,580,648,MX_PANEL);
    mx_text(28,78,"CHOOSE .SUNSYNTH FILE",MX_GREEN,2);
    fx_panel_text(28,110,552,30,b->directory,MX_TEXT,1,2);
    mx_button(28,140,110,"UP",0);mx_button(150,140,260,"FACTORY PATCHES",0);
    mx_button(422,140,158,"OPEN",0);
    for(int row=0;row<TS_BROWSER_VISIBLE_ROWS && row+b->scroll<b->entry_count;++row) {
        int at=b->scroll+row,y=180+row*34;
        if(at==b->selected)mx_rect(24,y-3,564,31,MX_BG);
        snprintf(label,sizeof(label),"%s%s",b->entries[at].is_directory?"[DIR] ":"",b->entries[at].name);
        fx_panel_text(30,y,546,28,label,at==b->selected?MX_GREEN:MX_TEXT,2,1);
    }
    fx_panel_text(28,584,552,58,b->message,MX_DIM,1,2);
    mx_button(28,672,110,"PREV",0);mx_button(464,672,116,"NEXT",0);
    snprintf(label,sizeof(label),"%d-%d / %d",b->entry_count?b->scroll+1:0,SDL_min(b->entry_count,b->scroll+TS_BROWSER_VISIBLE_ROWS),b->entry_count);
    mx_text(164,680,label,MX_DIM,2);
}
static int svfx_contains(const char *text,const char *find)
{
    if(!*find)return 1;
    for(;*text;++text) {
        const char *a=text,*b=find;
        while(*a && *b && toupper((unsigned char)*a)==toupper((unsigned char)*b)){++a;++b;}
        if(!*b)return 1;
    }
    return 0;
}
static int svfx_catalog(int *matches,const char **categories,int *category_count)
{
    int n=0,nc=1;categories[0]="All categories";
    for(int i=0;i<ts_sunvox_factory_count();++i) {
        const char *cat=ts_sunvox_factory_category(i);int j=1;
        for(;j<nc && strcmp(categories[j],cat);++j);
        if(j==nc && nc<32)categories[nc++]=cat;
    }
    sunvox_editor.category=SDL_clamp(sunvox_editor.category,0,nc-1);
    for(int i=0;i<ts_sunvox_factory_count();++i)
        if((!sunvox_editor.category || !strcmp(ts_sunvox_factory_category(i),categories[sunvox_editor.category])) &&
           (svfx_contains(ts_sunvox_factory_name(i),sunvox_editor.filter) || svfx_contains(ts_sunvox_factory_category(i),sunvox_editor.filter)))matches[n++]=i;
    sunvox_editor.scroll=SDL_clamp(sunvox_editor.scroll,0,SDL_max(0,n-14));
    *category_count=nc;return n;
}
static float svfx_value(const TsSunVoxControls *c,int at,TsSunVoxParameter *p)
{
    if(c->value[at]>=0)return c->value[at]/32768.f;
    return p->maximum==p->minimum?0:(p->initial-p->minimum)/(float)(p->maximum-p->minimum);
}
static void svfx_paint(void)
{
    const TsSunVoxControls *c=svfx_controls();if(!c){svfx_close();return;}
    int list[235],nc;const char *categories[32];int n=svfx_catalog(list,categories,&nc);(void)nc;
    char label[240];mx_rect(0,0,MATRIX_W,MATRIX_H,MX_BG);
    mx_text(24,24,"SUNVOX / 235 FACTORY PATCHES",MX_GREEN,2);
    mx_button(1032,18,224,sunvox_editor.child?"BACK TO BOARD":"BACK TO ROUTES",0);
    mx_rect(16,64,580,648,MX_PANEL);mx_rect(608,64,656,648,MX_PANEL);
    mx_text(28,78,"SEARCH NAME / CATEGORY",MX_DIM,1);
    mx_rect(28,100,552,30,sunvox_editor.focus==1?MX_DIM:MX_BG);
    fx_panel_text(34,105,538,23,*sunvox_editor.filter?sunvox_editor.filter:"Click to search...",MX_TEXT,2,1);
    mx_button(28,140,552,categories[sunvox_editor.category],0);
    for(int row=0;row<14 && row+sunvox_editor.scroll<n;++row) {
        int i=list[row+sunvox_editor.scroll],y=180+row*34;
        TsSunVoxControls selected;ts_sunvox_select(&selected,i);
        if(c->patch==selected.patch)mx_rect(24,y-3,564,31,MX_BG);
        fx_panel_text(30,y,546,28,ts_sunvox_factory_name(i),c->patch==selected.patch?MX_GREEN:MX_TEXT,2,1);
    }
    mx_button(28,672,110,"PREV",0);mx_button(464,672,116,"NEXT",0);
    snprintf(label,sizeof(label),"%d-%d / %d",n?sunvox_editor.scroll+1:0,SDL_min(n,sunvox_editor.scroll+14),n);
    mx_text(164,680,label,MX_DIM,2);
    if(sunvox_files.mode)svfx_files_paint();
    fx_panel_text(626,78,616,62,ts_sunvox_name(c),MX_GREEN,2,2);
    snprintf(label,sizeof(label),"%s / INSTANCE #%u",ts_sunvox_category(c),(unsigned)(sunvox_editor.child?sunvox_editor.child:sunvox_editor.node));
    fx_panel_text(626,137,366,28,label,MX_DIM,1,1);
    snprintf(label,sizeof(label),"BLOCK %u",ts_sunvox_quantum(c));
    mx_button(1010,137,228,label,0);
    int count=ts_sunvox_parameter_count(c),last=SDL_max(0,(count-1)/10);
    sunvox_editor.page=SDL_clamp(sunvox_editor.page,0,last);
    for(int row=0;row<10;++row) {
        int at=sunvox_editor.page*10+row;TsSunVoxParameter p;
        if(!ts_sunvox_parameter(c,at,&p))break;
        int y=180+row*48;float value=svfx_value(c,at,&p);
        int raw=p.minimum+(int)lroundf(value*(p.maximum-p.minimum));
        if(p.discrete && strchr(p.unit,';')) {
            const char *option=p.unit;int at=raw-p.minimum;
            while(at-->0 && strchr(option,';'))option=strchr(option,';')+1;
            const char *end=strchr(option,';');
            snprintf(label,sizeof(label),"%s: %.*s",p.name,(int)(end?end-option:strlen(option)),option);
        } else snprintf(label,sizeof(label),"%s: %d %s",p.name,raw+p.offset,p.unit);
        fx_panel_text(626,y,616,26,label,MX_TEXT,2,1);
        mx_rect(626,y+31,612,4,MX_DIM);mx_rect(626+(int)(608*value),y+27,4,12,MX_GREEN);
    }
    mx_button(626,672,110,"PREV",0);mx_button(1122,672,116,"NEXT",0);
    snprintf(label,sizeof(label),"CONTROLS %d/%d",sunvox_editor.page+1,last+1);mx_text(756,680,label,MX_DIM,2);
    mx_text(28,724,"CUSTOM .SUNSYNTH: BROWSE, PASTE PATH OR DROP FILE HERE",MX_DIM,1);
    mx_rect(28,747,796,30,sunvox_editor.focus==2?MX_DIM:MX_PANEL);
    const char *path=sunvox_editor.path;size_t len=strlen(path);
    fx_panel_text(34,752,784,25,len>88?path+len-88:path,MX_TEXT,2,1);
    mx_button(836,747,150,"BROWSE",0);mx_button(998,747,240,"LOAD PATCH",0);
    mx_button(28,792,176,c->held?"NOTE ON":"NOTE OFF",c->held);
    mx_button(214,792,44,"-",0);mx_button(352,792,44,"+",0);
    snprintf(label,sizeof(label),"NOTE %d",c->note);mx_text(272,800,label,MX_TEXT,1);
    fx_panel_text(416,791,822,43,*sunvox_editor.status?sunvox_editor.status:
        "Drag / Shift+wheel: adjust. Right-click: reset. Note utilities need a synth inside the patch.",MX_DIM,1,2);
}
static void svfx_load(int factory,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    TsSunVoxControls *c=svfx_controls();if(!c || !mx_editable())return;
    TsSunVoxControls patch;
    if(factory<0 && strspn(sunvox_editor.path," \t\r\n")==strlen(sunvox_editor.path)){svfx_browse(ui);return;}
    int ok=factory>=0?ts_sunvox_select(&patch,factory):ts_sunvox_import_ex(&patch,sunvox_editor.path,sunvox_editor.status,sizeof(sunvox_editor.status));
    if(!ok)return;
    *c=patch;
    svfx_commit(device,audio,ui);mx_rows(pages,bank);sunvox_editor.page=0;
    c=svfx_controls();int loaded=c && c->patch==patch.patch;
    if(loaded)ts_browser_close(&sunvox_files);
    snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"%s",loaded?
        "Patch loaded. Existing routes retained.":"Patch could not be staged; previous effect retained.");
}
static void svfx_file_selected(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    TsBrowser *b=&sunvox_files;
    if(b->selected<0 || b->selected>=b->entry_count)return;
    if(b->entries[b->selected].is_directory){ts_browser_enter_selected_directory(b);return;}
    char selected[TS_BROWSER_PATH_MAX];
    if(!ts_browser_selected_path(b,selected,sizeof(selected))) {
        snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Selected patch path is too long.");return;
    }
#ifdef _WIN32
    /* The shared browser uses Win32 ANSI paths; SDL drops/pastes use UTF-8. */
    wchar_t wide[TS_BROWSER_PATH_MAX];
    if(!MultiByteToWideChar(CP_ACP,0,selected,-1,wide,TS_BROWSER_PATH_MAX) ||
       !WideCharToMultiByte(CP_UTF8,0,wide,-1,sunvox_editor.path,sizeof(sunvox_editor.path),NULL,NULL)) {
        snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Cannot convert the selected patch path. Try dropping the file here.");return;
    }
#else
    snprintf(sunvox_editor.path,sizeof(sunvox_editor.path),"%s",selected);
#endif
    svfx_load(-1,device,audio,ui,pages,bank);
}
static int svfx_event(SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    TsSunVoxControls *c=svfx_controls();if(!c){svfx_back();return 1;}
    MatrixWindow *w=&matrix_window;
    char *field=sunvox_editor.focus==1?sunvox_editor.filter:sunvox_editor.path;
    size_t size=sunvox_editor.focus==1?sizeof(sunvox_editor.filter):sizeof(sunvox_editor.path);
    if(e->type==SDL_DROPFILE) {
        if(strlen(e->drop.file)>=sizeof(sunvox_editor.path)) {
            snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Dropped patch path is too long.");
            SDL_free(e->drop.file);e->drop.file=NULL;return 1;
        }
        snprintf(sunvox_editor.path,sizeof(sunvox_editor.path),"%s",e->drop.file);
        SDL_free(e->drop.file);e->drop.file=NULL;svfx_load(-1,device,audio,ui,pages,bank);return 1;
    }
    if(e->type==SDL_TEXTINPUT && sunvox_editor.focus) {
        size_t len=strlen(field);snprintf(field+len,size-len,"%s",e->text.text);sunvox_editor.scroll=0;return 1;
    }
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;
        if(sunvox_editor.sister && key==SDLK_m && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.mod&KMOD_SHIFT)) {
            midi_learn_set_active(ui,sunvox_editor.sister,!ui->midi_learn_active);return 1;
        }
        if(key==SDLK_ESCAPE){if(sunvox_files.mode)ts_browser_close(&sunvox_files);else svfx_back();return 1;}
        if(sunvox_files.mode && !sunvox_editor.focus) {
            if(key==SDLK_RETURN)svfx_file_selected(device,audio,ui,pages,bank);
            else if(key==SDLK_BACKSPACE)ts_browser_parent(&sunvox_files);
            else if(key==SDLK_UP || key==SDLK_DOWN)ts_browser_move_selection(&sunvox_files,key==SDLK_UP?-1:1);
            else if(key==SDLK_PAGEUP || key==SDLK_PAGEDOWN)ts_browser_move_selection(&sunvox_files,key==SDLK_PAGEUP?-TS_BROWSER_VISIBLE_ROWS:TS_BROWSER_VISIBLE_ROWS);
            return 1;
        }
        if(sunvox_editor.focus) {
            if((e->key.keysym.mod&KMOD_CTRL) && key==SDLK_v) {
                char *clip=SDL_GetClipboardText();if(clip){
                    if(strlen(clip)>=size)snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Pasted text is too long; previous text retained.");
                    else snprintf(field,size,"%s",clip);
                    SDL_free(clip);
                }
            } else if((e->key.keysym.mod&KMOD_CTRL) && key==SDLK_a)field[0]=0;
            else if(key==SDLK_BACKSPACE){size_t n=strlen(field);if(n){--n;while(n && (field[n]&0xc0)==0x80)--n;field[n]=0;}}
            else if(key==SDLK_RETURN){if(sunvox_editor.focus==2)svfx_load(-1,device,audio,ui,pages,bank);sunvox_editor.focus=0;SDL_StopTextInput();}
            sunvox_editor.scroll=0;
        } else if((e->key.keysym.mod&KMOD_CTRL) && (key==SDLK_z || key==SDLK_y))
            mx_undo(device,audio,ui,pages,bank,key==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT));
        return 1;
    }
    if(e->type==SDL_MOUSEBUTTONUP){w->drag=0;SDL_CaptureMouse(SDL_FALSE);return 1;}
    int rx,ry,ww,hh;
    if(e->type==SDL_MOUSEBUTTONDOWN){rx=e->button.x;ry=e->button.y;}
    else if(e->type==SDL_MOUSEMOTION){rx=e->motion.x;ry=e->motion.y;}
    else if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&rx,&ry);else return 1;
    SDL_GetWindowSize(w->window,&ww,&hh);int x=rx*MATRIX_W/SDL_max(1,ww),y=ry*MATRIX_H/SDL_max(1,hh);
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    int wheel=e->type==SDL_MOUSEWHEEL?wheel_event_detents(e,0):0;
    if(left) {
        sunvox_editor.focus=!sunvox_files.mode && y>=100 && y<130 && x>=28 && x<580?1:y>=747 && y<777 && x>=28 && x<824?2:0;
        if(sunvox_editor.focus){SDL_StartTextInput();return 1;}SDL_StopTextInput();
        if(x>=1032 && y>=18 && y<46){svfx_back();return 1;}
    }
    if(sunvox_files.mode && x<600 && y>=64 && y<712) {
        if(wheel)ts_browser_scroll(&sunvox_files,-wheel*3);
        if(left && y>=140 && y<168) {
            if(x>=28 && x<138)ts_browser_parent(&sunvox_files);
            else if(x>=150 && x<410)ts_browser_close(&sunvox_files);
            else if(x>=422 && x<580)svfx_file_selected(device,audio,ui,pages,bank);
        }
        if(left && x>=24 && x<588 && y>=180 && y<180+34*TS_BROWSER_VISIBLE_ROWS) {
            int at=sunvox_files.scroll+(y-180)/34;
            if(at<sunvox_files.entry_count){ts_browser_select(&sunvox_files,at);if(e->button.clicks>=2)svfx_file_selected(device,audio,ui,pages,bank);}
        }
        if(left && y>=672 && y<700) {
            if(x<138)ts_browser_scroll(&sunvox_files,-TS_BROWSER_VISIBLE_ROWS);
            else if(x>=464)ts_browser_scroll(&sunvox_files,TS_BROWSER_VISIBLE_ROWS);
        }
        return 1;
    }
    int list[235],nc;const char *categories[32];int n=svfx_catalog(list,categories,&nc);
    if(x<600) {
        if(wheel && y>=180 && y<706)sunvox_editor.scroll=SDL_clamp(sunvox_editor.scroll-wheel*3,0,SDL_max(0,n-14));
        if(left && y>=140 && y<168){sunvox_editor.category=(sunvox_editor.category+1)%nc;sunvox_editor.scroll=0;}
        if(left && y>=180 && y<656 && x>=24){int at=sunvox_editor.scroll+(y-180)/34;if(at<n)svfx_load(list[at],device,audio,ui,pages,bank);}
        if(left && y>=672 && y<700){if(x<138)sunvox_editor.scroll-=14;else if(x>=464)sunvox_editor.scroll+=14;}
    }
    if(left && y>=747 && y<775) {
        if(x>=836 && x<986)svfx_browse(ui);
        else if(x>=998 && x<1238)svfx_load(-1,device,audio,ui,pages,bank);
    }
    if(left && y>=672 && y<700 && x>=626){if(x<736)--sunvox_editor.page;else if(x>=1122)++sunvox_editor.page;}
    if(ui->midi_learn_active && sunvox_editor.child) {
        if(left && x>=626 && x<1238 && y>=180 && y<660 && sunvox_editor.sister) {
            int at=sunvox_editor.page*10+(y-180)/48;char target[80];
            if(at<ts_sunvox_parameter_count(c)) {
                snprintf(target,sizeof(target),"board.param.%u.%u.%d",sunvox_editor.node,sunvox_editor.child,100+at);
                midi_learn_click(ui,sunvox_editor.sister,target);
                snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"%.191s",ui->status);
            }
        }return 1;
    }
    if(!mx_editable())return 1;
    int changed=0;
    if(left && x>=1010 && x<1238 && y>=137 && y<165) {
        unsigned q=ts_sunvox_quantum(c);
        c->quantum=q==64?256:q==256?1024:64;changed=1;
        snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),
            "%u frames: %.2f ms per effect at %u Hz. 64 = low latency; 256/1024 = lower CPU. Restarts this effect.",
            ts_sunvox_quantum(c),1000.0*ts_sunvox_quantum(c)/(audio->output_rate?audio->output_rate:48000),audio->output_rate?audio->output_rate:48000);
    }
    if(wheel && (SDL_GetModState()&KMOD_SHIFT) && x>=626 && x<1238 && y>=180 && y<660) {
        int at=sunvox_editor.page*10+(y-180)/48;TsSunVoxParameter p;
        if(ts_sunvox_parameter(c,at,&p)) {
            float current=svfx_value(c,at,&p);int step=p.maximum>p.minimum?SDL_max(1,32768/(p.maximum-p.minimum)):1;
            c->value[at]=SDL_clamp((int)lroundf(current*32768)+wheel*step,0,32768);changed=1;
        }
    }
    if(click && x>=626 && x<1238 && y>=180 && y<660 && (y-180)%48<43) {
        int at=sunvox_editor.page*10+(y-180)/48;
        if(at<ts_sunvox_parameter_count(c)) {
            if(e->button.button==SDL_BUTTON_RIGHT){c->value[at]=-1;changed=1;}
            else if(left){w->drag=400+at;SDL_CaptureMouse(SDL_TRUE);}
        }
    }
    if(w->drag>=400 && w->drag<400+TS_SUNVOX_CONTROLS && (left || e->type==SDL_MOUSEMOTION)) {
        c->value[w->drag-400]=(int)lroundf(SDL_clamp((x-626)/608.f,0,1)*32768);changed=1;
    }
    if(left && y>=792 && y<820) {
        if(x>=28 && x<204){c->held=!c->held;changed=1;}
        else if(x>=214 && x<258){c->note=SDL_max(0,c->note-1);changed=1;}
        else if(x>=352 && x<396){c->note=SDL_min(119,c->note+1);changed=1;}
    }
    if(changed)svfx_commit(device,audio,ui);
    return 1;
}
