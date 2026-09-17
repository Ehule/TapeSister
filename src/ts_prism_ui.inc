/* Crisp native-pixel optics. Curves are sampled densely, but each stroke
   writes solid pixels rather than a low-resolution antialiasing fringe. */
static void prism_line(TsFramebuffer *fb,float x0,float y0,float x1,float y1,uint32_t color)
{
    ui_line(fb,(int)lrintf(x0),(int)lrintf(y0),(int)lrintf(x1),(int)lrintf(y1),color);
}

static void prism_curve(TsFramebuffer *fb,float x0,float y0,float x1,float y1,
                         float x2,float y2,float x3,float y3,uint32_t color)
{
    float px=x0,py=y0;
    int steps=(int)ceilf((fabsf(x1-x0)+fabsf(y1-y0)+fabsf(x2-x1)+fabsf(y2-y1)+
                         fabsf(x3-x2)+fabsf(y3-y2))/2);
    if(steps<8)steps=8;
    if(steps>256)steps=256;
    for(int i=1;i<=steps;++i) {
        float t=(float)i/steps,u=1-t;
        float x=u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3;
        float y=u*u*u*y0+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y3;
        prism_line(fb,px,py,x,y,color);px=x;py=y;
    }
}

static void prism_profile(int shape,float t,float *left,float *right)
{
    float bulge=1-t*t;
    switch(shape) {
    case TS_PRISM_PLANO_CONVEX: *left=-3;*right=3+15*bulge;break;
    case TS_PRISM_MENISCUS_POSITIVE: *left=-11+11*bulge;*right=-4+20*bulge;break;
    case TS_PRISM_BICONCAVE: *left=-17+12*bulge;*right=17-12*bulge;break;
    case TS_PRISM_PLANO_CONCAVE: *left=-17+12*bulge;*right=4;break;
    case TS_PRISM_MENISCUS_NEGATIVE: *left=-17+20*bulge;*right=-4+11*bulge;break;
    default: *left=-3-14*bulge;*right=3+14*bulge;break;
    }
}

static void prism_lens_outline(TsFramebuffer *fb,int x,int shape,uint32_t color)
{
    /* Fill first so the next row cannot erase a previously drawn edge. */
    for(int j=-56;j<=56;++j) {
        float l,r;prism_profile(shape,j/56.f,&l,&r);
        int left=(int)lrintf(x+l),right=(int)lrintf(x+r);
        rect(fb,left,167+j,right-left+1,1,RGB(18,27,31));
    }
    float pl,pr;prism_profile(shape,-1,&pl,&pr);
    prism_line(fb,x+pl,111,x+pr,111,color);
    for(int j=-56;j<56;++j) {
        float l,r;prism_profile(shape,(j+1)/56.f,&l,&r);
        prism_line(fb,x+pl,167+j,x+l,168+j,color);
        prism_line(fb,x+pr,167+j,x+r,168+j,color);pl=l;pr=r;
    }
    prism_line(fb,x+pl,223,x+pr,223,color);
}

static void prism_value(TsFramebuffer *fb,int x,int y,int w,const char *label,float amount,uint32_t color)
{
    sister_percent_parameter_state(fb,x,y,w,"",amount,100,color,0);
    rect(fb,x+2,y+2,w-4,11,RGB(24,23,25));text(fb,x+3,y+3,label,color,1);
}

