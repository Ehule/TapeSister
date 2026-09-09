/* Native-pixel signal layer. Cache actual audio extrema at the output width;
   never enlarge the 600-column envelope to manufacture apparent detail. */
static TsUiWaveformDetail *active_waveform_detail;

void ts_ui_waveform_detail_free(TsUiWaveformDetail *d)
{
    if (!d) return;
    if (active_waveform_detail == d) active_waveform_detail = NULL;
    free(d->pixels); free(d->coarse); free(d->columns); free(d->logical_x);
    memset(d, 0, sizeof(*d));
}

int ts_ui_waveform_detail_resize(TsUiWaveformDetail *d, int width, int height)
{
    if (!d || width <= 0 || height <= 0 || width > 16384 || height > 4096) return 0;
    if (d->pixels && width == d->width && height == d->height) return 1;
    TsUiWaveformDetail next = {0};
    next.pixels = malloc((size_t)width * height * sizeof(*next.pixels));
    next.coarse = malloc(TS_WAVE_W * TS_WAVE_H * sizeof(*next.coarse));
    next.columns = malloc((size_t)width * sizeof(*next.columns));
    next.logical_x = malloc((size_t)width * sizeof(*next.logical_x));
    if (!next.pixels || !next.coarse || !next.columns || !next.logical_x) {
        ts_ui_waveform_detail_free(&next); return 0;
    }
    next.width = width; next.height = height;
    for (int x = 0; x < width; ++x)
        next.logical_x[x] = (int)((2LL*x+1) * TS_WAVE_W / (2LL*width));
    ts_ui_waveform_detail_free(d); *d = next;
    return 1;
}

void ts_ui_waveform_detail_begin(TsUiWaveformDetail *d)
{
    active_waveform_detail = d;
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

static double detail_y(const TsUiWaveformDetail *d, float value, int lane, int stereo)
{
    double height = stereo ? TS_WAVE_H/2.0 : TS_WAVE_H;
    double center = lane*height + height/2.0;
    double scale = height/2.0 - (stereo ? 4 : 6);
    return (center - fmaxf(-1,fminf(1,value))*scale) * d->height/TS_WAVE_H;
}

static void detail_render(TsFramebuffer *fb, const TsUiState *ui,
    const TsWaveformRequest *request, uint64_t revision,
    int selected, size_t selection_first, size_t selection_last)
{
    TsUiWaveformDetail *d=active_waveform_detail;
    if (!d || !d->pixels) return;
    if (d->source_revision != revision) {
        TsWaveformRequest native=*request; native.width=d->width;
        if (!ts_waveform_analyze_columns(d->columns,&native)) return;
        d->source_revision=revision; ++d->analysis_count;
    }
    for (int y=0; y<d->height; ++y) {
        int ly=(int)((2LL*y+1)*TS_WAVE_H/(2LL*d->height));
        const uint32_t *row=fb->pixels+(TS_WAVE_Y+ly)*TS_UI_WIDTH+TS_WAVE_X;
        for (int x=0; x<d->width; ++x) d->pixels[(size_t)y*d->width+x]=row[d->logical_x[x]];
    }
    const TsSample *sample=request->sample;
    size_t span=request->last-request->first;
    TsWaveformDisplayMode mode=(TsWaveformDisplayMode)ui->config.waveform_display_mode;
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
        uint32_t left=in_selection?PAL_BLOCK_TEXT:mode==TS_WAVEFORM_DISPLAY_MONO_SUM?PAL_WAVE_SUM:PAL_WAVE_LEFT;
        for (int lane=0; lane<(display.stereo?2:1); ++lane) {
            uint32_t color=lane?(in_selection?PAL_TUNING:PAL_WAVE_RIGHT):left;
            double head_y=detail_y(d,lane?head.r:head.l,lane,display.stereo);
            if (x) detail_line(d,x-1,previous[lane],x,head_y,color);
            previous[lane]=detail_y(d,lane?tail.r:tail.l,lane,display.stereo);
            double lo=detail_y(d,lane?display.right_minimum:display.left_minimum,lane,display.stereo);
            double hi=detail_y(d,lane?display.right_maximum:display.left_maximum,lane,display.stereo);
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
            double y=detail_y(d,0,lane,stereo),half=fmax(1.0,1.5*d->height/TS_WAVE_H);
            detail_line(d,x,y-half,x,y+half,PAL_VOLUME);
        }
    }
    d->valid=1;
}

static void detail_capture_coarse(const TsFramebuffer *fb)
{
    TsUiWaveformDetail *d=active_waveform_detail;
    if (!d || !d->valid) return;
    for (int y=0; y<TS_WAVE_H; ++y)
        memcpy(d->coarse+y*TS_WAVE_W,fb->pixels+(TS_WAVE_Y+y)*TS_UI_WIDTH+TS_WAVE_X,
               TS_WAVE_W*sizeof(uint32_t));
}

int ts_ui_waveform_detail_finish(TsUiWaveformDetail *d, const TsFramebuffer *fb)
{
    active_waveform_detail=NULL;
    if (!d || !d->valid || !fb) return 0;
    /* Preserve every later control/label/playhead overlay at its original
       pixel scale; only the underlying signal is rendered at native detail. */
    for (int y=0; y<d->height; ++y) {
        int ly=(int)((2LL*y+1)*TS_WAVE_H/(2LL*d->height));
        const uint32_t *row=fb->pixels+(TS_WAVE_Y+ly)*TS_UI_WIDTH+TS_WAVE_X;
        for (int x=0; x<d->width; ++x) {
            int lx=d->logical_x[x];
            if (row[lx]!=d->coarse[ly*TS_WAVE_W+lx]) d->pixels[(size_t)y*d->width+x]=row[lx];
        }
    }
    return 1;
}
