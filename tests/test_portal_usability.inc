static int usability_count(TsPortalUi *p)
{
    TsPortalRecipe r;int count=0;while(ts_portal_filter(p,count,&r))++count;return count;
}
static void test_portal_usability(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;static SisterWindow sister;
    PortalController c;TransformController transform;char error[160];
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sister_runtime_init(&audio.sister);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,441,error,sizeof(error)));
    portal_init(&c,&ui.portal);TsPortalUi *p=&ui.portal;p->open=1;
    memset(&p->library,0,sizeof(p->library));
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    assert(portal_source(device,&audio,&ui,&instrument,&c));
    uint64_t source_hash=ts_sample_hash(p->source),generation=c.generation;
    SDL_Event e={0};
#define USE_EVENT() assert(portal_event(&e,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform))
#define USE_CLICK(X,Y) do {memset(&e,0,sizeof(e));e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;e.button.x=(X);e.button.y=(Y);USE_EVENT();}while(0)
#define USE_KEY(K,M,R) do {memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=(K);e.key.keysym.mod=(M);e.key.repeat=(R);USE_EVENT();}while(0)
    /* Channel filtering combines with family/search, including actual chain
       eligibility and stable, sparse saved/pin slots. No filtering renders. */
    int all=usability_count(p);assert(all==210);
    USE_CLICK(30,134);assert(p->channel_filter==TS_PORTAL_CHANNEL_STEREO && usability_count(p)==57);
    p->family=TS_PORTAL_LEVEL+1;assert(usability_count(p)==5);
    snprintf(p->query,sizeof(p->query),"gain");assert(usability_count(p)==2);
    p->family=0;p->query[0]=0;
    USE_CLICK(30,134);assert(p->channel_filter==TS_PORTAL_CHANNEL_MONO_ONLY && usability_count(p)==all-57);
    USE_CLICK(30,134);assert(usability_count(p)==all && c.generation==generation && !c.auto_pending);
    TsPortalRecipe r;assert(ts_portal_instrument_recipe(0,&r)); /* DUST HALO includes mono-only stages. */
    p->library.recipes[19]=r;p->library.recipes[19].stages[0].bypass=1;p->library.recipes[19].stages[1].bypass=1;
    p->library.recipes[4]=r;
    p->library.pins[27]=p->library.recipes[19];
    p->channel_filter=TS_PORTAL_CHANNEL_STEREO;p->tab=1;
    assert(ts_portal_filter_slot(p,0,&r)==19 && usability_count(p)==1);
    p->tab=2;assert(ts_portal_filter_slot(p,0,&r)==27 && usability_count(p)==1);
    p->tab=1;p->channel_filter=TS_PORTAL_CHANNEL_MONO_ONLY;assert(ts_portal_filter_slot(p,0,&r)==4);
    /* Search click clears existing text and scroll; caret stays inside the
       field for both empty and maximum-length text. */
    p->tab=0;p->channel_filter=TS_PORTAL_CHANNEL_ANY;
    snprintf(p->query,sizeof(p->query),"OLD QUERY");p->scroll=4;
    USE_CLICK(30,68);assert(p->search_focus && !p->query[0] && !p->scroll);
    static TsFramebuffer off,on;
    for(int long_text=0;long_text<2;++long_text) {
        if(long_text)snprintf(p->query,sizeof(p->query),"1234567890123456789012345678901");
        ui.text_cursor_visible=0;ts_ui_render(&off,&ui,&instrument);
        ui.text_cursor_visible=1;ts_ui_render(&on,&ui,&instrument);
        int changed=0;for(int y=62;y<78;++y)for(int x=14;x<136;++x)changed+=off.pixels[y*640+x]!=on.pixels[y*640+x];
        assert(changed==9);
    }
    USE_CLICK(30,68);selection_screenshot("TS_TEST_PORTAL_SEARCH_SHOT",&ui,&instrument);
    e.type=SDL_TEXTINPUT;e.text.windowID=SDL_GetWindowID(window);snprintf(e.text.text,sizeof(e.text.text),"gain");USE_EVENT();
    assert(!strcmp(p->query,"gain") && !c.auto_pending && c.generation==generation);
    USE_KEY(SDLK_a,KMOD_CTRL,0);assert(!p->query[0]);
    /* Arrows enter search results, repeat through them, scroll the selection
       into view, and do nothing at the boundary or in other text fields. */
    USE_KEY(SDLK_DOWN,KMOD_NONE,0);assert(!p->search_focus && p->selected_slot==0);
    for(int i=1;i<=13;++i) {USE_KEY(SDLK_DOWN,KMOD_NONE,1);assert(p->selected_slot==i);}
    assert(p->scroll==3);
    USE_KEY(SDLK_UP,KMOD_NONE,0);assert(p->selected_slot==12);
    p->selected_slot=0;p->scroll=0;generation=c.generation;
    USE_KEY(SDLK_UP,KMOD_NONE,1);assert(p->selected_slot==0 && c.generation==generation);
    USE_KEY(SDLK_LEFT,KMOD_NONE,0);assert(p->family==TS_PORTAL_FAMILIES && c.generation==generation);
    USE_KEY(SDLK_RIGHT,KMOD_NONE,0);assert(p->family==0 && c.generation==generation);
    p->name_focus=1;USE_KEY(SDLK_DOWN,KMOD_NONE,0);assert(p->name_focus && c.generation==generation);p->name_focus=0;
    p->number_focus=0;USE_KEY(SDLK_RIGHT,KMOD_NONE,0);assert(p->number_focus==0 && p->family==0);p->number_focus=-1;
    snprintf(p->query,sizeof(p->query),"NO SUCH PROCESS");USE_KEY(SDLK_DOWN,KMOD_NONE,0);assert(c.generation==generation);
    /* Sparse saved and pin navigation chooses the real slot; chain ADD uses
       the identical path as a click and never nests a factory chain. */
    p->query[0]=0;p->tab=2;p->channel_filter=TS_PORTAL_CHANNEL_STEREO;p->selected_tab=-1;
    USE_KEY(SDLK_DOWN,KMOD_NONE,0);assert(p->pin_slot==27 && p->selected_slot==27 && p->chain_active);
    USE_CLICK(490,70);assert(p->chain_add);
    snprintf(p->query,sizeof(p->query),"BROKEN SIGNAL");USE_KEY(SDLK_DOWN,KMOD_NONE,0);
    assert(p->chain_add && p->chain.stage_count==3 && strstr(p->message,"SINGLE PROCESS"));
    snprintf(p->query,sizeof(p->query),"LINEAR GAIN");USE_KEY(SDLK_DOWN,KMOD_NONE,0);
    assert(!p->chain_add && p->chain.stage_count==4 && !strcmp(p->recipe.process_id,"modify.loudness.1"));
    /* Keyboard selection cancels in-flight obsolete work; the new automatic
       result still renders through the normal worker and preserves source. */
    USE_CLICK(25,367);assert(!p->chain_active);
    p->query[0]=0;p->family=TS_PORTAL_LEVEL+1;p->selected_tab=-1;
    USE_KEY(SDLK_DOWN,KMOD_NONE,0);p->auto_preview=1;
    portal_preview(device,&audio,&ui,&c);assert(c.worker);PortalWorker *old=c.worker;
    USE_KEY(SDLK_DOWN,KMOD_NONE,0);assert(SDL_AtomicGet(&old->cancel) && c.auto_pending);
    c.auto_due=SDL_GetTicks()+60000;wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid);
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    assert(ts_sample_hash(p->source)==source_hash);
    /* Hover delay, cancellation and wraparound leave the actual status,
       recipe, source and completed render untouched. */
    p->auto_preview=0;generation=c.generation;
    snprintf(p->message,sizeof(p->message),"KEEP THIS OPERATION STATUS");
    portal_hover_reset(p,&c);portal_hover_poll(p,&c,260,340,1000,0);assert(!p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,1599,0);assert(!p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,1600,0);assert(strstr(p->tooltip,"REPLACE THE ORIGINAL TILE"));
    assert(!strcmp(p->message,"KEEP THIS OPERATION STATUS") && c.generation==generation && p->valid);
    selection_screenshot("TS_TEST_PORTAL_HELP_SHOT",&ui,&instrument);
    snprintf(p->message,sizeof(p->message),"NEW RESULT STATUS");portal_hover_poll(p,&c,260,340,1700,0);assert(!p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,4099,0);assert(!p->tooltip[0]);portal_hover_poll(p,&c,260,340,4100,0);assert(p->tooltip[0]);
    portal_hover_poll(p,&c,-1,-1,4200,0);assert(!p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,UINT32_MAX-299u,0);portal_hover_poll(p,&c,260,340,299,0);assert(!p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,300,0);assert(p->tooltip[0]);
    portal_hover_poll(p,&c,260,340,301,1);assert(!p->tooltip[0]);
    p->busy=1;portal_hover_poll(p,&c,260,340,10000,0);assert(!p->tooltip[0]);p->busy=0;
    p->manage_open=1;portal_hover_poll(p,&c,260,340,11000,0);portal_hover_poll(p,&c,260,340,12000,0);assert(!p->tooltip[0]);p->manage_open=0;
    char help[160];assert(strstr(portal_hover_help(p,320,340,help,sizeof(help)),"KEEP THE ORIGINAL"));
    assert(strstr(portal_hover_help(p,400,340,help,sizeof(help)),"CONTINUE"));
    p->channel_filter=TS_PORTAL_CHANNEL_STEREO;p->family=0;
    portal_hover_reset(p,&c);selection_screenshot("TS_TEST_PORTAL_FILTER_SHOT",&ui,&instrument);
    portal_close(device,&audio,&ui,&c);portal_free(&c);ts_instrument_free(&instrument);
    ts_sister_runtime_free(&audio.sister);ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
#undef USE_EVENT
#undef USE_CLICK
#undef USE_KEY
}
