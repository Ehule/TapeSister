/* Check the rendered pixels against a geometric stroke, rather than a saved
   screenshot tied to one monitor. Exercise fractional and nonuniform scales. */
static void cable_drawing(const char *dir)
{
    MatrixWindow *w=&matrix_window;
    uint32_t *saved=w->pixels;int saved_w=w->pixel_w,saved_h=w->pixel_h;
    int straight=w->layout->canvas_straight;
    const struct {int w,h;float dpi;} sizes[]={
        {1280,840,1},{1366,699,1},{1600,1050,1.25f},
        {1920,1080,1},{1920,1260,1.5f},{2560,1680,1},{2560,1680,2},{3840,2160,2}
    };
    const float lines[][4]={
        {100.25f,200.35f,850.75f,310.6f}, /* shallow */
        {210.3f,650.7f,300.6f,150.25f}, /* steep and reversed */
        {120,260,900,260},{350,160,350,650},
        {300.5f,320.5f,300.5f,320.5f}, /* zero length */
        {-100000,-100000,100000,100000} /* clipped, bounded work */
    };
    for(size_t size=0;size<sizeof(sizes)/sizeof(*sizes);++size) {
        w->pixel_w=sizes[size].w;w->pixel_h=sizes[size].h;
        size_t count=(size_t)w->pixel_w*w->pixel_h;
        w->pixels=calloc(count,sizeof(*w->pixels));assert(w->pixels);
        assert(fx_stroke_prepare());fx_stroke.scale=sizes[size].dpi;
        for(size_t line=0;line<sizeof(lines)/sizeof(*lines);++line) {
            memset(w->pixels,0,count*sizeof(*w->pixels));
            const float *l=lines[line];fx_line(l[0],l[1],l[2],l[3],0xffffffff);
            FxPoint a=fx_device_point((FxPoint){l[0],l[1]}),b=fx_device_point((FxPoint){l[2],l[3]});
            int core=0,edge=0;
            /* Inspect the entire clip, including pixels outside the stroke.
               This catches holes, widened steps, stray pixels and clipping. */
            for(int y=0;y<w->pixel_h;++y)for(int x=0;x<w->pixel_w;++x) {
                unsigned actual=w->pixels[(size_t)y*w->pixel_w+x]&255,expected=0;
                if(x>=mx_px(20) && x<mx_px(fx_right()) && y>=mx_py(FX_TOP) && y<mx_py(FX_BOTTOM)) {
                    float d=sqrtf(fx_segment_distance(x+.5f,y+.5f,a.x,a.y,b.x,b.y));
                    expected=(unsigned)(SDL_clamp(sizes[size].dpi+.5f-d,0,1)*255+.5f);
                }
                /* Huge offscreen coordinates lose a little float precision. */
                assert(abs((int)actual-(int)expected)<=(line==5?6:1));
                core+=actual==255;edge+=actual>0 && actual<255;
            }
            assert((core || line==4) && (edge || line==2 || line==3));
            for(int y=0;y<w->pixel_h;++y)assert(fx_stroke.left[y]==w->pixel_w && fx_stroke.right[y]==0);
        }
        /* The same curve drives rasterization, clicks and wire marquee. */
        w->layout->canvas_straight=0;
        const float curves[][4]={{100.25f,180.4f,850.8f,610.25f},{800,160,140,620},{400,200,400,650},{500,350,350,350}};
        for(size_t curve=0;curve<sizeof(curves)/sizeof(*curves);++curve) {
            const float *c=curves[curve];FxCablePath path;fx_cable_path(&path,c[0],c[1],c[2],c[3]);
            assert(path.count>=2 && path.count<=FX_CURVE_POINTS);
            float bend=fmaxf(40,fabsf(c[2]-c[0])*.45f);
            for(int sample=0;sample<=1000;++sample) {
                float t=sample/1000.f,u=1-t;
                FxPoint exact={u*u*u*c[0]+3*u*u*t*(c[0]+bend)+3*u*t*t*(c[2]-bend)+t*t*t*c[2],
                    u*u*u*c[1]+3*u*u*t*c[1]+3*u*t*t*c[3]+t*t*t*c[3]};
                FxPoint px=fx_device_point(exact);float distance=1e12f;
                for(int i=1;i<path.count;++i) {
                    FxPoint a=fx_device_point(path.point[i-1]),b=fx_device_point(path.point[i]);
                    distance=fminf(distance,fx_segment_distance(px.x,px.y,a.x,a.y,b.x,b.y));
                }
                assert(distance<=.065f);
                if(sample%100==0) {
                    assert(fx_cable(c[0],c[1],c[2],c[3],0,0,(int)lrintf(exact.x),(int)lrintf(exact.y))<1);
                    assert(fx_cable_crosses(c[0],c[1],c[2],c[3],(SDL_Rect){(int)floorf(exact.x)-1,(int)floorf(exact.y)-1,3,3}));
                }
            }
        }
        /* Unioning overlapping segments preserves exactly the same stroke. */
        memset(w->pixels,0,count*sizeof(*w->pixels));
        FxPoint a=fx_device_point((FxPoint){100,250}),b=fx_device_point((FxPoint){800,450});
        fx_stroke_begin();fx_stroke_segment(a,b);fx_stroke_segment(a,b);fx_stroke_end(0xffffffff);
        uint32_t *once=malloc(count*sizeof(*once));assert(once);memcpy(once,w->pixels,count*sizeof(*once));
        memset(w->pixels,0,count*sizeof(*w->pixels));
        fx_stroke_begin();fx_stroke_segment(a,b);fx_stroke_end(0xffffffff);
        assert(!memcmp(once,w->pixels,count*sizeof(*once)));free(once);
        /* A cached scene must match a fresh draw after moving endpoints,
           changing selection color/style, removing all cables, and resizing. */
        uint32_t *reference=malloc(count*sizeof(*reference));assert(reference);
        for(int variant=0;variant<5;++variant) {
            w->layout->canvas_straight=variant==3;
            uint32_t color=variant==2?FX_FOCUS:FX_CABLE;
            float start=variant==1?240.25f:180.5f;
            for(int pass=0;pass<3;++pass) {
                mx_rect(0,0,MATRIX_W,MATRIX_H,FX_BG);
                if(pass<2) {
                    fx_cables_begin();
                    if(variant!=4)fx_cables_add(start,200.5f,850.75f,610.25f,color);
                    fx_cables_end();
                    if(pass==0)memcpy(reference,w->pixels,count*sizeof(*reference));
                    else assert(!fx_cable_cache.changed);
                } else if(variant!=4)fx_cable(start,200.5f,850.75f,610.25f,color,1,0,0);
                assert(!memcmp(reference,w->pixels,count*sizeof(*reference)));
            }
        }
        free(reference);w->layout->canvas_straight=0;
        /* A few strokes are enough for a native-resolution visual reference. */
        if(dir) {
            mx_rect(0,0,MATRIX_W,MATRIX_H,FX_BG);
            for(size_t curve=0;curve<sizeof(curves)/sizeof(*curves);++curve) {
                const float *c=curves[curve];fx_cable(c[0],c[1],c[2],c[3],curve%2?FX_FOCUS:FX_CABLE,1,0,0);
            }
            char name[1024];snprintf(name,sizeof(name),"%s/cables-%dx%d-%d.bmp",dir,w->pixel_w,w->pixel_h,(int)(sizes[size].dpi*100));
            SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormatFrom(w->pixels,w->pixel_w,w->pixel_h,32,w->pixel_w*4,SDL_PIXELFORMAT_ARGB8888);
            assert(surface && !SDL_SaveBMP(surface,name));SDL_FreeSurface(surface);
            for(int pass=0;pass<2;++pass) {
                mx_rect(0,0,MATRIX_W,MATRIX_H,FX_BG);
                Uint64 start=SDL_GetPerformanceCounter();fx_cables_begin();
                for(int i=0;i<250;++i)fx_cables_add(80+(i%11)*2.f,150+(i%29)*16.f,920-(i%13)*2.f,160+(i%31)*15.f,FX_CABLE);
                fx_cables_end();
                printf("250 cables at %dx%d, %.2fx DPI (%s): %.2f ms\n",w->pixel_w,w->pixel_h,sizes[size].dpi,pass?"cached":"redraw",
                    1000.*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency());
            }
        }
        free(w->pixels);
    }
    w->pixels=saved;w->pixel_w=saved_w;w->pixel_h=saved_h;w->layout->canvas_straight=straight;
    fx_cables_free();assert(fx_stroke_prepare());
    puts("Native cable pixels, fractional DPI, bounded curves, clipping, joins and selection passed.");
}
