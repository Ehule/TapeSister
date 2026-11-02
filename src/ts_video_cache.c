/* One low-priority preparation worker in the visual child. MJPEG/AVI stores
   independently decodable frames: bounded seeks without external executables.
   Completed proxies are keyed by path + file size/mtime + format revision. */
#include "tapesister/video_cache.h"
#include "tapesister/browser.h"
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
struct TsVideoCache {
    SDL_Thread *thread;SDL_mutex *lock;atomic_uint stop,running;
    char source[1024];int pool;unsigned done,total,failed;
};
static enum AVPixelFormat video_scale_format(enum AVPixelFormat format)
{
    return format==AV_PIX_FMT_YUVJ420P?AV_PIX_FMT_YUV420P:format==AV_PIX_FMT_YUVJ422P?AV_PIX_FMT_YUV422P:format==AV_PIX_FMT_YUVJ444P?AV_PIX_FMT_YUV444P:format;
}
static int cache_name(const char *source,char *out,size_t n)
{
    struct stat st;if(!source || stat(source,&st) || !S_ISREG(st.st_mode))return 0;
    uint64_t hash=1469598103934665603ull;
    for(const unsigned char *p=(const unsigned char *)source;*p;++p){hash^=*p;hash*=1099511628211ull;}
    hash^=(uint64_t)st.st_size;hash*=1099511628211ull;hash^=(uint64_t)st.st_mtime;hash*=1099511628211ull;hash^=1;
    char *base=SDL_GetPrefPath("TapeSister","VideoCache");if(!base)return 0;
    int wrote=snprintf(out,n,"%s%016llx.avi",base,(unsigned long long)hash);SDL_free(base);
    return wrote>0 && (size_t)wrote<n;
}
int ts_video_cache_path(const char *source,char *out,size_t n)
{
    if(!cache_name(source,out,n))return 0;
    struct stat st;return !stat(out,&st) && S_ISREG(st.st_mode) && st.st_size>0;
}
static int cache_interrupt(void *opaque){return atomic_load(&((TsVideoCache *)opaque)->stop)!=0;}
static int cache_packets(AVCodecContext *encoder,AVFormatContext *output,AVPacket *packet)
{
    int result;
    while((result=avcodec_receive_packet(encoder,packet))>=0) {
        av_packet_rescale_ts(packet,encoder->time_base,output->streams[0]->time_base);packet->stream_index=0;
        result=av_interleaved_write_frame(output,packet);av_packet_unref(packet);if(result<0)return 0;
    }
    return result==AVERROR(EAGAIN) || result==AVERROR_EOF;
}
static int cache_prepare(TsVideoCache *cache,const char *path)
{
    char destination[2048],temporary[2112];if(ts_video_cache_path(path,destination,sizeof(destination)))return 1;
    if(!cache_name(path,destination,sizeof(destination)))return 0;
    snprintf(temporary,sizeof(temporary),"%s.%llu.part.avi",destination,(unsigned long long)SDL_GetPerformanceCounter());
    AVFormatContext *input=avformat_alloc_context(),*output=NULL;AVCodecContext *decoder=NULL,*encoder=NULL;
    AVPacket *packet=NULL,*encoded=NULL;AVFrame *frame=NULL,*scaled=NULL;struct SwsContext *scale=NULL;
    int ok=0,header=0,index=-1,draining=0;int64_t last_pts=-1;double first=NAN,next=0;
    if(!input)goto done;input->interrupt_callback=(AVIOInterruptCB){cache_interrupt,cache};input->probesize=2*1024*1024;input->max_analyze_duration=2*AV_TIME_BASE;
    AVDictionary *options=NULL;av_dict_set(&options,"protocol_whitelist","file",0);
    int opened=avformat_open_input(&input,path,NULL,&options);av_dict_free(&options);if(opened<0)goto done;
    if(avformat_find_stream_info(input,NULL)<0 || (index=av_find_best_stream(input,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0))<0)goto done;
    AVStream *stream=input->streams[index];
    if(stream->codecpar->width<=0 || stream->codecpar->height<=0 || stream->codecpar->width>4096 || stream->codecpar->height>4096)goto done;
    const AVCodec *codec=avcodec_find_decoder(stream->codecpar->codec_id),*mjpeg=avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if(!codec || !mjpeg || !(decoder=avcodec_alloc_context3(codec)) || !(encoder=avcodec_alloc_context3(mjpeg)))goto done;
    if(avcodec_parameters_to_context(decoder,stream->codecpar)<0)goto done;
    decoder->thread_count=1;decoder->max_pixels=4096*4096;if(avcodec_open2(decoder,codec,NULL)<0)goto done;
    double aspect=(double)decoder->width/decoder->height;
    AVRational sar=av_guess_sample_aspect_ratio(input,stream,NULL);if(sar.num>0 && sar.den>0)aspect*=av_q2d(sar);
    if(!isfinite(aspect) || aspect<.05 || aspect>20)goto done;
    int width=SDL_min(960,decoder->width),height=(int)(width/aspect);if(height>SDL_min(540,decoder->height)){height=SDL_min(540,decoder->height);width=(int)(height*aspect);}
    width=SDL_clamp(width&~1,2,960);height=SDL_clamp(height&~1,2,540);
    encoder->width=width;encoder->height=height;encoder->pix_fmt=AV_PIX_FMT_YUVJ420P;encoder->color_range=AVCOL_RANGE_JPEG;
    encoder->time_base=(AVRational){1,25};encoder->framerate=(AVRational){25,1};encoder->thread_count=1;
    encoder->flags|=AV_CODEC_FLAG_QSCALE;encoder->global_quality=FF_QP2LAMBDA*5;
    if(avcodec_open2(encoder,mjpeg,NULL)<0 || avformat_alloc_output_context2(&output,NULL,"avi",temporary)<0)goto done;
    AVStream *to=avformat_new_stream(output,NULL);if(!to || avcodec_parameters_from_context(to->codecpar,encoder)<0)goto done;
    to->time_base=encoder->time_base;
    if(avio_open(&output->pb,temporary,AVIO_FLAG_WRITE)<0 || avformat_write_header(output,NULL)<0)goto done;header=1;
    packet=av_packet_alloc();encoded=av_packet_alloc();frame=av_frame_alloc();scaled=av_frame_alloc();if(!packet || !encoded || !frame || !scaled)goto done;
    scaled->format=encoder->pix_fmt;scaled->width=width;scaled->height=height;scaled->quality=encoder->global_quality;
    if(av_frame_get_buffer(scaled,32)<0)goto done;
    double fallback=1.0/25;AVRational rate=av_guess_frame_rate(input,stream,NULL);if(rate.num>0 && rate.den>0)fallback=av_q2d(av_inv_q(rate));
    double previous=0;
    while(!atomic_load(&cache->stop)) {
        int result=avcodec_receive_frame(decoder,frame);
        if(result==AVERROR_EOF){ok=last_pts>=0;break;}
        if(result==AVERROR(EAGAIN)) {
            if(draining)goto done;
            for(;;) {
                if(atomic_load(&cache->stop))goto done;
                result=av_read_frame(input,packet);
                if(result<0){if(result!=AVERROR_EOF)goto done;avcodec_send_packet(decoder,NULL);draining=1;break;}
                if(packet->stream_index==index){result=avcodec_send_packet(decoder,packet);av_packet_unref(packet);if(result<0)goto done;break;}
                av_packet_unref(packet);
            }
            continue;
        }
        if(result<0 || frame->width>4096 || frame->height>4096)goto done;
        double pts=frame->best_effort_timestamp==AV_NOPTS_VALUE?(isnan(first)?0:previous+fallback):frame->best_effort_timestamp*av_q2d(stream->time_base);
        if(isnan(first))first=pts;previous=pts;double local=fmax(0,pts-first);
        if(!isfinite(local) || local>604800)goto done;
        if(local+.0001>=next) {
            int64_t out_pts=(int64_t)llround(local*25);if(out_pts<=last_pts)out_pts=last_pts+1;
            if(av_frame_make_writable(scaled)<0)goto done;
            scale=sws_getCachedContext(scale,frame->width,frame->height,video_scale_format((enum AVPixelFormat)frame->format),width,height,AV_PIX_FMT_YUV420P,SWS_FAST_BILINEAR,NULL,NULL,NULL);
            if(!scale)goto done;
            const int *coefficients=sws_getCoefficients(SWS_CS_DEFAULT);
            int full=frame->color_range==AVCOL_RANGE_JPEG || video_scale_format((enum AVPixelFormat)frame->format)!=(enum AVPixelFormat)frame->format;
            sws_setColorspaceDetails(scale,coefficients,full,coefficients,1,0,1<<16,1<<16);
            if(sws_scale(scale,(const uint8_t *const *)frame->data,frame->linesize,0,frame->height,scaled->data,scaled->linesize)!=height)goto done;
            scaled->pts=out_pts;if(avcodec_send_frame(encoder,scaled)<0 || !cache_packets(encoder,output,encoded))goto done;
            last_pts=out_pts;next=local+.04;
        }
        av_frame_unref(frame);
    }
    if(atomic_load(&cache->stop))ok=0;
    if(ok && (avcodec_send_frame(encoder,NULL)<0 || !cache_packets(encoder,output,encoded)))ok=0;
done:
    if(header && av_write_trailer(output)<0)ok=0;
    if(output && output->pb)avio_closep(&output->pb);avformat_free_context(output);
    sws_freeContext(scale);av_frame_free(&frame);av_frame_free(&scaled);av_packet_free(&packet);av_packet_free(&encoded);
    avcodec_free_context(&decoder);avcodec_free_context(&encoder);avformat_close_input(&input);
    if(ok && rename(temporary,destination))ok=0;
    if(!ok)remove(temporary);return ok;
}
static int cache_worker(void *opaque)
{
    TsVideoCache *cache=opaque;SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    TsBrowser *browser=NULL;unsigned count=1;
    if(cache->pool) {
        browser=calloc(1,sizeof(*browser));if(!browser)goto done;
        ts_browser_init(browser);browser->mode=TS_BROWSER_LOAD_VIDEO;snprintf(browser->directory,sizeof(browser->directory),"%s",cache->source);
        if(!ts_browser_refresh(browser))goto done;
        count=0;for(int i=0;i<browser->entry_count;++i)count+=!browser->entries[i].is_directory;
    }
    SDL_LockMutex(cache->lock);cache->total=count;SDL_UnlockMutex(cache->lock);
    for(unsigned i=0;i<(browser?(unsigned)browser->entry_count:1) && !atomic_load(&cache->stop);++i) {
        char path[2048];if(browser){if(browser->entries[i].is_directory)continue;int n=snprintf(path,sizeof(path),"%s/%s",cache->source,browser->entries[i].name);if(n<0 || n>=(int)sizeof(path))continue;}
        else snprintf(path,sizeof(path),"%s",cache->source);
        int ok=cache_prepare(cache,path);SDL_LockMutex(cache->lock);++cache->done;cache->failed+=!ok;SDL_UnlockMutex(cache->lock);
    }
done:
    free(browser);atomic_store(&cache->running,0);return 0;
}
TsVideoCache *ts_video_cache_create(void)
{
    TsVideoCache *cache=calloc(1,sizeof(*cache));if(!cache)return NULL;
    cache->lock=SDL_CreateMutex();if(!cache->lock){free(cache);return NULL;}atomic_init(&cache->stop,0);atomic_init(&cache->running,0);return cache;
}
void ts_video_cache_begin(TsVideoCache *cache,const char *path,int pool)
{
    if(!cache || !path || !*path || strlen(path)>=sizeof(cache->source) || (!strcmp(path,cache->source) && pool==cache->pool))return;
    atomic_store(&cache->stop,1);if(cache->thread)SDL_WaitThread(cache->thread,NULL);
    SDL_LockMutex(cache->lock);cache->done=cache->total=cache->failed=0;SDL_UnlockMutex(cache->lock);
    snprintf(cache->source,sizeof(cache->source),"%s",path);cache->pool=pool;atomic_store(&cache->stop,0);atomic_store(&cache->running,1);
    cache->thread=SDL_CreateThread(cache_worker,"video-preparation",cache);if(!cache->thread)atomic_store(&cache->running,0);
}
int ts_video_cache_status(TsVideoCache *cache,unsigned *done,unsigned *total,unsigned *failed)
{
    if(!cache)return 0;SDL_LockMutex(cache->lock);*done=cache->done;*total=cache->total;*failed=cache->failed;SDL_UnlockMutex(cache->lock);return atomic_load(&cache->running)!=0;
}
void ts_video_cache_free(TsVideoCache *cache)
{
    if(!cache)return;atomic_store(&cache->stop,1);if(cache->thread)SDL_WaitThread(cache->thread,NULL);SDL_DestroyMutex(cache->lock);free(cache);
}
