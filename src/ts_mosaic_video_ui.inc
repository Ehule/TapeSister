static void mosaic_video_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->mosaic_video_open)return;
    const TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_video_id);
    uint32_t ink=RGB(222,230,243),accent=RGB(133,190,239);
    rect(fb,90,76,460,266,accent);rect(fb,92,78,456,262,RGB(25,29,39));
    text(fb,106,90,"VIDEO CHANGE / RIGHT-CLICK A CARD TO EDIT",ink,1);
    mosaic_button(fb,106,110,208,"CHOOSE PARTICULAR FILE",0,1);
    mosaic_button(fb,326,110,208,"RANDOM FROM FOLDER",e && e->source->video_pool,1);
    char label[80];const char *path=e?e->source->video_path:"CHOOSE A FILE OR A FOLDER TO PLACE A TRIGGER";
    size_t n=strlen(path);if(n>70)path+=n-70;snprintf(label,sizeof(label),"%.70s",path);text(fb,106,136,label,accent,1);
    mosaic_button(fb,106,157,208,e && e->video_random_start?"START: RANDOM POSITION":"START: BEGINNING",e && e->video_random_start,1);
    mosaic_button(fb,326,157,208,e && e->video_bw?"BLACK & WHITE":"ORIGINAL COLOR",e && e->video_bw,1);
    snprintf(label,sizeof(label),"TRANSITION: %s",(const char*[]){"CUT","CROSSFADE","THROUGH BLACK"}[e?e->video_transition:0]);
    mosaic_button(fb,106,188,208,label,0,1);
    snprintf(label,sizeof(label),"FADE: %.2F SEC / WHEEL",e?e->video_fade:2);mosaic_button(fb,326,188,208,label,0,1);
    text(fb,106,220,"CLIP LOOPS UNTIL THE NEXT TRIGGER / VIDEO SOUND OFF",ink,1);
    text(fb,106,234,"SELECT + CTRL+R: SCATTER / NEWEST WINS AT SAME TIME",ink,1);
    mosaic_button(fb,106,259,208,"VISUALIZER / MANDALA SWITCH",0,1);
    mosaic_button(fb,326,259,208,"PREVIEW THIS TRIGGER",0,1);
    mosaic_button(fb,106,308,120,"DELETE",0,1);mosaic_button(fb,414,308,120,"DONE",0,1);
    text(fb,106,288,"KEEP YOUR VIDEO FOLDER AVAILABLE WHEN MOVING PROJECTS",accent,1);
    detail_opaque_panel(fb,90,76,460,266);
}
