/* Runs only in the low-priority visual child. A folder is indexed once and
   shuffled without replacement; decoding is bounded to incoming + outgoing.
   Interrupted transitions retain one GPU snapshot, never a stack of decoders. */
static void m_video_fit(Mandala *m,SDL_Texture *texture,Uint8 alpha)
{
    if(!texture || !alpha)return;
    int w=0,h=0;SDL_QueryTexture(texture,NULL,NULL,&w,&h);if(w<1 || h<1)return;
    float scale=fminf((float)m->width/w,(float)m->height/h);
    SDL_FRect dest={(m->width-w*scale)*.5f,(m->height-h*scale)*.5f,w*scale,h*scale};
    SDL_SetTextureAlphaMod(texture,alpha);SDL_RenderCopyF(m->renderer,texture,NULL,&dest);SDL_SetTextureAlphaMod(texture,255);
}
static void m_video_layer(Mandala *m,SDL_Texture *texture,Uint8 alpha,float level,int plain)
{
    if(!texture || !alpha)return;
    if(plain){m_video_fit(m,texture,alpha);return;}
    SDL_Texture *saved=m->video_texture;m->video_texture=texture;m_video_scene(m,alpha,level);m->video_texture=saved;
}
static void m_video_layers(Mandala *m,Uint8 alpha,float level,int plain)
{
    double fraction=m->cue_fading && m->video_texture?fmin(1,m->cue_elapsed/fmax(.001,m->cue_fade)):m->cue_fading?0:1;
    double incoming=m->cue_transition==2?fmax(0,2*fraction-1):fraction;
    double outgoing=m->cue_transition==2?fmax(0,1-2*fraction):1-fraction;
    if(plain){if(m->cue_snapshot)SDL_SetTextureBlendMode(m->cue_snapshot,SDL_BLENDMODE_ADD);if(m->old_texture)SDL_SetTextureBlendMode(m->old_texture,SDL_BLENDMODE_ADD);if(m->video_texture)SDL_SetTextureBlendMode(m->video_texture,SDL_BLENDMODE_ADD);}
    if(m->cue_snapshot)m_video_fit(m,m->cue_snapshot,(Uint8)(alpha*outgoing));
    else m_video_layer(m,m->old_texture,(Uint8)(alpha*outgoing),level,plain);
    m_video_layer(m,m->video_texture,(Uint8)(alpha*incoming),level,plain);
    if(plain){if(m->cue_snapshot)SDL_SetTextureBlendMode(m->cue_snapshot,SDL_BLENDMODE_BLEND);if(m->old_texture)SDL_SetTextureBlendMode(m->old_texture,SDL_BLENDMODE_BLEND);if(m->video_texture)SDL_SetTextureBlendMode(m->video_texture,SDL_BLENDMODE_BLEND);}
}
static int m_cue_path(Mandala *m,const TsVisualCue *cue,char path[1024])
{
    if(!cue->pool){snprintf(path,1024,"%s",cue->path);return 1;}
    if(!m->cue_pool || strcmp(m->cue_pool_path,cue->path)) {
        if(!m->cue_pool)m->cue_pool=calloc(1,sizeof(*m->cue_pool));
        if(!m->cue_pool)return 0;
        ts_browser_init(m->cue_pool);m->cue_pool->mode=TS_BROWSER_LOAD_VIDEO;
        snprintf(m->cue_pool->directory,sizeof(m->cue_pool->directory),"%s",cue->path);
        if(!ts_browser_refresh(m->cue_pool))return 0;
        snprintf(m->cue_pool_path,sizeof(m->cue_pool_path),"%s",cue->path);
        m->cue_bag_count=m->cue_bag_at=0;m->cue_last=-1;
    }
    if(m->cue_bag_at>=m->cue_bag_count) {
        m->cue_bag_count=0;
        for(int i=0;i<m->cue_pool->entry_count;++i)if(!m->cue_pool->entries[i].is_directory)m->cue_bag[m->cue_bag_count++]=i;
        for(int i=m->cue_bag_count-1;i>0;--i){int k=(int)(mrand(m)*(i+1));int swap=m->cue_bag[i];m->cue_bag[i]=m->cue_bag[k];m->cue_bag[k]=swap;}
        if(m->cue_bag_count>1 && m->cue_bag[0]==m->cue_last){int swap=m->cue_bag[0];m->cue_bag[0]=m->cue_bag[1];m->cue_bag[1]=swap;}
        m->cue_bag_at=0;
    }
    if(!m->cue_bag_count)return 0;
    int index=m->cue_bag[m->cue_bag_at++];m->cue_last=index;
    int length=snprintf(path,1024,"%s/%s",m->cue_pool_path,m->cue_pool->entries[index].name);
    return length>0 && length<1024;
}
static void m_video_retire(Mandala *m)
{
    ts_video_close(m->old_video);m->old_video=NULL;
    SDL_DestroyTexture(m->old_texture);m->old_texture=NULL;
    SDL_DestroyTexture(m->cue_snapshot);m->cue_snapshot=NULL;m->cue_fading=0;
}
static void m_video_change(Mandala *m,const char *path,double position,int transition,double fade)
{
    int interrupted=m->cue_fading;
    SDL_Texture *snapshot=NULL;
    if(interrupted) {
        snapshot=SDL_CreateTexture(m->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,m->width,m->height);
        if(snapshot){SDL_SetRenderTarget(m->renderer,snapshot);SDL_RenderCopy(m->renderer,m->history,NULL,NULL);SDL_SetRenderTarget(m->renderer,NULL);SDL_SetTextureBlendMode(snapshot,SDL_BLENDMODE_BLEND);}
    }
    m_video_retire(m);
    if(interrupted && snapshot){ts_video_close(m->video);m->video=NULL;SDL_DestroyTexture(m->video_texture);m->video_texture=NULL;m->cue_snapshot=snapshot;}
    else {m->old_restart=m->video_restart-m->video_restart_base;m->old_video=m->video;m->old_texture=m->video_texture;m->old_status=m->video_status;}
    m->video=NULL;m->video_texture=NULL;m->video_new=1;memset(&m->video_status,0,sizeof(m->video_status));
    snprintf(m->video_path,sizeof(m->video_path),"%s",path);
    m->cue_transition=transition;m->cue_fade=transition?fade:0;m->cue_elapsed=0;m->cue_fading=1;
    if(!*path){m_video_retire(m);m->video_status.state=TS_VIDEO_CLOSED;return;}
    m->video=ts_video_open_at(path,position);
    if(!m->video){m->video_status.state=TS_VIDEO_ERROR;snprintf(m->video_status.message,sizeof(m->video_status.message),"VIDEO MISSING OR PLAYER UNAVAILABLE");}
}
static void m_cue_request(Mandala *m,const TsVisualCue *cue)
{
    if(cue->serial==m->cue.serial)return;
    m->cue=*cue;
    if(!cue->path[0]){m->cue_path[0]=m->cue_original[0]=0;return;}
    char path[1024];
    if(!m_cue_path(m,cue,path)) {
        m->video_status.state=TS_VIDEO_ERROR;snprintf(m->video_status.message,sizeof(m->video_status.message),"VIDEO POOL EMPTY OR PATH TOO LONG");return;
    }
    snprintf(m->cue_original,sizeof(m->cue_original),"%s",path);m->cue_fraction=cue->random_start?mrand(m):0;
    if(!m->video_cache)m->video_cache=ts_video_cache_create();
    ts_video_cache_begin(m->video_cache,cue->path,cue->pool);
    char prepared[2048];if(ts_video_cache_path(path,prepared,sizeof(prepared)) && strlen(prepared)<sizeof(path))memcpy(path,prepared,strlen(prepared)+1);
    snprintf(m->cue_path,sizeof(m->cue_path),"%s",path);
    if(m->config.video_enabled)m_video_change(m,path,m->cue_fraction,cue->transition,cue->fade);
}