static void prism_capture_button(TsFramebuffer *fb,const TsSisterUiModel *model,int endpoint)
{
    TsPrismCaptureVisual state=ts_sister_ui_prism_capture_visual(model,endpoint);
    static const char *const names[]={"EMPTY","READY","EDIT","CHANGED"};
    static const uint32_t inks[]={RGB(142,151,163),RGB(105,229,153),RGB(119,190,255),RGB(255,193,92)};
    static const uint32_t fills[]={RGB(27,31,37),RGB(17,53,37),RGB(23,44,73),RGB(67,43,18)};
    int x=endpoint?88:16;char label[8];
    int letter=ts_sister_ui_prism_state(model,endpoint);
    int browsing=letter!=ts_sister_ui_prism_assigned(model,endpoint);
    rect(fb,x,280,66,22,browsing?inks[3]:inks[state]);rect(fb,x+1,281,64,20,browsing?fills[3]:fills[state]);
    snprintf(label,sizeof(label),"CAP %c",ts_prism_state_letter(letter));
    text(fb,x+18,283,label,inks[state],1);
    const char *status=browsing?"CHOOSE":names[state];
    text(fb,x+(66-(int)strlen(status)*6)/2,293,status,browsing?inks[3]:inks[state],1);
}

#include "ts_prism_zoya.inc"
#include "ts_prism_matrix_ui.inc"

