#include "tapesister/dark_matter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

static const double share[4] = {.17, .23, .27, .33};
static const double rate_hz[4] = {.0091, .0137, .0211, .0307};
static const float pan_position[4] = {-.72f, .64f, -.28f, .34f};
const char *ts_dark_matter_algorithm_name(int algorithm)
{
    static const char *names[]={"LEGACY MEMORY","TWO DELAYS","FOUR DELAYS","GIANT REVERB","GRANULAR"};
    return names[algorithm>=0 && algorithm<TS_DARK_ALGORITHM_COUNT?algorithm:0];
}
const char *ts_dark_matter_size_name(int size)
{
    static const char *names[]={"SMALL","MEDIUM","LARGE"};
    return names[size>=0 && size<3?size:0];
}
static float unit(float x) { return !isfinite(x) || x < 0 ? 0 : x > 1 ? 1 : x; }
static float bound(float x) { return !isfinite(x) ? 0 : x < -1 ? -1 : x > 1 ? 1 : x; }
static float normal(float x) { return !isfinite(x) || fabsf(x)<1e-20f ? 0 : x; }
static int prime(size_t n)
{
    if (n < 2 || !(n & 1)) return n == 2;
    for (size_t k = 3; k <= n / k; k += 2) if (!(n % k)) return 0;
    return 1;
}
static size_t prime_below(size_t n)
{
    if (n < 5) return 5;
    if (!(n & 1)) --n;
    while (!prime(n)) n -= 2;
    return n;
}
void ts_dark_matter_controls_default(TsDarkMatterControls *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->feedback = .92f; c->blur = .35f; c->drift = .35f;
    c->replace = .3f; c->drive = .08f; c->damp = .25f;
}
void ts_dark_matter_controls_sanitize(TsDarkMatterControls *c)
{
    if (!c) return;
    c->feedback = isfinite(c->feedback) ? fminf(1.05f, fmaxf(0, c->feedback)) : 0;
    c->blur=unit(c->blur); c->drift=unit(c->drift); c->replace=unit(c->replace);
    c->drive=unit(c->drive); c->damp=unit(c->damp);
    c->reverse=!!c->reverse; c->locks &= 1023u;
    if(c->algorithm<0 || c->algorithm>=TS_DARK_ALGORITHM_COUNT)c->algorithm=TS_DARK_LEGACY;
    if(c->size<0)c->size=0;
    if(c->size>2)c->size=2;
}
int ts_dark_matter_controls_read(TsDarkMatterControls *c, const char *text)
{
    TsDarkMatterControls v = {0}; char *end;
    if (!c || !text) return 0;
    float *fields[6]={&v.feedback,&v.blur,&v.drift,&v.replace,&v.drive,&v.damp};
    for(int i=0;i<6;++i) {
        errno=0;*fields[i]=strtof(text,&end);
        if(errno || end==text || !isfinite(*fields[i]) || *end!=',')return 0;
        text=end+1;
    }
    errno=0;long reverse=strtol(text,&end,10);
    if(errno || end==text || reverse<0 || reverse>1 || *end!=',')return 0;
    text=end+1;errno=0;unsigned long locks=strtoul(text,&end,10);
    if(errno || end==text || locks>1023)return 0;
    if(*end==',') {
        text=end+1;errno=0;long algorithm=strtol(text,&end,10);
        if(errno || end==text || algorithm<0 || algorithm>=TS_DARK_ALGORITHM_COUNT || *end!=',')return 0;
        text=end+1;errno=0;long size=strtol(text,&end,10);
        if(errno || end==text || size<0 || size>2)return 0;
        v.algorithm=(int)algorithm;v.size=(int)size;
    }
    while(isspace((unsigned char)*end))++end;
    if(*end)return 0;
    v.reverse=(int)reverse;v.locks=(unsigned)locks;
    ts_dark_matter_controls_sanitize(&v); *c=v; return 1;
}
int ts_dark_matter_controls_write(FILE *file, const char *key, const TsDarkMatterControls *c)
{
    return file && key && c && fprintf(file,"%s=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%u,%d,%d\n",
        key,c->feedback,c->blur,c->drift,c->replace,c->drive,c->damp,c->reverse,c->locks,c->algorithm,c->size)>0;
}
void ts_dark_matter_seed(TsDarkMatter *d, uint32_t seed)
{
    if (!d) return;
    for (int i=0;i<4;++i) d->bank[i].random=(seed?seed:1u) ^ (0x9e3779b9u*(unsigned)(i+1));
}
static void seam_geometry(TsDarkMatter *d, TsDarkMatterBank *b,
                          double *anchor, double *alternate, double *width)
{
    /* Pick the middle of the widest undamaged interval for the alternate
       moving read. No held samples and no changes to the remembered audio. */
    size_t sorted[TS_DARK_MATTER_SEAMS];
    unsigned count=0;
    for(unsigned j=0;j<b->seam_count;++j)if(b->seam[j]<b->length) {
        unsigned k=count++;
        while(k && sorted[k-1]>b->seam[j]){sorted[k]=sorted[k-1];--k;}
        sorted[k]=b->seam[j];
    }
    double gap=0,start=0;
    for(unsigned j=0;j<count;++j) {
        double next=j+1<count?(double)sorted[j+1]:(double)sorted[0]+b->length;
        if(next-sorted[j]>gap){gap=next-sorted[j];start=sorted[j];}
    }
    *anchor=start+gap*.25;*alternate=start+gap*.75;
    if(*anchor>=b->length)*anchor-=b->length;
    if(*alternate>=b->length)*alternate-=b->length;
    *width=fmax(1,fmin(.012*d->sample_rate,fmin(b->length/32.,gap*.125)));
}
static void add_seam(TsDarkMatter *d, TsDarkMatterBank *b, size_t position, float gain)
{
    if(position>=b->capacity)return;
    b->seam_writes_remaining=b->length;b->seam_releasing=0;
    for(unsigned j=0;j<b->seam_count;++j)if(b->seam[j]==position)return;
    if(b->seam_count==TS_DARK_MATTER_SEAMS) {
        memmove(b->seam,b->seam+1,(TS_DARK_MATTER_SEAMS-1)*sizeof(*b->seam));
        memmove(b->seam_gain,b->seam_gain+1,(TS_DARK_MATTER_SEAMS-1)*sizeof(*b->seam_gain));
        memmove(b->seam_anchor,b->seam_anchor+1,(TS_DARK_MATTER_SEAMS-1)*sizeof(*b->seam_anchor));
        memmove(b->seam_alternate,b->seam_alternate+1,(TS_DARK_MATTER_SEAMS-1)*sizeof(*b->seam_alternate));
        memmove(b->seam_width,b->seam_width+1,(TS_DARK_MATTER_SEAMS-1)*sizeof(*b->seam_width));
        --b->seam_count;
    }
    b->seam[b->seam_count]=position;b->seam_gain[b->seam_count++]=gain;
    seam_geometry(d,b,&b->seam_anchor[b->seam_count-1],
        &b->seam_alternate[b->seam_count-1],&b->seam_width[b->seam_count-1]);
}
static void apply_layout(TsDarkMatter *d)
{
    d->algorithm=d->controls.algorithm;d->size=d->controls.size;
    d->active_banks=d->target_banks;
    static const double grain_seconds[3]={.06,.18,.6};
    d->grain_frames=fmax(2,grain_seconds[d->size]*d->sample_rate);
    for(int i=0;i<4;++i) {
        TsDarkMatterBank *b=&d->bank[i];
        size_t old_length=b->length;
        b->length=d->target_length[i];b->write%=b->length;
        if(d->clock) {
            if(old_length!=b->length) {
                add_seam(d,b,0,1);
                add_seam(d,b,old_length,1);
            }
            if(b->frontier_valid)add_seam(d,b,b->frontier,1);
            /* The new writer may enter a different point in retained audio. */
            if(d->was_writing)add_seam(d,b,b->write,1);
            b->seam_writes_remaining=b->length;
            b->seam_releasing=0;
            double anchor,alternate,width;seam_geometry(d,b,&anchor,&alternate,&width);
            for(unsigned j=0;j<b->seam_count;++j) {
                b->seam_anchor[j]=anchor;b->seam_alternate[j]=alternate;b->seam_width[j]=width;
            }
        }
        b->grain_position[0]=(double)b->write-b->length*.35;
        b->grain_position[1]=(double)b->write-b->length*.65;
        b->grain_phase=i*.137;
        /* Diffusers and stored audio survive program changes. */
    }
    d->program_pending=0;
}
void ts_dark_matter_set(TsDarkMatter *d, const TsDarkMatterControls *c, float seconds)
{
    if (!d || !d->storage || !c) return;
    int old_algorithm=d->controls.algorithm,old_size=d->controls.size;
    d->controls=*c; ts_dark_matter_controls_sanitize(&d->controls);
    float cutoff=20000.f*powf(.01f,d->controls.damp);
    cutoff=fminf(cutoff,.45f*d->sample_rate);
    d->damp_target=1-expf(-6.28318530718f*cutoff/d->sample_rate);
    seconds=!isfinite(seconds)?40:fminf(60,fmaxf(5,seconds));
    if (seconds==d->seconds && old_algorithm==d->controls.algorithm && old_size==d->controls.size) return;
    d->seconds=seconds;
    static const float two[3]={2.5f,9.5f,22.f},four[3]={2.5f,8.5f,11.5f};
    static const float hall[3]={3.6f,8.4f,15.f},grain[3]={1.f,4.f,12.f};
    int algorithm=d->controls.algorithm,size=d->controls.size;
    d->target_banks=algorithm==TS_DARK_TWO_DELAYS?2:algorithm==TS_DARK_GRANULAR?(size==2?3:2):4;
    for (int i=0;i<4;++i) {
        TsDarkMatterBank *b=&d->bank[i];
        double duration=share[i]*seconds;
        if(algorithm==TS_DARK_TWO_DELAYS)duration=two[size]*(i==0?.46:.54);
        if(algorithm==TS_DARK_FOUR_DELAYS)duration=four[size]*share[i];
        if(algorithm==TS_DARK_GIANT_REVERB)duration=hall[size]*share[i];
        if(algorithm==TS_DARK_GRANULAR)duration=grain[size]*(i==0?.46:i==1?.54:.37);
        size_t n=prime_below((size_t)(duration*d->sample_rate));
        if (n>=b->capacity) n=prime_below(b->capacity-1);
        d->target_length[i]=n;
    }
    if(!d->clock)apply_layout(d);
    else {
        d->program_pending=algorithm!=d->algorithm || size!=d->size;
        for(int i=0;i<4;++i)if(d->bank[i].length!=d->target_length[i])d->program_pending=1;
    }
}
int ts_dark_matter_init(TsDarkMatter *d, uint32_t rate, float seconds)
{
    if (!d || rate<8 || rate>768000) return 0;
    memset(d,0,sizeof(*d)); d->sample_rate=rate;
    for (int i=0;i<4;++i) {
        d->bank[i].capacity=(size_t)ceil(share[i]*60*rate)+8;
        if (d->bank[i].capacity<16) d->bank[i].capacity=16;
        d->storage_frames+=d->bank[i].capacity;
    }
    d->storage=calloc(d->storage_frames,sizeof(*d->storage));
    if (!d->storage) return 0;
    size_t at=0;
    for (int i=0;i<4;++i) { d->bank[i].data=d->storage+at; at+=d->bank[i].capacity; }
    static const double delays[3]={.0137,.0233,.0419};
    static const double ratios[4]={.89,1.07,1.21,1.43};
    for(int i=0;i<4;++i)for(int j=0;j<3;++j) {
        d->diffuser[i][j].length=prime_below((size_t)(rate*delays[j]*ratios[i]));
        d->diffusion_frames+=d->diffuser[i][j].length;
    }
    d->diffusion_storage=calloc(d->diffusion_frames,sizeof(*d->diffusion_storage));
    if(!d->diffusion_storage){ts_dark_matter_free(d);return 0;}
    at=0;
    for(int i=0;i<4;++i)for(int j=0;j<3;++j) {
        d->diffuser[i][j].data=d->diffusion_storage+at;at+=d->diffuser[i][j].length;
    }
    d->fade_frames=(uint32_t)fmax(1,rate*.05);
    d->smoothing=1-expf(-1.f/(.02f*rate));
    d->attack=1-expf(-1.f/(.01f*rate)); d->release=1-expf(-1.f/(.5f*rate));
    d->dc_radius=expf(-6.28318530718f*5/rate);
    ts_dark_matter_controls_default(&d->controls);
    ts_dark_matter_set(d,&d->controls,seconds); ts_dark_matter_clear(d);
    return 1;
}
void ts_dark_matter_free(TsDarkMatter *d)
{ if (d) { free(d->storage);free(d->diffusion_storage); memset(d,0,sizeof(*d)); } }
void ts_dark_matter_clear(TsDarkMatter *d)
{
    if (!d || !d->storage) return;
    memset(d->storage,0,d->storage_frames*sizeof(*d->storage));
    if(d->diffusion_storage)memset(d->diffusion_storage,0,d->diffusion_frames*sizeof(*d->diffusion_storage));
    for(int i=0;i<4;++i)for(int j=0;j<3;++j)d->diffuser[i][j].write=0;
    apply_layout(d);d->program_gain=1;d->fade_position=d->fade_frames;d->was_writing=0;
    for (int i=0;i<4;++i) {
        TsDarkMatterBank *b=&d->bank[i];
        b->write=0; b->phase=i*.211; b->reverse_phase=i*.173;
        b->low=b->last=(TsStereoFrame){0,0};
        b->seam_count=0;b->seam_writes_remaining=0;b->frontier=0;b->frontier_valid=0;b->seam_releasing=0;
        b->dc_x=b->dc_y=(TsStereoFrame){0,0};
        b->chaos=b->chaos_target=b->pan=b->peak=0;
        b->grain_position[0]=-(double)b->length*.35;b->grain_position[1]=-(double)b->length*.65;b->grain_phase=i*.137;
    }
    d->smooth[0]=d->controls.feedback;d->smooth[1]=d->controls.blur;
    d->smooth[2]=d->controls.drift;d->smooth[3]=d->controls.replace;
    d->smooth[4]=d->controls.drive;d->smooth[5]=d->controls.damp;
    d->reverse_mix=(float)d->controls.reverse;d->clock=0;d->envelope=0;
    d->damp_coefficient=d->damp_target;
    d->dc_x=d->dc_y=(TsStereoFrame){0,0};ts_dark_matter_seed(d,0x81c4d31bu);
}
static TsStereoFrame read_raw(const TsDarkMatterBank *b, double position)
{
    while (position<0) position+=b->length;
    while (position>=b->length) position-=b->length;
    size_t a=(size_t)position,c=a+1==b->length?0:a+1;float f=(float)(position-a);
    return (TsStereoFrame){b->data[a].l+(b->data[c].l-b->data[a].l)*f,
                           b->data[a].r+(b->data[c].r-b->data[a].r)*f};
}
static float frontier_weight(const TsDarkMatterBank *b, double position, double width)
{
    if(!b->frontier_valid || b->frontier>=b->length)return 1;
    if(position<0)position+=b->length;
    if(position>=b->length)position-=b->length;
    double distance=fabs(position-b->frontier);
    if(distance>b->length*.5)distance=b->length-distance;
    float t=(float)fmin(1,distance/(2*width));
    return t*t*(3-2*t);
}
static TsStereoFrame read_bank(const TsDarkMatterBank *b, double position)
{
    while(position<0)position+=b->length;
    while(position>=b->length)position-=b->length;
    TsStereoFrame result=read_raw(b,position);
    for(unsigned j=0;j<b->seam_count;++j) {
        if(b->seam[j]>=b->length)continue;
        double distance=position-b->seam[j];
        if(distance>b->length*.5)distance-=b->length;
        if(distance<-(double)b->length*.5)distance+=b->length;
        if(fabs(distance)>=b->seam_width[j])continue;
        float t=(float)(fabs(distance)/b->seam_width[j]);
        float mix=(1-t*t*(3-2*t))*b->seam_gain[j];
        /* Either alternate can meet the moving write head during reverse or
           grain playback. Use two separated reads, fading away from that
           frontier so its old/new-data boundary cannot become another click. */
        double a=b->seam_anchor[j]+distance,z=b->seam_alternate[j]+distance;
        float wa=frontier_weight(b,a,b->seam_width[j]),wz=frontier_weight(b,z,b->seam_width[j]);
        float sum=wa+wz;
        if(sum<1e-6f){wa=wz=.5f;sum=1;} /* Degenerate low-rate stress layouts. */
        TsStereoFrame first=read_raw(b,a),second=read_raw(b,z);
        TsStereoFrame other={(first.l*wa+second.l*wz)/sum,(first.r*wa+second.r*wz)/sum};
        result.l+=(other.l-result.l)*mix;result.r+=(other.r-result.r)*mix;
    }
    return result;
}
static float random_signed(TsDarkMatterBank *b)
{
    uint32_t r=b->random;r^=r<<13;r^=r>>17;r^=r<<5;b->random=r;
    return (r>>8)*(2.f/16777215.f)-1;
}
static double grain_start(TsDarkMatter *d, TsDarkMatterBank *b)
{
    double age=.5*b->length + .3*b->length*d->smooth[1]*random_signed(b)
        + .15*b->length*d->smooth[2]*sinf((float)(6.28318530718*b->phase));
    return (double)b->write-age;
}
static TsStereoFrame granular_read(TsDarkMatter *d, TsDarkMatterBank *b)
{
    double p=b->grain_phase;
    float w=.5f-.5f*cosf((float)(6.28318530718*p));
    TsStereoFrame a=read_bank(b,b->grain_position[0]),z=read_bank(b,b->grain_position[1]);
    TsStereoFrame result={a.l*w+z.l*(1-w),a.r*w+z.r*(1-w)};
    double speed=1-2*d->reverse_mix;
    b->grain_position[0]+=speed;b->grain_position[1]+=speed;
    double next=p+1/d->grain_frames;
    if(p<.5 && next>=.5)b->grain_position[1]=grain_start(d,b);
    if(next>=1){next-=1;b->grain_position[0]=grain_start(d,b);}
    b->grain_phase=next;
    return result;
}
static TsStereoFrame diffuse(TsDarkMatter *d, int bank, TsStereoFrame input)
{
    for(int j=0;j<3;++j) {
        TsDarkMatterDiffuser *f=&d->diffuser[bank][j];
        TsStereoFrame delayed=f->data[f->write];
        /* Schroeder allpass: unity magnitude, energy spreads in time. */
        TsStereoFrame out={delayed.l-.6f*input.l,delayed.r-.6f*input.r};
        f->data[f->write]=(TsStereoFrame){normal(input.l+.6f*out.l),normal(input.r+.6f*out.r)};
        if(++f->write==f->length)f->write=0;
        input=out;
    }
    return input;
}
void ts_dark_matter_process(TsDarkMatter *d, TsStereoFrame input, int writing,
                           int mono, TsStereoFrame heads[3])
{
    if (!heads) return;
    memset(heads,0,3*sizeof(*heads));if (!d || !d->storage) return;
    /* Layout switches occur at silence. Repeated requests coalesce to the
       latest target without resetting or clearing any remembered audio. */
    if(d->program_pending) {
        if(d->fade_position)--d->fade_position;
        if(!d->fade_position)apply_layout(d);
    } else if(d->fade_position<d->fade_frames)++d->fade_position;
    float fade=(float)d->fade_position/d->fade_frames;
    d->program_gain=fade*fade*(3-2*fade);
    input.l=bound(input.l);input.r=mono?input.l:bound(input.r);
    TsStereoFrame clean={input.l-d->dc_x.l+d->dc_radius*d->dc_y.l,
                         input.r-d->dc_x.r+d->dc_radius*d->dc_y.r};
    clean.l=normal(clean.l);clean.r=normal(clean.r);
    d->dc_x=input;d->dc_y=clean;
    float target[6]={d->controls.feedback,d->controls.blur,d->controls.drift,
                    d->controls.replace,d->controls.drive,d->controls.damp};
    for (int j=0;j<6;++j) d->smooth[j]=normal(d->smooth[j]+d->smoothing*(target[j]-d->smooth[j]));
    d->reverse_mix+=d->smoothing*(d->controls.reverse-d->reverse_mix);
    if (!d->controls.reverse && d->reverse_mix<1e-5f) d->reverse_mix=0;
    d->damp_coefficient+=d->smoothing*(d->damp_target-d->damp_coefficient);
    float peak=fmaxf(fabsf(input.l),fabsf(input.r));
    d->envelope+=(peak>d->envelope?d->attack:d->release)*(peak-d->envelope);
    d->envelope=normal(d->envelope);
    float feedback=d->smooth[0]/(1+16*d->smooth[3]*d->envelope);
    float cross=.65f*d->smooth[1],drive=5*d->smooth[4];
    int count=d->active_banks,reverb=d->algorithm==TS_DARK_GIANT_REVERB;
    TsStereoFrame read[4]={{0,0}},sum={0,0};
    for (int i=0;i<4;++i) {
        TsDarkMatterBank *b=&d->bank[i];
        if(i>=count){b->peak=0;continue;}
        if(writing!=d->was_writing && b->frontier_valid)
            add_seam(d,b,writing?b->write:b->frontier,0);
        int any_seam=0;
        for(unsigned j=0;j<b->seam_count;++j) {
            if(b->seam[j]>=b->length)continue;
            float step=1.f/fmaxf(1,(b->seam_releasing?.05f:.02f)*d->sample_rate);
            b->seam_gain[j]=b->seam_releasing?fmaxf(0,b->seam_gain[j]-step):fminf(1,b->seam_gain[j]+step);
            if(b->seam_gain[j]>0)any_seam=1;
        }
        if(b->seam_releasing && !any_seam) {
            /* A short program has not overwritten dormant outer memory.
               Keep its splice history for a later expansion. */
            unsigned keep=0;
            for(unsigned j=0;j<b->seam_count;++j)if(b->seam[j]>=b->length) {
                b->seam[keep]=b->seam[j];b->seam_gain[keep]=b->seam_gain[j];
                b->seam_anchor[keep]=b->seam_anchor[j];b->seam_alternate[keep]=b->seam_alternate[j];
                b->seam_width[keep++]=b->seam_width[j];
            }
            b->seam_count=keep;b->seam_releasing=0;
        }
        if (!(d->clock%64)) {
            if (!(d->clock%((uint32_t)(d->sample_rate/64+1)*64))) {
                uint32_t r=b->random;r^=r<<13;r^=r>>17;r^=r<<5;b->random=r;
                b->chaos_target=(r>>8)*(2.f/16777215.f)-1;
            }
            b->chaos+=(b->chaos_target-b->chaos)*fminf(1,64.f/(5*d->sample_rate));
            b->phase+=64*rate_hz[i]*(1+.35*b->chaos)/d->sample_rate;
            if (b->phase>=1) b->phase-=1;
            b->pan=mono?0:(d->algorithm==TS_DARK_GRANULAR && i==2?0:pan_position[i])+
                (reverb || d->algorithm==TS_DARK_GRANULAR?0:.35f*d->smooth[2]*sinf((float)(6.28318530718*b->phase)));
            b->pan=fmaxf(-1,fminf(1,b->pan));
        }
        read[i]=d->algorithm==TS_DARK_GRANULAR?granular_read(d,b):read_bank(b,(double)b->write);
        /* Complementary reverse grains avoid a hard jump at each wrap. */
        if (d->algorithm!=TS_DARK_GRANULAR && d->reverse_mix>1e-5f) {
            double span=fmax(2,b->length*.5-2),p=b->reverse_phase;
            TsStereoFrame a=read_bank(b,(double)b->write-2-p*span);
            TsStereoFrame z=read_bank(b,(double)b->write-2-(p<.5?p+.5:p-.5)*span);
            float h=.5f-.5f*cosf((float)(6.28318530718*p));
            read[i].l+=d->reverse_mix*(a.l*h+z.l*(1-h)-read[i].l);
            read[i].r+=d->reverse_mix*(a.r*h+z.r*(1-h)-read[i].r);
        }
        b->reverse_phase+=2/fmax(2,b->length*.5-2);
        if (b->reverse_phase>=1) b->reverse_phase-=1;
        b->last=read[i];sum.l+=read[i].l;sum.r+=read[i].r;
        b->peak=fmaxf(fmaxf(fabsf(read[i].l),fabsf(read[i].r)),b->peak*.9995f);
        int h=i<2?i:2;
        heads[h].l+=read[i].l/count*(b->pan>0?1-b->pan:1);
        heads[h].r+=read[i].r/count*(b->pan<0?1+b->pan:1);
    }
    for (int i=0;i<count;++i) {
        TsDarkMatterBank *b=&d->bank[i];
        if (writing) {
            float exchange=d->algorithm==TS_DARK_GRANULAR?0:cross;
            TsStereoFrame old={(1-exchange)*read[i].l+exchange*(sum.l-read[i].l)/(count-1),
                               (1-exchange)*read[i].r+exchange*(sum.r-read[i].r)/(count-1)};
            /* An orthogonal feedback matrix produces recirculating hall
               reflections, rather than relabeling the delay mix as reverb. */
            if(reverb)old=(TsStereoFrame){.5f*sum.l-read[i].l,.5f*sum.r-read[i].r};
            b->low.l=normal(b->low.l+d->damp_coefficient*(old.l-b->low.l));
            b->low.r=normal(b->low.r+d->damp_coefficient*(old.r-b->low.r));
            /* Fade the write path as well as the speakers. Otherwise an
               abrupt topology change excites the reverb/damping histories
               and stores a click that returns after the audible fade. */
            float l=(clean.l+feedback*b->low.l)*d->program_gain;
            float r=(clean.r+feedback*b->low.r)*d->program_gain;
            if(reverb) {
                TsStereoFrame diffused=diffuse(d,i,(TsStereoFrame){l,r});
                l=diffused.l;r=diffused.r;
            }
            /* Unity small-signal gain: Drive cannot turn sub-unity feedback
               into an unintended growing loop. */
            l=l/(1+drive*fabsf(l));r=r/(1+drive*fabsf(r));
            TsStereoFrame dc={normal(l-b->dc_x.l+d->dc_radius*b->dc_y.l),
                               normal(r-b->dc_x.r+d->dc_radius*b->dc_y.r)};
            b->dc_x=(TsStereoFrame){l,r};b->dc_y=dc;l=dc.l;r=dc.r;
            l*=d->program_gain;r*=d->program_gain;
            b->data[b->write]=(TsStereoFrame){bound(l),mono?bound(l):bound(r)};
            b->frontier=b->write+1==b->length?0:b->write+1;b->frontier_valid=1;
            if(b->seam_writes_remaining && !--b->seam_writes_remaining)b->seam_releasing=1;
        }
        if (++b->write==b->length) b->write=0;
    }
    /* A bypassed hall keeps aging toward silence. Resuming a paused allpass
       clock would bring back a discontinuity in its old input history. */
    if(writing && !reverb)
        for(int i=0;i<4;++i)(void)diffuse(d,i,(TsStereoFrame){0,0});
    for(int i=0;i<3;++i){heads[i].l*=d->program_gain;heads[i].r*=d->program_gain;}
    d->was_writing=!!writing;
    ++d->clock;
}
