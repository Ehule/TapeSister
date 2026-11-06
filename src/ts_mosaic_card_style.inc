/* Both raster paths choose detail from logical card size, never viewport
   clipping. Scrolling therefore translates the same pixels without reflow. */
typedef struct {
    int compact, focused, chars;
    double margin, header, footer;
} MosaicCardStyle;
static MosaicCardStyle mosaic_card_style(const TsUiState *ui,const TsMosaicEvent *e,
                                         double width,double height,int selected)
{
    MosaicCardStyle s={0};
    s.compact=height<40 || width<64;
    s.focused=selected || e->id==ui->mosaic_hover_event || e->id==ui->mosaic_card_mix_event || e->id==ui->mosaic_resize_event;
    s.margin=s.compact?3:6;
    double reserve=s.focused && height>=14 && width>=24?22:s.margin;
    s.chars=(int)fmax(0,fmin(63,floor((width-s.margin-reserve+1)/6)));
    if(height<(s.compact?10:14))s.chars=0;
    s.header=s.compact?(s.chars?11:2):24;
    s.footer=s.compact?2:14;
    return s;
}
static uint32_t mosaic_hover_color(uint32_t source)
{
    return mosaic_mix_color(source,ts_theme_color(active_palette(),TS_THEME_MOSAIC,
        TS_COLOR_TEXT,RGB(224,214,233)),.7f);
}
static void mosaic_card_name(char *out,size_t size,const char *name,int chars)
{
    snprintf(out,size,"%.*s",chars,name);
    if(chars>=4 && strlen(name)>(size_t)chars)out[chars-1]='.';
}
static void mosaic_card_info(TsFramebuffer *fb,const TsUiState *ui)
{
    if(ui->mosaic_grab || ui->mosaic_resize_event || ui->mosaic_card_mix_event || ui->mosaic_box)return;
    const TsMosaicEvent *e=ui->mosaic?ts_mosaic_find(ui->mosaic,ui->mosaic_hover_event):NULL;
    int hovering=e!=NULL;
    if(!e && !ui->mosaic_source_hover[0] && ui->mosaic)e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    if(!e)return;
    uint32_t ink=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_TEXT,RGB(224,214,233));
    /* Selection names use the spare row; keep operation warnings, navigation
       hints and Gallery/crossfade status visible when the pointer moves away. */
    uint32_t background=ts_theme_color(active_palette(),TS_THEME_MOSAIC,TS_COLOR_BACKGROUND,RGB(11,13,16));
    if(!hovering) {
        rect(fb,164,32,476,7,background);text(fb,164,32,e->name,ink,1);return;
    }
    rect(fb,164,24,476,15,background);
    char line[100];
    if(ui->mosaic_gallery_count>0)snprintf(line,sizeof(line),"C%02d / %s",ui->mosaic_gallery_active+1,e->name);
    else snprintf(line,sizeof(line),"%s",e->name);
    text(fb,164,24,line,ink,1);
    unsigned minutes=(unsigned)(e->start/60);double seconds=e->start-minutes*60;
    const char *state=e->muted?"MUTE":e->solo?"SOLO":e->source && e->source->video_path[0]?"VISUAL":e->live?"LIVE":e->looping?"LOOP":"ONCE";
    snprintf(line,sizeof(line),"CARD %llu / %02u:%06.3F / %.3FS / %s",
        (unsigned long long)e->id,minutes,seconds,e->duration,state);
    text(fb,164,32,line,ink,1);
}