static void sister_prism_render(TsFramebuffer *fb,const TsSisterUiModel *model)
{
    if(model->prism_panel==3){sister_prism_matrix_render(fb,model);return;}
    TsPrismControls shown=model->parameters.prism;
    const TsPrismControls *p=&shown;
    const TsPrismPatch *left=ts_prism_endpoint_patch(p,0),*right=ts_prism_endpoint_patch(p,1);
    if(p->morph_enabled && ts_prism_pair_ready(p)) {
        float t=model->routing.prism.valid ? model->routing.prism.morph : p->morph;
        shown.lenses=left->lenses>right->lenses ? left->lenses : right->lenses;
#define BLEND(field) shown.field=left->field+(right->field-left->field)*t
        BLEND(spread);BLEND(drift);BLEND(focus);BLEND(stereo);BLEND(body);BLEND(mix);
        BLEND(output_db);BLEND(dry_level);BLEND(color);BLEND(drift_rate);
#undef BLEND
        shown.input_shape=t<.5f ? left->input_shape : right->input_shape;
        shown.output_shape=t<.5f ? left->output_shape : right->output_shape;
    }
    TsPrismView v=model->routing.prism.valid && !model->prism_matrix_edit?model->routing.prism:ts_prism_control_view(p);
    if(v.matrix_active) {shown.lenses=v.matrix_lenses;shown.input_shape=v.matrix_input_shape;shown.output_shape=v.matrix_output_shape;}
    const uint32_t background=RGB(7,11,15),ink=RGB(140,179,192),original=RGB(229,232,212);
    const uint32_t colors[12]={RGB(236,224,159),RGB(241,148,108),RGB(239,193,101),
        RGB(195,215,120),RGB(126,215,171),RGB(92,207,212),RGB(106,168,238),
        RGB(159,146,229),RGB(207,139,206),RGB(205,177,122),RGB(187,153,110),RGB(227,188,124)};
    char label[96];
    rect(fb,10,40,620,232,background);
    button(fb,16,48,96,p->enabled?"PRISM ON":"PRISM OFF",p->enabled);
    snprintf(label,sizeof(label),"%c/%c MORPH",ts_prism_state_letter(p->endpoint[0]),ts_prism_state_letter(p->endpoint[1]));
    if(v.matrix_active)snprintf(label,sizeof(label),"%c/%c MATRIX",ts_prism_state_letter(v.matrix_from),ts_prism_state_letter(v.matrix_to));
    int factory=ts_prism_factory_find(p);
    char factory_label[32];
    snprintf(factory_label,sizeof(factory_label),"%s%s",factory>=0?ts_prism_factory_name(factory):ts_prism_mode_name(p->mode),factory>=0?"":" *");
    button(fb,122,48,116,(p->morph_enabled || v.matrix_active)?label:factory_label,1);
    if(p->morph_enabled)snprintf(label,sizeof(label),"%c%02d > %c%02d  %03d%%",ts_prism_state_letter(p->endpoint[0]),left->lenses,ts_prism_state_letter(p->endpoint[1]),right->lenses,(int)lrintf(100*(v.valid?v.morph:p->morph)));
    else snprintf(label,sizeof(label),"%d SHIFTED + 01 BODY",p->lenses-1);
    if(v.matrix_active)snprintf(label,sizeof(label),"STEP %02d / %03d%%",v.matrix_step+1,(int)lrintf(100*v.matrix_morph));
    text(fb,250,55,label,ink,1);
    button(fb,534,48,82,"MATRIX >",model->routing.prism.matrix_running);
    for(int i=0;i<TS_PRISM_LENSES;++i) {
        int x=TS_PRISM_STRIP_X+i*TS_PRISM_STRIP_STEP;
        uint32_t color=colors[i%12];
        if(i>=p->lenses || v.lens[i].level<.001f)color=blend_color(background,color,28);
        if(model->prism_selected==i+1)rect(fb,x,TS_PRISM_STRIP_Y,TS_PRISM_STRIP_W,TS_PRISM_STRIP_H,RGB(55,18,64));
        snprintf(label,sizeof(label),"%02d",i+1);text(fb,x+5,TS_PRISM_STRIP_Y+2,label,color,1);
        for(int j=0;j<p->seq_count;++j)if(p->sequence[j]==i) {
            rect(fb,x+3,TS_PRISM_STRIP_Y+TS_PRISM_STRIP_H-1,16,1,
                 v.seq_lens==i+1 ? RGB(255,255,230) : RGB(221,67,207));
        }
    }
    text(fb,28,85,"SOURCE",ink,1);text(fb,118,85,"INPUT",ink,1);
    text(fb,290,85,"ALTER",ink,1);text(fb,440,85,"OUTPUT",ink,1);text(fb,574,85,"OUT",ink,1);
    prism_zoya_figures(fb,model,p,&v);
    prism_lens_outline(fb,148,p->input_shape,RGB(114,181,203));
    prism_lens_outline(fb,462,p->output_shape,RGB(114,181,203));
    int shape[2]={p->input_shape,p->output_shape};
    for(int stage=0;stage<2;++stage) {
        int cx=stage?462:148;
        const char *name=ts_prism_shape_name(shape[stage]);
        text(fb,cx-(int)strlen(name)*3,229,name,ink,1);
        const char *tone=ts_prism_shape_color_name(shape[stage]);
        text(fb,cx-(int)strlen(tone)*3,99,tone,blend_color(background,ink,80),1);
    }
    /* A separate original lane makes Dry Level distinct from the wet anchor. */
    uint32_t dry=blend_color(background,original,(int)lrintf(8+92*fminf(1,v.dry)));
    prism_curve(fb,65,167,82,167,73,242,100,242,dry);
    prism_line(fb,100,242,566,242,dry);
    prism_curve(fb,566,242,592,242,582,167,602,167,dry);
    rect(fb,266,237,86,11,background);
    snprintf(label,sizeof(label),"DRY %03d",(int)lrintf(v.dry*100));text(fb,273,239,label,dry,1);
    prism_line(fb,30,167,148,167,original);
    prism_line(fb,90,163,98,167,original);prism_line(fb,90,171,98,167,original);
    for(int i=TS_PRISM_LENSES-1;i>=0;--i) {
        TsPrismLensView ray=v.lens[i];if(i>=p->lenses && ray.level<=.001f)continue;
        float x,y;ts_sister_ui_prism_point_f(ray,&x,&y);
        float strength=ray.level/(1+ray.level);
        uint32_t color=blend_color(background,colors[i%12],(int)lrintf(8+strength*(12+80*v.wet)));
        float arrive_y=167+(y-167)*.7f;
        float exit_y=167+(ts_sister_ui_prism_y(ray.cents)-167)*.7f;
        float join_x=548+16*ray.pan;
        prism_curve(fb,148,167,180,167,x-34,y,x,y,color);
        prism_curve(fb,x,y,x+34,y,420,arrive_y,444,arrive_y,color);
        prism_line(fb,444,arrive_y,480,exit_y,color);
        prism_curve(fb,480,exit_y,501,exit_y,join_x-14,167,join_x,167,color);
        prism_line(fb,join_x,167,602,167,color);
        if(i==0) {
            rect(fb,(int)lrintf(x)-2,(int)lrintf(y)-2,5,5,color);
            if(model->prism_selected!=1)text(fb,(int)x-20,(int)y+8,"01 BODY",color,1);
        } else {
            int px=(int)lrintf(x),py=(int)lrintf(y);
            rect(fb,px-3,py-3,7,7,color);
            rect(fb,px-1,py-1,3,3,background);
        }
        if((p->mute_mask|p->solo_mask)&(1<<i))text(fb,(int)x-3,(int)y+6,(p->mute_mask&(1<<i))?"M":"S",ink,1);
    }
    /* Keep the selected handle and readout visible when other rays coincide. */
    int selected=model->prism_selected-1;
    if(selected>=0 && selected<p->lenses) {
        int x,y;ts_sister_ui_prism_point(v.lens[selected],&x,&y);
        uint32_t highlight=RGB(240,240,220);
        if(selected==0) {
            rect(fb,x-2,y-2,5,5,highlight);
            snprintf(label,sizeof(label),"01 BODY %+.0FDB",p->trim_db[0]);
        } else {
            rect(fb,x-3,y-3,7,7,highlight);rect(fb,x-1,y-1,3,3,background);
            snprintf(label,sizeof(label),"%02d %+.0FDB",selected+1,p->trim_db[selected]);
        }
        text(fb,x+7,y-3,label,highlight,1);
    }
    prism_line(fb,602,167,609,167,original);
    prism_line(fb,601,163,609,167,original);prism_line(fb,601,171,609,167,original);
    button(fb,16,250,78,"SOUND",model->prism_panel==0);
    button(fb,102,250,92,"PERFORM",model->prism_panel==1);
    button(fb,202,250,86,"PRESETS",model->prism_panel==2);
    const char *edit_status=ts_sister_ui_prism_edit_status(model);
    uint32_t state_color=p->morph_enabled ? RGB(205,169,255) :
        strstr(edit_status,"NOT CAPTURED") ? RGB(255,193,92) :
        model->prism_edit_endpoint ? RGB(119,190,255) : RGB(105,229,153);
    text(fb,300,257,edit_status,state_color,1);
    if(model->prism_matrix_edit) {
        snprintf(label,sizeof(label),"SAVE %c",'A'+model->prism_matrix_edit-1);button(fb,530,250,86,label,1);
    }
    if(model->prism_panel==1) {
        prism_capture_button(fb,model,0);
        prism_capture_button(fb,model,1);
        for(int side=0;side<2;++side) {
            snprintf(label,sizeof(label),"TO %c",ts_prism_state_letter(ts_sister_ui_prism_state(model,side)));
            button(fb,160+72*side,280,66,label,p->morph_enabled && p->morph_target==side);
        }
        float morph=v.matrix_active?v.matrix_morph:v.valid && p->morph_enabled ? v.morph : p->morph;
        snprintf(label,sizeof(label),"%c/%c",ts_prism_state_letter(ts_sister_ui_prism_assigned(model,0)),ts_prism_state_letter(ts_sister_ui_prism_assigned(model,1)));
        sister_percent_parameter_state(fb,308,280,154,label,morph,100,colors[5],0);
        snprintf(label,sizeof(label),"TIME %.2FS",p->morph_seconds);
        prism_value(fb,472,280,144,label,logf(p->morph_seconds/.05f)/logf(2400),colors[6]);
        button(fb,16,308,66,p->seq_enabled?"SEQ ON":"SEQ OFF",p->seq_enabled);
        button(fb,88,308,66,"EDIT",model->prism_seq_edit);
        button(fb,160,308,66,"RESET",0);
        button(fb,232,308,66,"CLEAR",0);
        snprintf(label,sizeof(label),"STEP %.2FHZ",p->seq_rate);
        prism_value(fb,308,308,154,label,logf(p->seq_rate/.05f)/logf(640),colors[2]);
        snprintf(label,sizeof(label),"SNAP %s",ts_prism_snap_name(p->snap));
        button(fb,472,308,144,label,p->snap!=0);
        snprintf(label,sizeof(label),"RATE %.3FHZ",p->drift_rate);
        prism_value(fb,16,332,144,label,logf(p->drift_rate/.005f)/logf(8000),colors[2]);
        float group=(p->morph_enabled || v.matrix_active)?v.group_octave:(float)p->group_octave;
        snprintf(label,sizeof(label),"GROUP OCT %+.2F",(double)group);
        prism_value(fb,170,332,144,label,(group+3)/6.f,colors[4]);
        text(fb,326,336,model->prism_seq_edit ? "SEQ EDIT: CTRL-CLICK NUMBERS" : "WHEEL CAP/TO: A-L / CLICK CONFIRMS",RGB(119,190,255),1);
    } else if(model->prism_panel==2) {
        button(fb,16,280,30,"<",0);
        button(fb,52,280,218,model->prism_preset_count?model->prism_preset_name:"PRISM BANK EMPTY",0);
        button(fb,276,280,30,">",0);
        button(fb,314,280,90,"SAVE NEW",0);
        button(fb,412,280,96,"NEW",0);
        button(fb,516,280,100,"VARY",0);
        static const char *const locks[]={"PITCH","MOTION","GLASS","MIX","COUNT","SEQ"};
        for(int i=0;i<6;++i)button(fb,16+i*101,308,95,locks[i],(p->locks&(1u<<i))!=0);
        text(fb,16,338,"LOCK GROUPS ABOVE / SHIFT SAVE: OVERWRITE",ink,1);
    } else {
    static const char *const names[8] = {
        "LENSES", "SPREAD", "DRIFT", "FOCUS", "STEREO", "BODY", "WET", "OUTPUT"
    };
    float amounts[8] = {(p->lenses - 2) / (float)(TS_PRISM_LENSES - 2), p->spread / 2, p->drift / 2, p->focus,
                        p->stereo, p->body / 3, p->mix, (p->output_db + 12) / 24};
    for (int i = 0; i < 8; ++i) {
        int x = 16 + (i % 4) * 154, y = 280 + (i / 4) * 28;
        int locked = p->morph_enabled || ts_sister_ui_parameter_locked(model, TS_SISTER_UI_PARAM_PRISM_LENSES + i);
        sister_percent_parameter_state(fb, x, y, 144, names[i], amounts[i],
                                       i==1 || i==2 ? 200 : i==5 ? 300 : 100, colors[i], locked);
        if (i == 0 || i == 7) {
            rect(fb, x + 2, y + 2, 140, 11, RGB(24,23,25));
            if (i == 0) snprintf(label, sizeof(label), "LENSES %02d", p->lenses);
            else snprintf(label, sizeof(label), "OUTPUT %+.1F DB", p->output_db);
            text(fb, x + 3, y + 3, label, locked ? sister_dim_color(colors[i]) : colors[i], 1);
        }
    }
    sister_percent_parameter_state(fb,16,332,144,"DRY LEVEL",p->dry_level/2,200,colors[0],
        p->morph_enabled || ts_sister_ui_parameter_locked(model,TS_SISTER_UI_PARAM_PRISM_DRY));
    sister_percent_parameter_state(fb,170,332,144,"COLOR",p->color,100,colors[5],
        p->morph_enabled || ts_sister_ui_parameter_locked(model,TS_SISTER_UI_PARAM_PRISM_COLOR));
    text(fb,326,338,model->routing.enabled ? "TO TAPE + FX" : "TO FX",ink,1);
    }
}
