static void sister_prism_matrix_render(TsFramebuffer *fb,const TsSisterUiModel *model)
{
    const TsPrismControls *c=&model->parameters.prism;const TsPrismView *v=&model->routing.prism;
    static const uint32_t colors[12]={RGB(236,224,159),RGB(241,148,108),RGB(239,193,101),
        RGB(195,215,120),RGB(126,215,171),RGB(92,207,212),RGB(106,168,238),
        RGB(159,146,229),RGB(207,139,206),RGB(205,177,122),RGB(187,153,110),RGB(227,188,124)};
    const uint32_t cyan=RGB(92,207,212),ink=RGB(140,179,192),dim=RGB(66,76,83),amber=RGB(255,193,92);
    char label[80];rect(fb,10,40,620,306,RGB(7,11,15));
    button(fb,16,48,70,"< PRISM",0);text(fb,102,52,"MORPH MATRIX",cyan,2);
    snprintf(label,sizeof(label),v->matrix_waiting?"JOINING / STEP 01 NEXT":v->matrix_running?"RUN %02d / %02d":"STOP / %02d STEPS",
        v->matrix_running?v->matrix_step+1:c->matrix.length,c->matrix.length);
    text(fb,438,55,label,v->matrix_running?cyan:ink,1);
    button(fb,16,78,62,"PLAY",v->matrix_running);
    button(fb,86,78,90,"STOP/RESET",0);
    button(fb,184,78,62,"LOOP",c->matrix.loop);
    snprintf(label,sizeof(label),"LENGTH %02d",c->matrix.length);button(fb,254,78,86,label,0);
    char duration[24],hold[24];ts_prism_time_label(duration,sizeof(duration),c->matrix.step_seconds);
    snprintf(label,sizeof(label),"STEP %s",duration);
    prism_value(fb,348,78,124,label,ts_prism_time_normalized(c->matrix.step_seconds),colors[3]);
    ts_prism_time_label(duration,sizeof(duration),c->morph_seconds);
    snprintf(label,sizeof(label),"MORPH %s",duration);
    prism_value(fb,480,78,136,label,ts_prism_time_normalized(c->morph_seconds),colors[6]);
    ts_prism_time_label(duration,sizeof(duration),fminf(c->morph_seconds,c->matrix.step_seconds));
    ts_prism_time_label(hold,sizeof(hold),fmaxf(0,c->matrix.step_seconds-c->morph_seconds));
    snprintf(label,sizeof(label),"TRANSITION %.12s + HOLD %.12s / -: HOLD / X: MISSING - HOLD",duration,hold);
    text(fb,16,104,label,ink,1);
    for(int i=0;i<TS_PRISM_MATRIX_STEPS;++i) {
        int x=TS_PRISM_MATRIX_X+(i%8)*TS_PRISM_MATRIX_DX,y=TS_PRISM_MATRIX_Y+(i/8)*TS_PRISM_MATRIX_DY;
        int state=c->matrix.step[i],included=i<c->matrix.length;
        int missing=state>=0 && !(c->captured&(1<<state));
        int playing=v->matrix_active && !v->matrix_waiting && v->matrix_step==i;
        uint32_t color=!included?dim:state<0?ink:missing?amber:colors[state];
        rect(fb,x,y,TS_PRISM_MATRIX_W,TS_PRISM_MATRIX_H,playing?cyan:included?RGB(55,60,68):RGB(24,29,35));
        rect(fb,x+1,y+1,TS_PRISM_MATRIX_W-2,TS_PRISM_MATRIX_H-2,playing?RGB(18,53,60):RGB(18,23,29));
        snprintf(label,sizeof(label),"%02d",i+1);text(fb,x+4,y+4,label,included?ink:dim,1);
        label[0]=state<0?'-':ts_prism_state_letter(state);label[1]=0;text(fb,x+32,y+3,label,color,2);
        if(missing && included)text(fb,x+60,y+5,"X",amber,1);
        if(i==c->matrix.length-1)rect(fb,x+66,y+14,3,4,amber);
        if(playing)rect(fb,x+2,y+19,(int)((TS_PRISM_MATRIX_W-4)*v->matrix_progress),2,cyan);
    }
    for(int state=0;state<TS_PRISM_STATES;++state) {
        int x=16+state*50,ready=c->captured&(1<<state);
        rect(fb,x,318,46,22,ready?colors[state]:dim);rect(fb,x+1,319,44,20,RGB(18,23,29));
        label[0]=ts_prism_state_letter(state);label[1]=0;text(fb,x+9,323,label,ready?colors[state]:dim,1);
        text(fb,x+24,323,ready?"OK":"--",ready?ink:dim,1);
    }
}
