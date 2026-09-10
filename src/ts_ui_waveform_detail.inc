/* Native-pixel signal layer. Cache actual audio extrema at the output width;
   never enlarge the 600-column envelope to manufacture apparent detail. */
static TsUiWaveformDetail *active_waveform_detail;
static int active_detail_count, detail_output_width, detail_output_height;

void ts_ui_waveform_detail_free(TsUiWaveformDetail *d)
{
    if (!d) return;
    if (active_waveform_detail == d) active_waveform_detail = NULL;
    free(d->pixels); free(d->coarse); free(d->columns); free(d->logical_x);
    memset(d, 0, sizeof(*d));
}

static int detail_resize(TsUiWaveformDetail *d, int width, int height,
                         int x, int y, int logical_width, int logical_height)
{
    if (!d || width <= 0 || height <= 0 || width > 16384 || height > 4096) return 0;
    if (d->pixels && width == d->width && height == d->height &&
        x == d->x && y == d->y && logical_width == d->logical_width &&
        logical_height == d->logical_height) return 1;
    TsUiWaveformDetail next = {0};
    next.pixels = malloc((size_t)width * height * sizeof(*next.pixels));
    next.coarse = malloc((size_t)logical_width * logical_height * sizeof(*next.coarse));
    next.columns = malloc((size_t)width * sizeof(*next.columns));
    next.logical_x = malloc((size_t)width * sizeof(*next.logical_x));
    if (!next.pixels || !next.coarse || !next.columns || !next.logical_x) {
        ts_ui_waveform_detail_free(&next); return 0;
    }
    next.width = width; next.height = height;
    next.x=x; next.y=y; next.logical_width=logical_width; next.logical_height=logical_height;
    for (int x = 0; x < width; ++x)
        next.logical_x[x] = (int)((2LL*x+1) * logical_width / (2LL*width));
    TsUiWaveformDetail *active=active_waveform_detail;
    ts_ui_waveform_detail_free(d); *d = next; active_waveform_detail=active;
    return 1;
}

int ts_ui_waveform_detail_resize(TsUiWaveformDetail *d, int width, int height)
{
    return detail_resize(d,width,height,TS_WAVE_X,TS_WAVE_Y,TS_WAVE_W,TS_WAVE_H);
}

void ts_ui_waveform_details_begin(TsUiWaveformDetail details[2], int width, int height)
{
    active_waveform_detail=details; active_detail_count=2;
    detail_output_width=width; detail_output_height=height;
    for(int i=0;i<2;++i)details[i].valid=0;
}

void ts_ui_waveform_detail_begin(TsUiWaveformDetail *d)
{
    active_waveform_detail = d; active_detail_count=1;
    detail_output_width=detail_output_height=0;
    if (d) d->valid = 0;
}

static void detail_plot(TsUiWaveformDetail *d, int x, int y, uint32_t color, double alpha)
{
    if (x < 0 || x >= d->width || y < 0 || y >= d->height || alpha <= 0) return;
    uint32_t *pixel = &d->pixels[(size_t)y*d->width+x];
    *pixel = blend_color(*pixel, color, (unsigned)lrint(fmin(1.0, alpha)*100));
}

