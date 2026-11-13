/* Exercise observations separately from retained totals, including reset/wrap. */
static int health_row_has(const TsFramebuffer *fb,int y,uint32_t color)
{
    for(int j=y;j<y+7;++j)for(int x=20;x<620;++x)
        if(fb->pixels[j*640+x]==color)return 1;
    return 0;
}
static void check_health_recent_events(const TsUiState *ui,const AudioState *audio)
{
    TsAudioHealthWindow *saved=malloc(sizeof(*saved));assert(saved);*saved=ts_health;
    TsFramebuffer *fb=calloc(1,sizeof(*fb));assert(fb);
    uint64_t totals[6]={500,100,80,40,30,20};
    ts_health.events_valid=0;
    ts_audio_health_recent_events(totals,10000,1000);
    assert(!ts_health.events_ready);
    ++totals[0];totals[2]+=2;
    ts_audio_health_recent_events(totals,10025,1250);
    assert(!ts_health.events_ready);
    totals[1]+=3;ts_audio_health_recent_events(totals,10100,2000);
    assert(ts_health.events_ready && ts_health.events_seconds==1);
    assert(ts_health.events_recent[0]==1 && ts_health.events_recent[1]==3 &&
           ts_health.events_recent[2]==2 && ts_health.events_recent[5]==0);
    ts_health.snapshot.deadline_overruns=501;ts_health.snapshot.near_overruns=103;
    ts_health.snapshot.delayed_starts=82;
    uint32_t ink=ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_TEXT,0xffe0d6e9u);
    uint32_t red=ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_ERROR,0xffeb777du);
    uint32_t amber=ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_WARNING,0xffdfbc72u);
    uint32_t dim=ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_DIM,0xffa5a0aeu);
    ts_audio_health_draw(fb,ui,audio,2000);
    assert(health_row_has(fb,146,ink) && !health_row_has(fb,146,red));
    assert(health_row_has(fb,155,red) && health_row_has(fb,212,amber));
    ts_audio_health_recent_events(totals,10200,3000);
    for(int i=0;i<6;++i)assert(ts_health.events_recent[i]==0);
    ts_audio_health_draw(fb,ui,audio,3000);
    assert(health_row_has(fb,155,dim) && !health_row_has(fb,155,red));
    assert(ts_health.snapshot.deadline_overruns==501);
    totals[0]=0;ts_audio_health_recent_events(totals,10201,3100);
    assert(!ts_health.events_ready); /* A reset of just one source is not a huge delta. */
    ts_audio_health_recent_events(totals,1,3500);assert(!ts_health.events_ready);
    ts_health.events_valid=0;
    ts_audio_health_recent_events(totals,2,UINT32_MAX-500u);
    ++totals[5];ts_audio_health_recent_events(totals,3,600);
    assert(ts_health.events_ready && ts_health.events_recent[5]==1);
    assert(fabs(ts_health.events_seconds-1.101)<.0001);
    ts_health=*saved;free(saved);free(fb);
}
static void health_button_position(SDL_Event *event,int button)
{
    TsUiRect r=ts_audio_health_button_rect(button);int w,h;
    SDL_GetWindowSize(ts_health.window,&w,&h);
    TsUiPresentation p=ts_audio_health_presentation();r=ts_ui_presentation_rect(&p,r);
    event->button.x=(r.x+r.w/2)*w/p.width;
    event->button.y=(r.y+r.h/2)*h/p.height;
}
static void check_health_button_gaps(SDL_Window *window,AudioState *audio,TsUiState *ui)
{
    static const char *longest[]={"","HEARD A CLICK","SAVE UNAVAILABLE","FOLDER OPENED",
        "RESET DONE","COPY FAILED","PROFILE STARTED","CLOSE","SUBSYSTEMS"};
    for(int i=TS_HEALTH_BUTTON_MARK;i<=TS_HEALTH_BUTTON_EFFECTS;++i) {
        TsUiRect r=ts_audio_health_button_rect(i);
        assert(r.h==16 && r.w>=(int)strlen(longest[i])*6+12);
        assert(r.x>=0 && r.x+r.w<=640 && r.y+r.h<=400);
        assert(ts_audio_health_hit(i,r.x+r.w-1,r.y+r.h-1));
        assert(!ts_audio_health_hit(i,r.x+r.w,r.y+r.h-1));
        for(int j=i+1;j<=TS_HEALTH_BUTTON_EFFECTS;++j) {
            TsUiRect b=ts_audio_health_button_rect(j);
            assert(r.x+r.w<=b.x || b.x+b.w<=r.x || r.y+r.h<=b.y || b.y+b.h<=r.y);
        }
    }
    int original_w,original_h;SDL_GetWindowSize(ts_health.window,&original_w,&original_h);
    const int widths[]={800,1280,1600},heights[]={500,800,1000};
    for(int i=0;i<3;++i) {
        SDL_SetWindowSize(ts_health.window,widths[i],heights[i]);
        TsUiRect r=ts_audio_health_button_rect(TS_HEALTH_BUTTON_RESET);
        SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;
        event.button.windowID=ts_health.window_id;event.button.button=SDL_BUTTON_LEFT;
        TsUiPresentation p=ts_audio_health_presentation();
        TsUiRect gap=ts_ui_presentation_rect(&p,(TsUiRect){r.x+r.w+3,r.y+r.h/2,1,1});
        event.button.x=gap.x*widths[i]/p.width;
        event.button.y=gap.y*heights[i]/p.height;
        ts_health.flash_button=TS_HEALTH_BUTTON_NONE;
        assert(ts_audio_health_event(&event,window,audio,ui));
        assert(ts_health.flash_button==TS_HEALTH_BUTTON_NONE);
        health_button_position(&event,TS_HEALTH_BUTTON_COPY);
        assert(ts_audio_health_event(&event,window,audio,ui));
        assert(ts_health.flash_button==TS_HEALTH_BUTTON_COPY);
        if(i==2) {
            assert(p.target[0].w==1280 && p.target[0].h==800);
            event.button.x=10;event.button.y=10;ts_health.flash_button=TS_HEALTH_BUTTON_NONE;
            assert(ts_audio_health_event(&event,window,audio,ui));assert(!ts_health.flash_button);
        }
    }
    SDL_SetWindowSize(ts_health.window,original_w,original_h);
}
static void check_health_profile_scroll(SDL_Window *window,AudioState *audio,TsUiState *ui)
{
    SDL_Event event;SDL_zero(event);event.type=SDL_KEYDOWN;
    event.key.windowID=ts_health.window_id;event.key.keysym.sym=SDLK_END;
    assert(ts_audio_health_event(&event,window,audio,ui));ts_audio_health_update(audio,ui);
    assert(ts_health.profile_scroll==SDL_max(0,TS_MATRIX_EFFECT_CAPACITY-16));
    event.key.keysym.sym=SDLK_HOME;
    assert(ts_audio_health_event(&event,window,audio,ui));assert(!ts_health.profile_scroll);
    event.type=SDL_MOUSEWHEEL;event.wheel.windowID=ts_health.window_id;event.wheel.y=-1;
    assert(ts_audio_health_event(&event,window,audio,ui));ts_audio_health_update(audio,ui);
    assert(ts_health.profile_scroll==SDL_min(3,SDL_max(0,TS_MATRIX_EFFECT_CAPACITY-16)));
}
