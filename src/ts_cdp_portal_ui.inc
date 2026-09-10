/* Native 640x400 Portal page. All text is bounded to its own column. */
static void portal_caption(TsFramebuffer *fb,int x,int y,int chars,const char *s,uint32_t color)
{
    char line[108];
    if(chars>106)chars=106;
    snprintf(line,sizeof(line),"%.*s",chars,s?s:"");
    text(fb,x,y,line,color,1);
}
static void portal_mini(TsFramebuffer *fb,int x,int y,int width,const char *label,int active)
{
    mini_button(fb,x,y,width,"",active);
    portal_caption(fb,x+4,y+5,(width-8)/6,label,active?PAL_EFFECT:RGB(230,223,230));
}
static void portal_name_field(TsFramebuffer *fb,int x,int y,int width,
                              const char *name,const char *placeholder,int focus,int blink)
{
    int capacity=(width-12)/6;
    size_t length=strlen(name),first=focus && length>(size_t)capacity?length-(size_t)capacity:0;
    portal_mini(fb,x,y,width,focus || length?name+first:placeholder,focus);
    if(focus && blink)rect(fb,x+4+(int)(length-first)*6,y+5,1,9,PAL_VOLUME);
}
static void portal_wrap(TsFramebuffer *fb,int x,int y,int columns,int rows,const char *s)
{
    for(int row=0;*s && row<rows;++row) {
        char line[80];int n=0,space=-1;
        while(s[n] && n<columns) {if(s[n]==' ')space=n;line[n]=(char)toupper((unsigned char)s[n]);++n;}
        if(s[n] && space>0)n=space;
        line[n]=0;text(fb,x,y+row*11,line,RGB(210,204,210),1);
        s+=n;while(*s==' ')++s;
    }
}
static void portal_wave_render(TsFramebuffer *fb,const TsPortalUi *p,int which,int y)
{
    const TsSample *s=which?p->result:p->source;
    const TsPortalWave *w=&p->waves[which];
    uint32_t color=PAL_WAVE_LEFT; /* Keep L/R colors consistent with the main canvas. */
    char label[64];
    frame(fb,148,y,318,87,RGB(8,8,8),RGB(84,77,84));
    text(fb,154,y+6,which?"RESULT":"SOURCE",which?PAL_NOTE:PAL_EFFECT,1);
    portal_caption(fb,201,y+6,17,which?(p->chain_active?(p->chain_audition>=0?p->recipe.name:p->chain.name):p->recipe.name):p->source_name,RGB(210,204,210));
    if(s && s->frames) {
        snprintf(label,sizeof(label),"%.2FS %uHZ %s",(double)s->frames/s->sample_rate,s->sample_rate,s->channels==2?"ST":"M");
        portal_caption(fb,316,y+6,24,label,RGB(210,204,210));
    }
    const int top=y+20,height=62,mid=top+height/2;
    for(int x=154;x<460;x+=38)rect(fb,x,top,1,height,RGB(31,31,31));
    for(int yy=top;yy<top+height;yy+=15)rect(fb,154,yy,TS_PORTAL_WAVE_COLUMNS,1,RGB(31,31,31));
    if(s && s->data && w->last>w->first) {
        for(int x=0;x<TS_PORTAL_WAVE_COLUMNS;++x) {
            size_t f=ts_portal_wave_frame(w,x);
            if(w->has_selection && f>=w->selection_first && f<w->selection_last)
                rect(fb,154+x,top,1,height,PAL_BLOCK);
        }
        TsWaveformRequest request={0};request.sample=s;request.first=w->first;
        request.last=w->last;request.revision=w->revision;
        detail_sample(fb,which,154,top,TS_PORTAL_WAVE_COLUMNS,height,&request,
            TS_WAVEFORM_DISPLAY_STEREO,color,PAL_WAVE_RIGHT,color,PAL_WAVE_RIGHT,
            0,0,0,s->channels==2?1.5:2);
        for(int x=0;x<TS_PORTAL_WAVE_COLUMNS;++x) {
            for(unsigned ch=0;ch<s->channels && ch<2;++ch) {
                float lo=fmaxf(-1,fminf(1,ch?w->right_minimum[x]:w->minimum[x]));
                float hi=fmaxf(-1,fminf(1,ch?w->right_maximum[x]:w->maximum[x]));
                int center=s->channels==2?top+15+(int)ch*31:mid;
                int scale=s->channels==2?14:29;
                int a=center-(int)(hi*scale),b=center-(int)(lo*scale);
                rect(fb,154+x,a,1,b-a+1,ch?PAL_WAVE_RIGHT:color);
            }
        }
        detail_capture_slot(fb,which);
        if(s->channels==2) {
            text(fb,155,top+1,"L",color,1);text(fb,155,top+32,"R",PAL_WAVE_RIGHT,1);
        }
        if(w->playhead>=w->first && w->playhead<w->last) {
            int x=154+(int)((w->playhead-w->first)*TS_PORTAL_WAVE_COLUMNS/(w->last-w->first));
            rect(fb,x,top,1,height,PAL_VOLUME);
        }
    } else portal_caption(fb,168,mid-3,45,which?"PREVIEW TO HEAR AND SEE THE RESULT":"LOAD A MONO OR STEREO TILE",RGB(210,204,210));
}
static void portal_manager_render(TsFramebuffer *fb,const TsPortalUi *p,int blink)
{
    const TsPortalRecipe *r=p->manage_tab?&p->library.pins[p->manage_slot]:&p->library.recipes[p->manage_slot];
    char line[100];
    frame(fb,80,50,476,292,RGB(18,18,18),PAL_VOLUME);
    text(fb,94,66,"MANAGE YOUR TOOLS",PAL_VOLUME,1);
    portal_mini(fb,472,62,70,"CLOSE",0);
    portal_mini(fb,94,86,96,"SAVED",!p->manage_tab);
    portal_mini(fb,196,86,98,"PINS",p->manage_tab);
    for(int i=0;i<8;++i) {
        int slot=p->manage_scroll+i;
        const TsPortalRecipe *item=p->manage_tab?&p->library.pins[slot]:&p->library.recipes[slot];
        int selected=slot==p->manage_slot;
        if(selected)frame(fb,94,109+i*18,200,17,PAL_BLOCK,PAL_VOLUME);
        snprintf(line,sizeof(line),"%02d %.28s",slot+1,item->process_id[0]?item->name:"[EMPTY]");
        portal_caption(fb,98,114+i*18,32,line,selected?PAL_BLOCK_TEXT:RGB(210,204,210));
    }
    portal_mini(fb,94,258,96,"< PREVIOUS",0);
    portal_mini(fb,196,258,98,"NEXT >",0);
    snprintf(line,sizeof(line),"%s SLOT %02d",p->manage_tab?"PIN":"SAVED",p->manage_slot+1);
    portal_caption(fb,310,90,38,line,PAL_VOLUME);
    portal_caption(fb,310,108,38,r->process_id[0]?r->name:"EMPTY SLOT",PAL_EFFECT);
    portal_caption(fb,310,122,38,r->process_id,RGB(210,204,210));
    portal_caption(fb,310,144,38,"CURRENT WORKING RECIPE:",PAL_TUNING);
    portal_caption(fb,310,158,38,p->manage_recipe.name,PAL_EFFECT);
    portal_name_field(fb,310,178,232,p->manage_name,"TYPE A NEW NAME...",p->manage_name_focus,blink);
    portal_mini(fb,310,205,110,"RENAME",0);
    portal_mini(fb,430,205,110,"UPDATE",0);
    portal_mini(fb,310,227,110,"REPLACE",0);
    portal_mini(fb,430,227,110,"REMOVE",0);
    if(p->manage_action) {
        const char *verb=p->manage_action==TS_PORTAL_RENAME?"RENAME":p->manage_action==TS_PORTAL_UPDATE?"UPDATE":p->manage_action==TS_PORTAL_REPLACE?"REPLACE":"REMOVE";
        snprintf(line,sizeof(line),"%s %s %02d?",verb,p->manage_tab?"PIN":"RECIPE",p->manage_slot+1);
        portal_caption(fb,310,265,38,line,PAL_VOLUME);
        portal_caption(fb,310,280,38,p->manage_action==TS_PORTAL_REMOVE?"AUDIO AND OTHER SLOTS ARE KEPT":p->manage_action==TS_PORTAL_RENAME?p->manage_name:"USES CURRENT WORKING SETTINGS",RGB(210,204,210));
        portal_mini(fb,310,300,110,"CONFIRM",1);
        portal_mini(fb,430,300,110,"CANCEL",0);
    } else {
        portal_caption(fb,310,262,38,"UPDATE: SETTINGS, SAME PROCESS",PAL_TUNING);
        portal_caption(fb,310,276,38,"REPLACE: WHOLE WORKING RECIPE",PAL_TUNING);
        portal_caption(fb,94,304,72,"NAME FIELD: CLICK TO EDIT / CTRL+A CLEARS / ESC CANCELS",RGB(210,204,210));
    }
    portal_caption(fb,94,326,72,"SLOTS STAY NUMBERED; REMOVING A PIN LEAVES ITS TILE EMPTY",PAL_TUNING);
}
static void portal_render(TsFramebuffer *fb,const TsUiState *ui)
{
    const TsPortalUi *p=&ui->portal;
    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
    char line[96];
    clear(fb,PAL_DESKTOP);
    rect(fb,0,0,640,32,RGB(12,12,12));
    sister_portal_render(fb,ui);
    rect(fb,52,17,100,11,RGB(12,12,12));text(fb,54,19,"CDP PORTAL",PAL_EFFECT,1);
    button(fb,165,4,54,"MAIN",0);
    button(fb,224,4,66,"SISTER",0);
    button(fb,295,4,66,"RELOAD",0);
    button(fb,366,4,94,p->load_selection?"LOAD: SEL":"LOAD: TILE",p->load_selection);
    int recording=ui->file_record_state==TS_PERFORMANCE_FILE_RECORDING;
    button(fb,464,4,28,"REC",recording && ui->text_cursor_visible);
    button(fb,TS_UI_MASTER_LIMITER_X,5,28,"LIM",ui->master_output.limiter_enabled);
    master_output_fader(fb,ui->master_output.gain,528,5);
    master_output_meter(fb,&ui->master_output,580,8);

    frame(fb,8,40,134,314,RGB(18,18,18),RGB(84,77,84));
    text(fb,14,47,"PROCESS LIBRARY",RGB(210,204,210),1);
    const char *query=p->query;
    if(strlen(query)>18)query+=strlen(query)-18;
    portal_mini(fb,14,62,122,p->query[0] || p->search_focus?query:"SEARCH...",p->search_focus);
    if(p->search_focus && ui->text_cursor_visible)
        rect(fb,18+(int)strlen(query)*6,67,1,9,PAL_VOLUME);
    portal_mini(fb,14,83,38,"ALL",p->tab==0);
    portal_mini(fb,56,83,38,"SAVE",p->tab==1);
    portal_mini(fb,98,83,38,"PINS",p->tab==2);
    snprintf(line,sizeof(line),"%s >",p->family?ts_portal_family_name(p->family-1):"ALL FAMILIES");
    portal_mini(fb,14,105,122,line,p->family!=0);
    portal_mini(fb,14,126,122,p->channel_filter==TS_PORTAL_CHANNEL_STEREO?"STEREO >":
        p->channel_filter==TS_PORTAL_CHANNEL_MONO_ONLY?"MONO ONLY >":"ANY CHANNELS >",p->channel_filter!=TS_PORTAL_CHANNEL_ANY);
    int rows=0;
    for(int i=0;i<TS_PORTAL_LIST_ROWS;++i) {
        TsPortalRecipe r;
        int slot=ts_portal_filter_slot(p,p->scroll+i,&r);if(slot<0)break;
        int active=p->selected_tab==p->tab && p->selected_slot==slot;
        if(active)frame(fb,13,TS_PORTAL_LIST_Y+i*TS_PORTAL_LIST_ROW_H,124,16,PAL_BLOCK,PAL_VOLUME);
        portal_caption(fb,17,TS_PORTAL_LIST_Y+4+i*TS_PORTAL_LIST_ROW_H,19,r.name,active?PAL_BLOCK_TEXT:RGB(210,204,210));
        ++rows;
    }
    if(!rows)portal_caption(fb,17,TS_PORTAL_LIST_Y+7,19,"NO MATCHES",RGB(210,204,210));
    portal_mini(fb,14,334,68,"TOOLS",0);
    portal_mini(fb,86,334,50,"SAVE AS",0);

    portal_wave_render(fb,p,0,40);
    portal_wave_render(fb,p,1,132);
    portal_mini(fb,150,223,49,p->playing?"STOP":"PLAY",p->playing);
    portal_mini(fb,203,223,62,"SOURCE",!p->listen_result);
    portal_mini(fb,269,223,62,"RESULT",p->listen_result);
    portal_mini(fb,335,223,42,"LOOP",p->loop);
    portal_mini(fb,381,223,40,"FIT",0);
    portal_mini(fb,425,223,39,"A/B",0);

    frame(fb,148,245,318,82,RGB(18,18,18),RGB(84,77,84));
    text(fb,154,250,p->macro_view?(p->chain_active?"CHAIN MACROS":"PIN MACROS"):"PROCESS PARAMETERS",RGB(210,204,210),1);
    portal_mini(fb,310,247,124,p->process_selection?"PROCESS: SEL":"PROCESS: WHOLE",p->process_selection);
    text(fb,440,250,"PIN",PAL_VOLUME,1);
    unsigned total=(unsigned)ts_portal_control_count(p);
    for(int row=0;row<3;++row) {
        TsPortalControl c;if(!ts_portal_control_at(p,p->parameter_scroll+row,&c))break;
        unsigned i=(unsigned)c.index;const TsPortalProcess *control_proc=c.process;
        const TsPortalParam *param=&control_proc->parameters[i];
        const TsPortalMacro *macro=c.macro;
        int y=265+row*19;
        const char *label=p->macro_view && macro->configured?macro->name:param->label;
        if(p->macro_view && p->chain_active) {
            snprintf(line,sizeof(line),"%d %.13s",c.stage+1,label);label=line;
        }
        portal_caption(fb,154,y,16,label,RGB(210,204,210));
        double lo=param->minimum,hi=param->maximum,v=c.value;
        if(p->macro_view && macro->configured){lo=macro->minimum;hi=macro->maximum;}
        double t=hi>lo?(v-lo)/(hi-lo):0;
        if(hi>100 && lo>=0)t=log1p(fmax(0,v-lo))/log1p(hi-lo);
        if(t<0)t=0;if(t>1)t=1;
        rect(fb,253,y+2,129,5,RGB(8,8,8));rect(fb,253,y+2,(int)(t*124),5,PAL_EFFECT);
        rect(fb,253+(int)(t*124),y,4,9,PAL_WAVE_LEFT);
        ts_portal_parameter_format(control_proc,i,v,line,sizeof(line));
        if(p->number_focus==(int)i && (c.stage<0 || c.stage==p->chain_stage)) {
            const char *entry=p->number_text;size_t length=strlen(entry);if(length>6)entry+=length-6;
            frame(fb,388,y-2,49,13,PAL_BLOCK,PAL_VOLUME);portal_caption(fb,390,y,6,entry,PAL_VOLUME);
            if(ui->text_cursor_visible)rect(fb,390+(int)strlen(entry)*6,y,1,9,PAL_VOLUME);
        } else portal_caption(fb,390,y,7,line,PAL_EFFECT);
        portal_mini(fb,440,y-3,20,c.exposed?"X":"-",c.exposed);
    }
    if(!total)portal_caption(fb,156,272,48,p->macro_view?"EXACT RECIPE: ALL PARAMETERS ARE FIXED":"THIS MODE HAS NO NUMERIC PARAMETERS",PAL_EFFECT);
    unsigned last=(unsigned)p->parameter_scroll+3;if(last>total)last=total;
    if(total>3)snprintf(line,sizeof(line),"WHEEL LABELS %d-%u/%u; RIGHT LABEL: EDIT MACRO",p->parameter_scroll+1,last,total);
    else snprintf(line,sizeof(line),"RIGHT-CLICK LABEL: NAME / RANGE FOR MACRO");
    portal_caption(fb,154,317,50,line,PAL_TUNING);

    frame(fb,472,40,160,314,RGB(18,18,18),RGB(84,77,84));
    if(p->chain_active) {
        if(p->result)snprintf(line,sizeof(line),"%u/8 STAGES %s %.2F",p->chain.stage_count,ts_cdp_safety_name(p->safety),p->peak);
        else snprintf(line,sizeof(line),"CHAIN: %u/8 STAGES",p->chain.stage_count);
        portal_caption(fb,478,48,25,line,PAL_VOLUME);
        portal_mini(fb,478,63,46,p->chain_add?"PICK":"ADD",p->chain_add);
        portal_mini(fb,528,63,46,"UP",0);portal_mini(fb,578,63,48,"DOWN",0);
        for(unsigned i=0;i<p->chain.stage_count;++i) {
            const TsPortalStep *step=&p->chain.stages[i];
            snprintf(line,sizeof(line),"%u %c %.16s",i+1,(p->chain_cached&(1u<<i))?'+':' ',i==(unsigned)p->chain_stage?p->recipe.name:step->name);
            if(i==(unsigned)p->chain_stage)frame(fb,477,83+(int)i*14,124,14,PAL_BLOCK,PAL_VOLUME);
            portal_caption(fb,480,87+(int)i*14,20,line,i==(unsigned)p->chain_stage?PAL_EFFECT:RGB(210,204,210));
            frame(fb,603,84+(int)i*14,23,13,step->bypass?PAL_DESKTOP:PAL_BLOCK,RGB(84,77,84));
            portal_caption(fb,605,87+(int)i*14,3,step->bypass?"OFF":"ON",step->bypass?RGB(210,204,210):PAL_EFFECT);
        }
        int info_y=90+(int)p->chain.stage_count*14;
        if(proc && info_y<185) {
            portal_caption(fb,478,info_y,24,p->recipe.process_id,PAL_TUNING);
            portal_wrap(fb,478,info_y+13,24,(198-info_y-13)/11,proc->description);
        }
        portal_mini(fb,478,201,72,"REMOVE",0);portal_mini(fb,554,201,72,p->chain_edit?"EDIT ON":"EDIT OFF",p->chain_edit);
    } else {
    portal_caption(fb,478,48,25,proc?proc->title:"PROCESS",PAL_VOLUME);
    portal_caption(fb,478,63,25,p->recipe.process_id,PAL_EFFECT);
    if(proc)portal_wrap(fb,478,83,24,8,proc->description);
    text(fb,478,178,ts_portal_stereo_supported(&p->recipe)?"INPUT: MONO / ST":"INPUT: MONO ONLY",RGB(210,204,210),1);
    text(fb,478,190,proc && proc->changes_duration?"DURATION MAY CHANGE":proc?ts_portal_family_name(proc->family):"",PAL_TUNING,1);
    snprintf(line,sizeof(line),"%s  PEAK %.2F",ts_cdp_safety_name(p->safety),p->peak);
    portal_caption(fb,478,202,24,p->valid?line:"NO RESULT YET",p->safety==TS_CDP_SAFETY_HOT?PAL_VOLUME:PAL_NOTE);
    }
    portal_mini(fb,478,220,72,"DEFAULT",0);
    portal_mini(fb,554,220,72,p->macro_view?"FULL":"MACROS",p->macro_view);
    portal_name_field(fb,478,242,148,p->chain_active?p->chain.name:p->recipe.name,"NAME...",p->name_focus,ui->text_cursor_visible);
    portal_mini(fb,478,263,148,p->exact_pin?"PIN: EXACT RECIPE":"PIN: CHECKED MACROS",!p->exact_pin);
    portal_mini(fb,478,287,22,"<",0);
    snprintf(line,sizeof(line),"PIN SLOT %02d%s",p->pin_slot+1,p->library.pins[p->pin_slot].process_id[0]?" FULL":"");
    portal_caption(fb,505,292,16,line,RGB(210,204,210));
    portal_mini(fb,604,287,22,">",0);
    portal_mini(fb,478,308,66,p->auto_preview?"AUTO ON":"AUTO OFF",p->auto_preview);
    portal_mini(fb,550,308,32,"SUS",ui->keyboard_sustain);
    snprintf(line,sizeof(line),"C%d %d/5",ts_ui_keyboard_base_note(ui)/12-1,p->note_count);
    portal_caption(fb,586,314,6,line,p->note_count?PAL_VOLUME:RGB(210,204,210));
    button(fb,150,332,76,p->busy?"CANCEL":"PREVIEW",p->busy);
    button(fb,230,332,64,"APPLY",p->valid);
    button(fb,298,332,78,"NEW TILE",0);
    button(fb,380,332,84,"NEW+CONT",0);
    button(fb,478,332,148,p->chain_active?"PIN CHAIN TILE":"PIN PROCESS TILE",0);
    portal_mini(fb,8,360,54,p->chain_active?"SINGLE":"CHAIN",p->chain_active);
    for(int i=0;i<p->history_count;++i) {
        portal_mini(fb,64+i*107,360,103,"",i==p->history_selected);
        snprintf(line,sizeof(line),"%d %.13s",i+1,p->history_names[i]);
        portal_caption(fb,69+i*107,365,16,line,i==p->history_selected?PAL_EFFECT:RGB(210,204,210));
    }
    portal_mini(fb,501,360,125,p->chain_active?"FINAL RESULT":ts_portal_factory_find(p->recipe.process_id)?"SAVE BANK":"MAIN: CTRL+Z",0);
    if(p->manage_open)portal_manager_render(fb,p,ui->text_cursor_visible);
    if(p->macro_edit) {
        frame(fb,162,116,316,167,RGB(18,18,18),PAL_VOLUME);
        text(fb,174,126,"EDIT MACRO",PAL_EFFECT,1);
        const TsPortalParam *param=&proc->parameters[p->macro_edit-1];
        snprintf(line,sizeof(line),"%s / %s",proc->title,param->label);
        portal_caption(fb,174,141,48,line,RGB(210,204,210));
        const char *labels[]={"NAME","MINIMUM","MAXIMUM"};
        for(int i=0;i<3;++i) {
            int y=157+i*25;text(fb,178,y+5,labels[i],PAL_EFFECT,1);
            portal_mini(fb,268,y,198,"",i==p->macro_field);
            const char *entry=p->macro_text[i];if(strlen(entry)>29)entry+=strlen(entry)-29;
            text(fb,272,y+5,entry,PAL_VOLUME,1);
            if(i==p->macro_field && ui->text_cursor_visible)rect(fb,272+(int)strlen(entry)*6,y+5,1,9,PAL_VOLUME);
        }
        text(fb,174,235,"TAB FIELD  CTRL+A CLEAR  ENTER SAVE",RGB(210,204,210),1);
        button(fb,178,250,90,"SAVE",1);button(fb,277,250,90,"RESET",0);button(fb,376,250,90,"CANCEL",0);
    }
    if(p->full_action) {
        frame(fb,166,140,308,113,RGB(18,18,18),PAL_VOLUME);
        text(fb,181,155,"THIS SAMPLE PAGE IS FULL",PAL_VOLUME,1);
        text(fb,181,177,"KEEP THE RESULT ON A NEW PAGE?",PAL_EFFECT,1);
        text(fb,181,193,"SOURCE AND PREVIEW ARE KEPT IF CANCELLED",RGB(210,204,210),1);
        button(fb,192,217,118,"NEW PAGE",1);
        button(fb,330,217,118,"CANCEL",0);
    }
    rect(fb,0,382,640,18,RGB(8,8,8));
    portal_caption(fb,9,388,103,p->tooltip[0]?p->tooltip:p->message,p->tooltip[0]?PAL_TUNING:PAL_WAVE_LEFT);
}
