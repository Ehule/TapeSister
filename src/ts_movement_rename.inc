/* Two passes make a movement rename all-or-nothing. This edits presentation
   metadata only: stable IDs, sequencing, routes, DSP and samples are untouched. */
int ts_matrix_layout_rename_movement(TsMatrixLayout *layout,TsMosaicGallery *gallery,
    int index,const char *name,char *error,size_t capacity)
{
    if(!layout || !gallery || index<0 || index>=gallery->count || !name || !*name ||
       strlen(name)>=TS_GALLERY_NAME) {
        if(error && capacity)snprintf(error,capacity,"NAME MUST BE 1-47 PRINTABLE CHARACTERS");
        return 0;
    }
    for(const unsigned char *p=(const unsigned char *)name;*p;++p)if(*p<32 || *p>126) {
        if(error && capacity)snprintf(error,capacity,"USE PRINTABLE ASCII IN MOVEMENT NAMES");
        return 0;
    }
    TsMosaicCanvas *canvas=&gallery->canvas[index];
    char old[TS_GALLERY_NAME],next[TS_GALLERY_NAME];
    snprintf(old,sizeof(old),"%s",canvas->name);snprintf(next,sizeof(next),"%s",name);
    if(!strcmp(old,next))return 1;
    int cards=0;
    for(int pass=0;pass<2;++pass) {
        for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            TsMosaicEvent *event=&canvas->scene->events[i];if(!event->id)continue;
            const char *suffix=ts_movement_suffix(event->name,old);if(!suffix)continue;
            if(strlen(next)+strlen(suffix)>=sizeof(event->name)) {
                if(error && capacity)snprintf(error,capacity,"RENAME CANCELLED: CARD %llu WOULD LOSE ITS LAYER NAME; USE A SHORTER TITLE",(unsigned long long)event->id);
                return 0;
            }
            if(pass) {
                char text[sizeof(event->name)];snprintf(text,sizeof(text),"%s%s",next,suffix);
                memcpy(event->name,text,strlen(text)+1);++event->revision;++cards;
            }
        }
        for(int i=0;i<layout->heading_count;++i) {
            TsFxCanvasHeading *heading=&layout->headings[i];
            if(heading->canvas!=canvas->id || !heading->label.text[0])continue;
            const char *suffix=ts_movement_suffix(heading->label.text,old);
            if(!suffix)suffix="";
            if(strlen(next)+strlen(suffix)>=sizeof(heading->label.text)) {
                if(error && capacity)snprintf(error,capacity,"RENAME CANCELLED: MOVEMENT HEADING WOULD BE TOO LONG");
                return 0;
            }
            if(pass) {
                char text[TS_FX_LABEL_TEXT];snprintf(text,sizeof(text),"%s%s",next,suffix);
                memcpy(heading->label.text,text,strlen(text)+1);
            }
        }
    }
    /* Mosaic owns source-card names. FX aliases mirror that canonical name. */
    for(int i=0;i<layout->canvas_count;++i) {
        TsFxCanvasPosition *p=&layout->canvas[i];
        if(p->kind!=3 || p->canvas!=canvas->id)continue;
        const TsMosaicEvent *event=ts_mosaic_find(canvas->scene,p->id);
        if(event)snprintf(p->name,sizeof(p->name),"%s",event->name);
    }
    snprintf(canvas->name,sizeof(canvas->name),"%s",next);
    if(cards)++canvas->scene->revision;
    if(error && capacity)snprintf(error,capacity,"RENAMED MOVEMENT / %d MATCHING CARDS / CTRL+Z: UNDO",cards);
    return 1;
}
