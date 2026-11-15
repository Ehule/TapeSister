static void gallery_point_label(char *out,size_t n,int marker,double seconds)
{
    if(marker==TS_GALLERY_POINT_END)snprintf(out,n,"END");
    else if(marker>=0)snprintf(out,n,"M%02d",marker+1);
    else if(seconds==0)snprintf(out,n,"START");
    else snprintf(out,n,"%02d:%05.2f",(int)(seconds/60),fmod(seconds,60));
}
static void gallery_sequence_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    const TsGallerySequence *s=&ui->gallery_sequence;const TsGallerySequencePlayer *p=&ui->gallery_player;
    uint32_t bg=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(18,15,23));
    uint32_t panel=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_PANEL,RGB(31,28,38));
    uint32_t edge=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_EDGE,RGB(64,54,70));
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    uint32_t dim=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DIM,RGB(160,149,172));
    uint32_t teal=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA2,RGB(129,206,200));
    uint32_t plum=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA5,RGB(196,151,195));
    uint32_t gold=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_DATA1,RGB(230,181,137));
    char label[112],in[24],out[24];rect(fb,8,8,624,384,edge);rect(fb,10,10,620,380,bg);
    mosaic_button(fb,16,16,82,"< GALLERY",0,0);text(fb,116,21,"GALLERY SEQUENCER",ink,2);
    snprintf(label,sizeof(label),"%s %02d / %02d",p->running?(p->paused?"PAUSED":"PLAYING"):"STOPPED",p->running?p->step+1:ui->gallery_sequence_selected+1,s->length);
    if(p->running && p->step<0)snprintf(label,sizeof(label),"%s / REMOVED",p->paused?"PAUSED":"PLAYING");
    text(fb,470,25,label,p->running?teal:dim,1);
    mosaic_button(fb,16,48,58,p->running&&!p->paused?"PAUSE":"PLAY",p->running&&!p->paused,0);
    mosaic_button(fb,80,48,58,"STOP",0,0);mosaic_button(fb,144,48,58,"LOOP",s->loop,0);
    snprintf(label,sizeof(label),"FROM %02d",s->first+1);mosaic_button(fb,208,48,78,label,s->loop,0);
    snprintf(label,sizeof(label),"TO %02d",s->last+1);mosaic_button(fb,292,48,78,label,s->loop,0);
    snprintf(label,sizeof(label),"%02d STEPS",s->length);text(fb,384,56,label,dim,1);
    mosaic_button(fb,468,48,34,"<",0,0);
    snprintf(label,sizeof(label),"PAGE %d/4",ui->gallery_sequence_page+1);text(fb,512,56,label,ink,1);
    mosaic_button(fb,584,48,34,">",0,0);
    for(int cell=0;cell<16;++cell) {
        int i=ui->gallery_sequence_page*16+cell,x=16+(cell%4)*152,y=84+(cell/4)*36;
        int used=i<s->length,selected=i==ui->gallery_sequence_selected,playing=p->running && p->step==i;
        int canvas=ui->gallery_step_canvas[i],valid=used && ui->gallery_step_valid[i];
        uint32_t color=!valid?gold:canvas%3==0?plum:canvas%3==1?teal:gold;
        rect(fb,x,y,146,32,playing?teal:selected?plum:edge);rect(fb,x+1,y+1,144,30,panel);
        if(!used){snprintf(label,sizeof(label),"%02d  -",i+1);text(fb,x+5,y+5,label,dim,1);continue;}
        snprintf(label,sizeof(label),"%02d %.18s",i+1,canvas>=0?ui->mosaic_gallery_names[canvas]:"MISSING CANVAS");text(fb,x+5,y+5,label,color,1);
        gallery_point_label(in,sizeof(in),s->step[i].in_marker,s->step[i].in_seconds);
        gallery_point_label(out,sizeof(out),s->step[i].out_marker,s->step[i].out_seconds);
        snprintf(label,sizeof(label),"%.8s > %.8s x%d",in,out,s->step[i].repeats);text(fb,x+5,y+17,label,valid?ink:gold,1);
        if(s->loop && i>=s->first && i<=s->last)rect(fb,x+1,y+29,144,2,plum);
        if(p->queued==i && p->running)text(fb,x+134,y+5,">",teal,1);
        if(playing)rect(fb,x+2,y+29,(int)(142*ui->gallery_step_progress),2,teal);
    }
    int selected=ui->gallery_sequence_selected;if(selected<0 || selected>=s->length)selected=0;
    const TsGalleryStep *step=&s->step[selected];int canvas=ui->gallery_step_canvas[selected];
    snprintf(label,sizeof(label),"EDIT %02d / %.47s%s",selected+1,canvas>=0?ui->mosaic_gallery_names[canvas]:"MISSING CANVAS",ui->gallery_step_valid[selected]?"":" / CHECK RANGE");
    text(fb,16,238,label,ui->gallery_step_valid[selected]?ink:gold,1);
    snprintf(label,sizeof(label),"CANVAS %02d %.19s",canvas+1,canvas>=0?ui->mosaic_gallery_names[canvas]:"MISSING");mosaic_button(fb,16,254,196,label,0,0);
    gallery_point_label(in,sizeof(in),step->in_marker,step->in_seconds);gallery_point_label(out,sizeof(out),step->out_marker,step->out_seconds);
    snprintf(label,sizeof(label),"IN  %s",ui->gallery_sequence_edit==1?ui->gallery_sequence_text:in);mosaic_button(fb,220,254,196,label,ui->gallery_sequence_edit==1,0);
    snprintf(label,sizeof(label),"OUT %s",ui->gallery_sequence_edit==2?ui->gallery_sequence_text:out);mosaic_button(fb,424,254,194,label,ui->gallery_sequence_edit==2,0);
    snprintf(label,sizeof(label),"REPEAT %03d",step->repeats);mosaic_button(fb,16,284,112,label,0,0);
    snprintf(label,sizeof(label),"FADE %.2fS",step->fade_seconds);
    if(ui->gallery_sequence_edit==3)snprintf(label,sizeof(label),"FADE %s",ui->gallery_sequence_text);
    mosaic_button(fb,136,284,144,label,ui->gallery_sequence_edit==3,0);
    const char *curves[]={"LINEAR","SMOOTH","EQUAL POWER"};
    snprintf(label,sizeof(label),"%s",curves[step->curve>=0 && step->curve<TS_GALLERY_FADE_CURVES?step->curve:0]);mosaic_button(fb,288,284,160,label,0,0);
    mosaic_button(fb,456,284,76,"QUEUE",p->queued==selected&&p->running,0);mosaic_button(fb,540,284,78,"NEXT",0,0);
    mosaic_button(fb,16,316,68,"ADD",0,0);mosaic_button(fb,92,316,68,"DUP",0,0);
    mosaic_button(fb,168,316,68,"DELETE",0,0);mosaic_button(fb,244,316,90,"< EARLIER",0,0);
    mosaic_button(fb,342,316,90,"LATER >",0,0);mosaic_button(fb,440,316,178,"LAUNCH SELECTED",0,0);
    if(p->error[0])snprintf(label,sizeof(label),"%.98s",p->error);
    else if(p->running && p->step<0)snprintf(label,sizeof(label),"REMOVED STEP / FINISHING CURRENT PASS / %s",p->paused?"PAUSED":ui->mosaic_gallery_fading?"CROSSFADING":"PLAYING");
    else if(p->running)snprintf(label,sizeof(label),"STEP %02d / PASS %d OF %d / %s / FADE %.2fS",p->step+1,p->visit,p->current.repeats,
        ui->mosaic_gallery_fading?"CROSSFADING":p->paused?"PAUSED":"PLAYING",ui->mosaic_gallery_fading?ui->mosaic_gallery_fade:p->next_fade);
    else snprintf(label,sizeof(label),"%.98s",ui->mosaic_gallery_status);
    text(fb,16,352,label,p->error[0]?gold:dim,1);
    text(fb,16,370,ui->gallery_sequence_edit?"TIME: M:SS OR SECONDS / ENTER APPLY / ESC CANCEL":
        "CLICK/WHEEL: CHOOSE / RIGHT-CLICK IN, OUT, FADE: TYPE / ESC: BACK",dim,1);
    text(fb,16,382,"FADES END AT OUT / LONG FADES FIT HALF OF EACH SECTION",dim,1);
}
