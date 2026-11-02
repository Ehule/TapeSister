#include "tapesister/mandala_video.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Three scaled frames; codec reference frames are bounded by the input limits.
   Worker owns decoder and conversion. Renderer owns textures. Both may stall
   without touching the host's audio thread or its queues. */
enum { V_WIDTH=960, V_HEIGHT=540, V_SLOTS=3 };
typedef struct { uint8_t pixels[V_WIDTH*V_HEIGHT*4]; int width,height; unsigned ms,restart; } VFrame;
struct TsVideo {
    SDL_Thread *thread; SDL_mutex *status_lock;
    atomic_uint stop, paused, loop, restart, fps, write, read;
    TsVideoStatus status;
    char path[1024]; double fraction; VFrame frame[V_SLOTS];
    /* Renderer-only cache: never refilter an already filtered frame, and never
       hold a producer slot while uploading/recoloring a paused image. */
    VFrame current;
    uint32_t palette[8], ramp[256]; unsigned strength;
    int palette_ready, upload_dirty;
};
static void status_set(TsVideo *v,unsigned state,const char *message)
{
    SDL_LockMutex(v->status_lock);v->status.state=state;
    snprintf(v->status.message,sizeof(v->status.message),"%s",message);
    SDL_UnlockMutex(v->status_lock);
}
static enum AVPixelFormat video_scale_format(enum AVPixelFormat format)
{
    return format==AV_PIX_FMT_YUVJ420P?AV_PIX_FMT_YUV420P:format==AV_PIX_FMT_YUVJ422P?AV_PIX_FMT_YUV422P:format==AV_PIX_FMT_YUVJ444P?AV_PIX_FMT_YUV444P:format;
}
static int interrupt_io(void *p){return atomic_load(&((TsVideo*)p)->stop)!=0;}
static int decode(void *opaque)
{
    TsVideo *v=opaque;AVFormatContext *format=avformat_alloc_context();
    AVCodecContext *codec=NULL;AVPacket *packet=NULL;AVFrame *frame=NULL;struct SwsContext *sws=NULL;
    int result=-1,index=-1,draining=0,eof=0,frames=0;double first=NAN,last=0,fallback=1.0/25,epoch=0,next_upload=0;
    unsigned restart=atomic_load(&v->restart);Uint64 origin=SDL_GetTicks64(),paused_at=0;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    if(!format)goto done;
    format->interrupt_callback=(AVIOInterruptCB){interrupt_io,v};
    format->probesize=2*1024*1024;format->max_analyze_duration=2*AV_TIME_BASE;
    AVDictionary *options=NULL;
    /* Local media only; playlists must never trigger network access. */
    av_dict_set(&options,"protocol_whitelist","file",0);
    result=avformat_open_input(&format,v->path,NULL,&options);av_dict_free(&options);
    if(result<0)goto done;
    if((result=avformat_find_stream_info(format,NULL))<0)goto done;
    index=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    if(index<0){result=index;goto done;}
    AVStream *stream=format->streams[index];
    if(stream->codecpar->width<=0 || stream->codecpar->height<=0 || stream->codecpar->width>4096 || stream->codecpar->height>2160) {
        status_set(v,TS_VIDEO_ERROR,"VIDEO TOO LARGE: USE A COPY AT 1080P OR LOWER");goto cleanup;
    }
    const AVCodec *decoder=avcodec_find_decoder(stream->codecpar->codec_id);
    if(!decoder){status_set(v,TS_VIDEO_ERROR,"UNSUPPORTED VIDEO CODEC: TRY H.264 MP4");goto cleanup;}
    codec=avcodec_alloc_context3(decoder);if(!codec)goto done;
    if((result=avcodec_parameters_to_context(codec,stream->codecpar))<0)goto done;
    codec->thread_count=1;codec->max_pixels=4096*2160;
    if((result=avcodec_open2(codec,decoder,NULL))<0)goto done;
    packet=av_packet_alloc();frame=av_frame_alloc();if(!packet||!frame)goto done;
    AVRational rate=av_guess_frame_rate(format,stream,NULL);
    if(rate.num>0 && rate.den>0)fallback=av_q2d(av_inv_q(rate));
    fallback=fmax(.001,fmin(1,fallback));
    SDL_LockMutex(v->status_lock);
    int64_t duration=stream->duration;
    double seconds=duration!=AV_NOPTS_VALUE?duration*av_q2d(stream->time_base):format->duration/(double)AV_TIME_BASE;
    if(seconds>0 && seconds<4294967)v->status.duration_ms=(unsigned)(seconds*1000);
    SDL_UnlockMutex(v->status_lock);
    double seek_target=0;
    if(v->fraction>0 && isfinite(seconds) && seconds>.1 && seconds<4294967) {
        seek_target=fmax(0,seconds-.1)*v->fraction;
        int64_t start=stream->start_time==AV_NOPTS_VALUE?0:stream->start_time;
        if(av_seek_frame(format,index,start+(int64_t)(seek_target/av_q2d(stream->time_base)),AVSEEK_FLAG_BACKWARD)<0)seek_target=0;
        else avcodec_flush_buffers(codec);
    }
    double position_origin=seek_target;
    status_set(v,TS_VIDEO_PLAYING,"PLAYING / VIDEO SOUND OFF");origin=SDL_GetTicks64();
    while(!atomic_load(&v->stop)) {
        Uint64 now=SDL_GetTicks64();
        unsigned request=atomic_load(&v->restart);
        if(request!=restart || eof) {
            if(eof && request==restart && (!atomic_load(&v->loop) || atomic_load(&v->paused))) {
                if(!paused_at)paused_at=now;
                SDL_Delay(10);continue;
            }
            if(paused_at){origin+=now-paused_at;paused_at=0;}
            int64_t start=stream->start_time==AV_NOPTS_VALUE?0:stream->start_time;
            if(av_seek_frame(format,index,start,AVSEEK_FLAG_BACKWARD)<0) {
                status_set(v,TS_VIDEO_ERROR,"THIS VIDEO CANNOT RESTART / REPLACE FILE");break;
            }
            avcodec_flush_buffers(codec);av_packet_unref(packet);av_frame_unref(frame);
            if(request!=restart){epoch=0;origin=now;restart=request;}
            else {
                epoch+=fmax(last+fallback,.001);
                SDL_LockMutex(v->status_lock);v->status.loops++;SDL_UnlockMutex(v->status_lock);
            }
            seek_target=position_origin=0;first=NAN;last=0;next_upload=0;draining=eof=frames=0;paused_at=0;
            status_set(v,TS_VIDEO_PLAYING,"PLAYING / VIDEO SOUND OFF");
        }
        if(atomic_load(&v->paused)) {if(!paused_at)paused_at=now;SDL_Delay(10);continue;}
        if(paused_at){origin+=now-paused_at;paused_at=0;}
        result=avcodec_receive_frame(codec,frame);
        if(result==AVERROR_EOF) {if(!frames){result=AVERROR_INVALIDDATA;goto done;}eof=1;status_set(v,TS_VIDEO_ENDED,"END / RESTART OR ENABLE LOOP");continue;}
        if(result==AVERROR(EAGAIN)) {
            if(draining){result=AVERROR_INVALIDDATA;goto done;}
            int got=0;
            while(!got && !atomic_load(&v->stop)) {
                result=av_read_frame(format,packet);
                if(result<0){if(result!=AVERROR_EOF)goto done;avcodec_send_packet(codec,NULL);draining=1;break;}
                if(packet->stream_index==index){result=avcodec_send_packet(codec,packet);got=1;}
                av_packet_unref(packet);if(got && result<0)goto done;
            }
            continue;
        }
        if(result<0)goto done;
        ++frames;
        double pts=frame->best_effort_timestamp==AV_NOPTS_VALUE?(isnan(first)?0:last+fallback):frame->best_effort_timestamp*av_q2d(stream->time_base);
        double absolute=pts-(stream->start_time==AV_NOPTS_VALUE?0:stream->start_time*av_q2d(stream->time_base));
        if(seek_target>0 && absolute+fallback*.5<seek_target){av_frame_unref(frame);continue;}
        if(isnan(first)){first=pts;position_origin=fmax(0,absolute);if(seek_target>0){origin=SDL_GetTicks64();seek_target=0;}}
        double local=fmax(0,pts-first);if(frame->best_effort_timestamp==AV_NOPTS_VALUE)local=pts;
        last=local;
        /* Pace by source timestamps, independent of visual frame rate. */
        for(;;) {
            if(atomic_load(&v->stop) || atomic_load(&v->restart)!=restart)break;
            now=SDL_GetTicks64();
            if(atomic_load(&v->paused)){if(!paused_at)paused_at=now;SDL_Delay(10);continue;}
            if(paused_at){origin+=now-paused_at;paused_at=0;}
            if((now-origin)/1000.0>=epoch+local)break;
            SDL_Delay(5);
        }
        if(atomic_load(&v->stop))break;
        if(atomic_load(&v->restart)!=restart){av_frame_unref(frame);continue;}
        double lateness=(SDL_GetTicks64()-origin)/1000.0-(epoch+local);
        unsigned w=atomic_load_explicit(&v->write,memory_order_relaxed);
        unsigned r=atomic_load_explicit(&v->read,memory_order_acquire);
        if(local+.0001>=next_upload && lateness<.25 && w-r<V_SLOTS) {
            if(frame->width<=0 || frame->height<=0 || frame->width>4096 || frame->height>2160) {status_set(v,TS_VIDEO_ERROR,"VIDEO FRAME EXCEEDS SIZE LIMIT");break;}
            AVRational sar=av_guess_sample_aspect_ratio(format,stream,frame);
            double aspect=frame->width*(sar.num>0 && sar.den>0?av_q2d(sar):1)/frame->height;
            if(!isfinite(aspect)||aspect<.05||aspect>20)aspect=(double)frame->width/frame->height;
            int width=SDL_min(V_WIDTH,frame->width),height=(int)(width/aspect);
            if(height>V_HEIGHT){height=V_HEIGHT;width=(int)(height*aspect);}
            width=SDL_clamp(width,2,V_WIDTH);height=SDL_clamp(height,2,V_HEIGHT);
            VFrame *out=&v->frame[w%V_SLOTS];out->width=width;out->height=height;out->ms=(unsigned)fmin((position_origin+local)*1000,4294967295.0);out->restart=restart;
            sws=sws_getCachedContext(sws,frame->width,frame->height,video_scale_format((enum AVPixelFormat)frame->format),width,height,AV_PIX_FMT_BGRA,SWS_FAST_BILINEAR,NULL,NULL,NULL);
            if(!sws){result=AVERROR(ENOMEM);goto done;}
            const int *coefficients=sws_getCoefficients(SWS_CS_DEFAULT);
            int full=frame->color_range==AVCOL_RANGE_JPEG || video_scale_format((enum AVPixelFormat)frame->format)!=(enum AVPixelFormat)frame->format;
            sws_setColorspaceDetails(sws,coefficients,full,coefficients,1,0,1<<16,1<<16);
            uint8_t *dest[4]={out->pixels,NULL,NULL,NULL};int stride[4]={width*4,0,0,0};
            if(sws_scale(sws,(const uint8_t*const*)frame->data,frame->linesize,0,frame->height,dest,stride)!=height){result=AVERROR_INVALIDDATA;goto done;}
            atomic_store_explicit(&v->write,w+1,memory_order_release);
            unsigned fps=atomic_load(&v->fps);next_upload=local+1.0/SDL_clamp(fps,8u,30u);
        }
        av_frame_unref(frame);
    }
    goto cleanup;
done:
    if(!atomic_load(&v->stop)) {
        char reason[AV_ERROR_MAX_STRING_SIZE],message[96];av_strerror(result,reason,sizeof(reason));
        snprintf(message,sizeof(message),"VIDEO: %.85s",reason);status_set(v,TS_VIDEO_ERROR,message);
    }
cleanup:
    sws_freeContext(sws);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);avformat_close_input(&format);
    return 0;
}
TsVideo *ts_video_open_at(const char *path,double fraction)
{
    if(!path || !*path || strlen(path)>=1024)return NULL;
    TsVideo *v=calloc(1,sizeof(*v));if(!v)return NULL;
    snprintf(v->path,sizeof(v->path),"%s",path);v->fraction=isfinite(fraction)?fmax(0,fmin(.999999,fraction)):0;
    atomic_init(&v->stop,0);atomic_init(&v->paused,0);atomic_init(&v->loop,1);atomic_init(&v->restart,0);atomic_init(&v->fps,15);atomic_init(&v->write,0);atomic_init(&v->read,0);
    v->status_lock=SDL_CreateMutex();if(!v->status_lock){free(v);return NULL;}
    status_set(v,TS_VIDEO_OPENING,"OPENING VIDEO...");v->thread=SDL_CreateThread(decode,"mandala-video",v);
    if(!v->thread){SDL_DestroyMutex(v->status_lock);free(v);return NULL;}
    return v;
}
TsVideo *ts_video_open(const char *path){return ts_video_open_at(path,0);}
void ts_video_control(TsVideo *v,int paused,int loop,unsigned restart,unsigned fps)
{
    if(!v)return;
    atomic_store(&v->paused,paused!=0);atomic_store(&v->loop,loop!=0);atomic_store(&v->restart,restart);atomic_store(&v->fps,fps);
}
static unsigned video_luma(uint32_t rgb)
{return (77*((rgb>>16)&255)+150*((rgb>>8)&255)+29*(rgb&255))>>8;}
void ts_video_palette(TsVideo *v,const uint32_t colors[8],unsigned strength)
{
    if(!v || !colors)return;
    strength=SDL_min(strength,100u);
    if(v->palette_ready && v->strength==strength && !memcmp(v->palette,colors,sizeof(v->palette)))return;
    memcpy(v->palette,colors,sizeof(v->palette));v->strength=strength;v->palette_ready=1;v->upload_dirty=1;
    /* Continuous luminance gradient, darkest to lightest. No posterization or
       per-pixel color search; custom swatches participate exactly like presets. */
    uint32_t sorted[8];memcpy(sorted,colors,sizeof(sorted));
    for(int i=1;i<8;++i){uint32_t color=sorted[i];int j=i;while(j>0 && video_luma(sorted[j-1])>video_luma(color)){sorted[j]=sorted[j-1];--j;}sorted[j]=color;}
    for(unsigned y=0;y<256;++y) {
        unsigned at=y*7,lo=at/255,blend=at%255;uint32_t a=sorted[lo],b=sorted[SDL_min(lo+1,7u)],rgb=0;
        for(unsigned shift=0;shift<=16;shift+=8)rgb|=(((((a>>shift)&255)*(255-blend)+((b>>shift)&255)*blend)+127)/255)<<shift;
        v->ramp[y]=rgb;
    }
}
int ts_video_upload(TsVideo *v,SDL_Renderer *renderer,SDL_Texture **texture,TsVideoStatus *status)
{
    if(!v)return 0;
    if(!SDL_TryLockMutex(v->status_lock)){*status=v->status;SDL_UnlockMutex(v->status_lock);}
    unsigned r=atomic_load_explicit(&v->read,memory_order_relaxed),w=atomic_load_explicit(&v->write,memory_order_acquire);
    if(w!=r && !atomic_load(&v->paused)) {
        VFrame *next=&v->frame[(w-1)%V_SLOTS];
        if(next->restart==atomic_load(&v->restart)) {
            v->current.width=next->width;v->current.height=next->height;v->current.ms=next->ms;v->current.restart=next->restart;
            memcpy(v->current.pixels,next->pixels,(size_t)next->width*next->height*4);v->upload_dirty=1;
        }
        atomic_store_explicit(&v->read,w,memory_order_release);
    }
    VFrame *f=&v->current;int width=0,height=0;
    if(!f->width || (!v->upload_dirty && *texture))return 0;
    if(*texture)SDL_QueryTexture(*texture,NULL,NULL,&width,&height);
    if(width!=f->width || height!=f->height){SDL_DestroyTexture(*texture);*texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,f->width,f->height);}
    int uploaded=0;void *pixels;int pitch;
    if(*texture && !SDL_LockTexture(*texture,NULL,&pixels,&pitch)) {
        for(int y=0;y<f->height;++y) {
            const uint8_t *src=f->pixels+y*f->width*4;uint8_t *dst=(uint8_t*)pixels+y*pitch;
            if(!v->strength)memcpy(dst,src,f->width*4);
            else for(int x=0;x<f->width;++x,src+=4,dst+=4) {
                uint32_t mapped=v->ramp[(77*src[2]+150*src[1]+29*src[0])>>8];
                for(int c=0;c<3;++c)dst[c]=(uint8_t)((src[c]*(100-v->strength)+((mapped>>(c*8))&255)*v->strength+50)/100);
                dst[3]=src[3];
            }
        }
        SDL_UnlockTexture(*texture);SDL_SetTextureBlendMode(*texture,SDL_BLENDMODE_BLEND);uploaded=1;
        v->upload_dirty=0;
        status->position_ms=f->ms;
        if(!SDL_TryLockMutex(v->status_lock)){v->status.position_ms=f->ms;SDL_UnlockMutex(v->status_lock);}
    }
    return uploaded;
}
void ts_video_close(TsVideo *v)
{
    if(!v)return;
    atomic_store(&v->stop,1);SDL_WaitThread(v->thread,NULL);SDL_DestroyMutex(v->status_lock);free(v);
}
