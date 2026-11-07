/* Browser/editor for the same stable Matrix instance used by FX Canvas. */
static int svfx_slot(void)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(matrix_window.controls.effect[i].id==sunvox_editor.node &&
           matrix_window.controls.effect[i].type==TS_MATRIX_FX_SUNVOX)return i;
    return -1;
}
static void svfx_close(void)
{
    sunvox_editor.node=0;sunvox_editor.focus=0;matrix_window.drag=0;
    SDL_StopTextInput();
}
static void svfx_open(uint32_t id)
{
    sunvox_editor.node=id;sunvox_editor.scroll=0;sunvox_editor.page=0;
    sunvox_editor.focus=0;sunvox_editor.status[0]=0;matrix_window.drag=0;
    SDL_StopTextInput();
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
    int slot=svfx_slot();if(slot<0){svfx_close();return;}
    const TsMatrixEffectControls *c=&matrix_window.controls.effect[slot];
    int list[235],nc;const char *categories[32];int n=svfx_catalog(list,categories,&nc);(void)nc;
    char label[240];mx_rect(0,0,MATRIX_W,MATRIX_H,MX_BG);
    mx_text(24,24,"SUNVOX / 235 FACTORY PATCHES",MX_GREEN,2);
    mx_button(1032,18,224,"BACK TO ROUTES",0);
    mx_rect(16,64,580,648,MX_PANEL);mx_rect(608,64,656,648,MX_PANEL);
    mx_text(28,78,"SEARCH NAME / CATEGORY",MX_DIM,1);
    mx_rect(28,100,552,30,sunvox_editor.focus==1?MX_DIM:MX_BG);
    fx_panel_text(34,105,538,23,*sunvox_editor.filter?sunvox_editor.filter:"Click to search...",MX_TEXT,2,1);
    mx_button(28,140,552,categories[sunvox_editor.category],0);
    for(int row=0;row<14 && row+sunvox_editor.scroll<n;++row) {
        int i=list[row+sunvox_editor.scroll],y=180+row*34;
        TsSunVoxControls selected;ts_sunvox_select(&selected,i);
        if(c->sunvox.patch==selected.patch)mx_rect(24,y-3,564,31,MX_BG);
        fx_panel_text(30,y,546,28,ts_sunvox_factory_name(i),c->sunvox.patch==selected.patch?MX_GREEN:MX_TEXT,2,1);
    }
    mx_button(28,672,110,"PREV",0);mx_button(464,672,116,"NEXT",0);
    snprintf(label,sizeof(label),"%d-%d / %d",n?sunvox_editor.scroll+1:0,SDL_min(n,sunvox_editor.scroll+14),n);
    mx_text(164,680,label,MX_DIM,2);
    fx_panel_text(626,78,616,62,ts_sunvox_name(&c->sunvox),MX_GREEN,2,2);
    snprintf(label,sizeof(label),"%s / INSTANCE #%u",ts_sunvox_category(&c->sunvox),(unsigned)c->id);
    fx_panel_text(626,137,616,28,label,MX_DIM,1,1);
    int count=ts_sunvox_parameter_count(&c->sunvox),last=SDL_max(0,(count-1)/10);
    sunvox_editor.page=SDL_clamp(sunvox_editor.page,0,last);
    for(int row=0;row<10;++row) {
        int at=sunvox_editor.page*10+row;TsSunVoxParameter p;
        if(!ts_sunvox_parameter(&c->sunvox,at,&p))break;
        int y=180+row*48;float value=svfx_value(&c->sunvox,at,&p);
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
    mx_text(28,724,"CUSTOM .SUNSYNTH: PASTE PATH OR DROP FILE HERE",MX_DIM,1);
    mx_rect(28,747,980,30,sunvox_editor.focus==2?MX_DIM:MX_PANEL);
    const char *path=sunvox_editor.path;size_t len=strlen(path);
    fx_panel_text(34,752,968,25,len>110?path+len-110:path,MX_TEXT,2,1);
    mx_button(1020,747,218,"LOAD PATCH",0);
    mx_button(28,792,176,c->sunvox.held?"NOTE ON":"NOTE OFF",c->sunvox.held);
    mx_button(214,792,44,"-",0);mx_button(352,792,44,"+",0);
    snprintf(label,sizeof(label),"NOTE %d",c->sunvox.note);mx_text(272,800,label,MX_TEXT,1);
    fx_panel_text(416,791,822,43,*sunvox_editor.status?sunvox_editor.status:
        "Drag / Shift+wheel: adjust. Right-click: reset. Note utilities need a synth inside the patch.",MX_DIM,1,2);
}
static void svfx_load(int factory,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    int slot=svfx_slot();if(slot<0 || !mx_editable())return;
    TsSunVoxControls patch;
    int ok=factory>=0?ts_sunvox_select(&patch,factory):ts_sunvox_import(&patch,sunvox_editor.path);
    if(!ok){snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"Cannot load this .sunsynth file (missing, invalid, or import cache full).");return;}
    matrix_window.controls.effect[slot].sunvox=patch;
    mx_commit_controls(device,audio,ui);mx_rows(pages,bank);sunvox_editor.page=0;
    snprintf(sunvox_editor.status,sizeof(sunvox_editor.status),"%s",matrix_window.controls.effect[slot].sunvox.patch==patch.patch?
        "Patch loaded. Existing routes retained.":"Patch could not be staged; previous effect retained.");
}
static int svfx_event(SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    int slot=svfx_slot();if(slot<0){svfx_close();return 1;}
    MatrixWindow *w=&matrix_window;TsSunVoxControls *c=&w->controls.effect[slot].sunvox;
    char *field=sunvox_editor.focus==1?sunvox_editor.filter:sunvox_editor.path;
    size_t size=sunvox_editor.focus==1?sizeof(sunvox_editor.filter):sizeof(sunvox_editor.path);
    if(e->type==SDL_DROPFILE) {
        snprintf(sunvox_editor.path,sizeof(sunvox_editor.path),"%s",e->drop.file);
        SDL_free(e->drop.file);e->drop.file=NULL;svfx_load(-1,device,audio,ui,pages,bank);return 1;
    }
    if(e->type==SDL_TEXTINPUT && sunvox_editor.focus) {
        size_t len=strlen(field);snprintf(field+len,size-len,"%s",e->text.text);sunvox_editor.scroll=0;return 1;
    }
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;
        if(key==SDLK_ESCAPE){svfx_close();return 1;}
        if(sunvox_editor.focus) {
            if((e->key.keysym.mod&KMOD_CTRL) && key==SDLK_v) {
                char *clip=SDL_GetClipboardText();if(clip){snprintf(field,size,"%s",clip);SDL_free(clip);}
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
        sunvox_editor.focus=y>=100 && y<130 && x>=28 && x<580?1:y>=747 && y<777 && x>=28 && x<1008?2:0;
        if(sunvox_editor.focus){SDL_StartTextInput();return 1;}SDL_StopTextInput();
        if(x>=1032 && y>=18 && y<46){svfx_close();return 1;}
    }
    int list[235],nc;const char *categories[32];int n=svfx_catalog(list,categories,&nc);
    if(x<600) {
        if(wheel && y>=180 && y<706)sunvox_editor.scroll=SDL_clamp(sunvox_editor.scroll-wheel*3,0,SDL_max(0,n-14));
        if(left && y>=140 && y<168){sunvox_editor.category=(sunvox_editor.category+1)%nc;sunvox_editor.scroll=0;}
        if(left && y>=180 && y<656 && x>=24){int at=sunvox_editor.scroll+(y-180)/34;if(at<n)svfx_load(list[at],device,audio,ui,pages,bank);}
        if(left && y>=672 && y<700){if(x<138)sunvox_editor.scroll-=14;else if(x>=464)sunvox_editor.scroll+=14;}
    }
    if(left && y>=747 && y<775 && x>=1020)svfx_load(-1,device,audio,ui,pages,bank);
    if(left && y>=672 && y<700 && x>=626){if(x<736)--sunvox_editor.page;else if(x>=1122)++sunvox_editor.page;}
    if(!mx_editable())return 1;
    int changed=0;
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
    if(changed)mx_commit_controls(device,audio,ui);
    return 1;
}
