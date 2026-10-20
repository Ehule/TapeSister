#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#include "tapesister/mandala_video.h"
#include <assert.h>
#include <stdio.h>

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static TsVideoStatus status;
static unsigned poll_video(TsVideo *video,unsigned ms)
{
    Uint64 start=SDL_GetTicks64();unsigned updates=0;
    do {updates+=ts_video_upload(video,renderer,&texture,&status);SDL_Delay(5);}while(SDL_GetTicks64()-start<ms);
    return updates;
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
    puts("video: H.264 B-frames, muted audio track, pacing, pause, EOF, loop, restart, backpressure, missing file and teardown PASS");return 0;
}
