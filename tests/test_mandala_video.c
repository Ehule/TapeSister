#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#include "tapesister/mandala_video.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static TsVideoStatus status;
static unsigned poll_video(TsVideo *video,unsigned ms)
{
    Uint64 start=SDL_GetTicks64();unsigned updates=0;
    do {updates+=ts_video_upload(video,renderer,&texture,&status);SDL_Delay(5);}while(SDL_GetTicks64()-start<ms);
    return updates;
}
static void video_pixels(uint32_t pixels[100*100])
{
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    assert(!SDL_RenderCopy(renderer,texture,NULL,NULL));
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,100*4));
}
static void palette_checks(TsVideo *video)
{
    uint32_t original[100*100],filtered[100*100],again[100*100],colors[8];
    unsigned held=status.position_ms;video_pixels(original);
    for(int i=0;i<8;++i)colors[i]=0xC08040;
    ts_video_palette(video,colors,100);assert(poll_video(video,25)==1);video_pixels(filtered);
    for(int i=0;i<100*100;++i)assert((filtered[i]&0xffffff)==0xC08040);
    assert(status.position_ms==held); /* Recolor a paused frame without advancing it. */
    ts_video_palette(video,colors,50);assert(poll_video(video,25)==1);video_pixels(filtered);
    for(int i=0;i<100*100;++i)for(int shift=0;shift<=16;shift+=8) {
        int expected=(((original[i]>>shift)&255)+((0xC08040>>shift)&255)+1)/2;
        assert(abs((int)((filtered[i]>>shift)&255)-expected)<=1);
    }
    ts_video_palette(video,colors,0);assert(poll_video(video,25)==1);video_pixels(again);
    assert(!memcmp(original,again,sizeof(original))); /* Always remap the raw source. */
    for(int i=0;i<8;++i){unsigned gray=(255*(7-i)+3)/7;colors[i]=gray*0x010101;}
    ts_video_palette(video,colors,100);assert(poll_video(video,25)==1);video_pixels(filtered);
    for(int i=0;i<100*100;++i) {
        unsigned rgb=original[i],luma=(77*((rgb>>16)&255)+150*((rgb>>8)&255)+29*(rgb&255))>>8;
        for(int shift=0;shift<=16;shift+=8)assert(abs((int)((filtered[i]>>shift)&255)-(int)luma)<=2);
    } /* Unsorted custom swatches still preserve continuous light/dark detail. */
    for(int i=0;i<8;++i)colors[i]=0x204080;
    ts_video_palette(video,colors,100);assert(poll_video(video,25)==1);video_pixels(filtered);
    for(int i=0;i<100*100;++i)assert((filtered[i]&0xffffff)==0x204080);
    ts_video_palette(video,colors,100);assert(!poll_video(video,25)); /* Cached, not reprocessed every tick. */
    SDL_DestroyTexture(texture);texture=NULL;assert(poll_video(video,25)==1);video_pixels(again);
    assert(!memcmp(filtered,again,sizeof(filtered))); /* Device-reset restoration while paused. */
    ts_video_palette(video,colors,0);assert(poll_video(video,25)==1);
    assert(status.position_ms==held);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("Video fixture",0,0,100,100,0);assert(window);
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    TsVideo *video=ts_video_open(argv[1]);assert(video);ts_video_control(video,0,0,0,15);
    Uint64 start=SDL_GetTicks64();
    while(!texture && SDL_GetTicks64()-start<8000)poll_video(video,20);
    assert(texture && status.state!=TS_VIDEO_ERROR);
    int width,height;SDL_QueryTexture(texture,NULL,NULL,&width,&height);
    assert(width<=640 && height<=400 && width*2==height*3); /* source aspect retained */
    assert(poll_video(video,350)>0);unsigned held=status.position_ms;
    ts_video_control(video,1,0,0,15);
    assert(!poll_video(video,250) && status.position_ms==held);
    palette_checks(video);
    ts_video_control(video,0,0,0,15);assert(poll_video(video,250)>0);
    assert(status.position_ms>=held && status.position_ms-held<500); /* no catch-up jump */
    poll_video(video,1800);assert(status.state==TS_VIDEO_ENDED && !status.loops);
    held=status.position_ms;assert(!poll_video(video,200) && status.position_ms==held);
    ts_video_control(video,0,1,0,15);assert(poll_video(video,300)>0 && status.loops==1);
    ts_video_control(video,0,0,1,30);poll_video(video,250);assert(status.position_ms<500);
    /* Stalled consumer has bounded storage and resumes with current frames. */
    SDL_Delay(450);assert(poll_video(video,250)>0 && status.state!=TS_VIDEO_ERROR);
    ts_video_close(video);SDL_DestroyTexture(texture);texture=NULL;
    video=ts_video_open("no-such-video-file.mp4");assert(video);
    poll_video(video,300);assert(status.state==TS_VIDEO_ERROR && !texture && status.message[0]);ts_video_close(video);
    for(int i=0;i<8;++i){video=ts_video_open(argv[1]);assert(video);ts_video_close(video);}
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    puts("video: decode, pacing, pause, palette blend/custom edits, raw restore, texture reset, EOF, loop, restart, backpressure and teardown PASS");return 0;
}