/* One physical pixel, with coverage on its two neighboring raster rows. */
static void detail_line(TsUiWaveformDetail *d, double x0, double y0,
                         double x1, double y1, uint32_t color)
{
    /* Envelopes are vertical at an integer column. Fill their fully covered
       pixels directly; only the two end caps need alpha blending. */
    if(x0==x1 && x0==floor(x0) && y0!=y1) {
        int x=(int)x0;
        if(x<0 || x>=d->width)return;
        double lo=fmin(y0,y1),hi=fmax(y0,y1);
        int first=(int)floor(lo),last=(int)ceil(hi);
        if(first<0)first=0;if(last>=d->height)last=d->height-1;
        if(first>last)return;
        int full_first=(int)ceil(lo-.5),full_last=(int)floor(hi-.5);
        if(full_first<first)full_first=first;if(full_last>last)full_last=last;
        for(int y=full_first;y<=full_last;++y)d->pixels[(size_t)y*d->width+x]=color;
        if(first<full_first)
            detail_plot(d,x,first,color,fmin(first+1.0,hi+.5)-fmax((double)first,lo-.5));
        int cap=full_last+1;
        if(cap>=first && cap<=last && !(cap==first && first<full_first))
            detail_plot(d,x,cap,color,fmin(cap+1.0,hi+.5)-fmax((double)cap,lo-.5));
        return;
    }
    int steep = fabs(y1-y0) > fabs(x1-x0);
    if (steep) { double t=x0; x0=y0; y0=t; t=x1; x1=y1; y1=t; }
    if (x1 < x0) { double t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
    double gradient = x1 > x0 ? (y1-y0)/(x1-x0) : 0;
    int first=(int)floor(x0), last=(int)ceil(x1);
    for (int x=first; x<=last; ++x) {
        double y=y0+(x-x0)*gradient;
        int iy=(int)floor(y); double fraction=y-iy;
        double edge = fmin(x+1.0,x1+0.5)-fmax((double)x,x0-0.5);
        if (steep) {
            detail_plot(d,iy,x,color,(1-fraction)*edge);
            detail_plot(d,iy+1,x,color,fraction*edge);
        } else {
            detail_plot(d,x,iy,color,(1-fraction)*edge);
            detail_plot(d,x,iy+1,color,fraction*edge);
        }
    }
}

static double detail_y(const TsUiWaveformDetail *d, float value, int lane, int stereo, double padding)
{
    double height = stereo ? d->logical_height/2.0 : d->logical_height;
    double center = lane*height + height/2.0;
    double scale = height/2.0 - padding;
    return (center - fmaxf(-1,fminf(1,value))*scale) * d->height/d->logical_height;
}

static TsUiWaveformDetail *detail_region(int slot, int x, int y, int width, int height)
{
    if (!active_waveform_detail || slot>=active_detail_count) return NULL;
    TsUiWaveformDetail *d=&active_waveform_detail[slot];
    if (detail_output_width>0 && detail_output_height>0) {
        int ox=x*detail_output_width/TS_UI_WIDTH, oy=y*detail_output_height/TS_UI_HEIGHT;
        int w=(x+width)*detail_output_width/TS_UI_WIDTH-ox;
        int h=(y+height)*detail_output_height/TS_UI_HEIGHT-oy;
        if(!detail_resize(d,w,h,x,y,width,height))return NULL;
        d->output_x=ox; d->output_y=oy;
    }
    return d->pixels?d:NULL;
}

static void detail_background(TsUiWaveformDetail *d, const TsFramebuffer *fb)
{
    for (int y=0; y<d->height; ++y) {
        int ly=(int)((2LL*y+1)*d->logical_height/(2LL*d->height));
        const uint32_t *row=fb->pixels+(d->y+ly)*TS_UI_WIDTH+d->x;
        for (int x=0; x<d->width; ++x) d->pixels[(size_t)y*d->width+x]=row[d->logical_x[x]];
    }
}

static void detail_sample(TsFramebuffer *fb, int slot, int x, int y, int width, int height,
    const TsWaveformRequest *request, TsWaveformDisplayMode mode,
    uint32_t left_color, uint32_t right_color, uint32_t selected_left, uint32_t selected_right,
    int selected, size_t selection_first, size_t selection_last, double padding)
{
    TsUiWaveformDetail *d=detail_region(slot,x,y,width,height);
    if(!d)return;
    const TsSample *sample=request->sample;
    if(d->source!=sample || d->source_data!=sample->data ||
       d->source_frames!=sample->frames || d->source_channels!=sample->channels ||
       d->detect_zero_crossings!=request->detect_zero_crossings ||
       d->first!=request->first || d->last!=request->last ||
       d->source_revision!=request->revision) {
        TsWaveformRequest native=*request;native.width=d->width;
        if(!ts_waveform_analyze_columns(d->columns,&native))return;
        d->source=sample;d->source_data=sample->data;d->source_frames=sample->frames;
        d->detect_zero_crossings=request->detect_zero_crossings;
        d->source_channels=sample->channels;d->first=request->first;d->last=request->last;
        d->source_revision=request->revision;++d->analysis_count;
    }
    detail_background(d,fb);
    size_t span=request->last-request->first;
    double previous[2]={0,0};
    for (int x=0; x<d->width; ++x) {
        const TsWaveformColumn *column=&d->columns[x];
        TsWaveformDisplayColumn display=ts_waveform_display_column(column,sample->channels,mode);
        TsStereoFrame head,tail;
        if (span <= (size_t)d->width) {
            double at=request->first+(double)x*span/d->width;
            size_t frame=(size_t)at;
            TsStereoFrame a=ts_sample_read_frame(sample,frame);
            TsStereoFrame b=ts_sample_read_frame(sample,frame+1<request->last?frame+1:frame);
            float fraction=(float)(at-frame);
            head=ts_waveform_display_frame((TsStereoFrame){a.l+(b.l-a.l)*fraction,a.r+(b.r-a.r)*fraction},sample->channels,mode);
            tail=head;
            display.left_minimum=display.left_maximum=head.l;
            display.right_minimum=display.right_maximum=head.r;
        } else {
            head=ts_waveform_display_frame(ts_sample_read_frame(sample,column->first),sample->channels,mode);
            tail=ts_waveform_display_frame(ts_sample_read_frame(sample,column->last-1),sample->channels,mode);
        }
        int in_selection=selected && column->last>selection_first && column->first<selection_last;
        uint32_t left=in_selection?selected_left:left_color;
        for (int lane=0; lane<(display.stereo?2:1); ++lane) {
            uint32_t color=lane?(in_selection?selected_right:right_color):left;
            double head_y=detail_y(d,lane?head.r:head.l,lane,display.stereo,padding);
            if (x) detail_line(d,x-1,previous[lane],x,head_y,color);
            previous[lane]=detail_y(d,lane?tail.r:tail.l,lane,display.stereo,padding);
            double lo=detail_y(d,lane?display.right_minimum:display.left_minimum,lane,display.stereo,padding);
            double hi=detail_y(d,lane?display.right_maximum:display.left_maximum,lane,display.stereo,padding);
            detail_line(d,x,lo,x,hi,color);
        }
    }
    /* Mark exactly the shared cut boundaries, on each crossing stereo lane. */
    for (int x=0; x<d->width; ++x) {
        unsigned mask=d->columns[x].zero_crossing_channels;
        if (!mask) continue;
        int stereo=sample->channels==2 && mode==TS_WAVEFORM_DISPLAY_STEREO;
        for (int lane=0; lane<(stereo?2:1); ++lane) {
            if (stereo && !(mask&(1u<<lane))) continue;
            double y=detail_y(d,0,lane,stereo,padding),half=fmax(1.0,1.5*d->height/d->logical_height);
            detail_line(d,x,y-half,x,y+half,PAL_VOLUME);
        }
    }
    d->valid=1;
}

static void detail_capture_slot(const TsFramebuffer *fb, int slot)
{
    TsUiWaveformDetail *d=active_waveform_detail && slot<active_detail_count?&active_waveform_detail[slot]:NULL;
    if (!d || !d->valid) return;
    for (int y=0; y<d->logical_height; ++y)
        memcpy(d->coarse+y*d->logical_width,fb->pixels+(d->y+y)*TS_UI_WIDTH+d->x,
               d->logical_width*sizeof(uint32_t));
}

int ts_ui_waveform_detail_finish(TsUiWaveformDetail *d, const TsFramebuffer *fb)
{
    active_waveform_detail=NULL;
    if (!d || !d->valid || !fb) return 0;
    /* Preserve every later control/label/playhead overlay at its original
       pixel scale; only the underlying signal is rendered at native detail. */
    for (int y=0; y<d->height; ++y) {
        int ly=(int)((2LL*y+1)*d->logical_height/(2LL*d->height));
        const uint32_t *row=fb->pixels+(d->y+ly)*TS_UI_WIDTH+d->x;
        for (int x=0; x<d->width; ++x) {
            int lx=d->logical_x[x];
            if (row[lx]!=d->coarse[ly*d->logical_width+lx]) d->pixels[(size_t)y*d->width+x]=row[lx];
        }
    }
    return 1;
}

static void detail_render(TsFramebuffer *fb, const TsUiState *ui,
    const TsWaveformRequest *request, uint64_t revision,
    int selected, size_t first, size_t last)
{
    (void)revision;
    if(ui->fm_open || ui->input_meter_active || ui->amplitude_gesture.active)return;
    TsWaveformDisplayMode mode=ts_waveform_display_sanitize(ui->config.waveform_display_mode);
    detail_sample(fb,0,TS_WAVE_X,TS_WAVE_Y,TS_WAVE_W,TS_WAVE_H,request,mode,
        mode==TS_WAVEFORM_DISPLAY_MONO_SUM?PAL_WAVE_SUM:PAL_WAVE_LEFT,PAL_WAVE_RIGHT,
        PAL_BLOCK_TEXT,PAL_TUNING,selected,first,last,
        request->sample->channels==2 && mode==TS_WAVEFORM_DISPLAY_STEREO?4:6);
}
static void detail_capture_coarse(const TsFramebuffer *fb) { detail_capture_slot(fb,0); }
