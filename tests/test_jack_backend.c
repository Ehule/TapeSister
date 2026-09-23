/* Native JACK ABI/lifecycle tests; --live additionally exercises a real server. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/router.h"
#include "tapesister/insert.h"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../src/main_sdl_jack.inc"
#if defined(__linux__)
static _Atomic unsigned called[2],heard;
static void audio(void *userdata,Uint8 *bytes,int size)
{
    int capture=(int)(intptr_t)userdata;
    float *samples=(float *)bytes;
    atomic_fetch_add(&called[capture],1);
    if (capture) {
        for (int i=0;i<size/(int)sizeof(float);i+=2)
            if (fabsf(samples[i]-.25f)<.001f && fabsf(samples[i+1]+.5f)<.001f)
                atomic_fetch_add(&heard,1);
    } else for (int i=0;i<size/(int)sizeof(float);++i) samples[i]=(i&1)?-.5f:.25f;
}
static SDL_AudioSpec desired(int capture)
{
    SDL_AudioSpec s;SDL_zero(s);s.freq=44100;s.samples=512;s.channels=2;
    s.format=AUDIO_F32SYS;s.callback=audio;s.userdata=(void *)(intptr_t)capture;return s;
}
#include "test_jack_router.inc"
static int live(void)
{
    ts_jack_selected=1;
    SDL_AudioSpec want=desired(0),send_spec,return_spec;
    SDL_AudioDeviceID send=ts_native_open(ts_jack_names[0][1],0,&want,&send_spec,0);
    if (!send) {fprintf(stderr,"JACK open: %s\n",SDL_GetError());return 1;}
    want=desired(1);
    SDL_AudioDeviceID ret=ts_native_open(ts_jack_names[1][1],1,&want,&return_spec,0);
    assert(ret && send_spec.freq==return_spec.freq && send_spec.samples==return_spec.samples);
    assert(send_spec.channels==2 && return_spec.channels==2);
    int (*connect_ports)(TsJackClient *,const char *,const char *);
    const char *(*port_name)(const TsJackPort *);
    void *symbol=SDL_LoadFunction(ts_jack_library,"jack_connect");memcpy(&connect_ports,&symbol,sizeof(symbol));
    symbol=SDL_LoadFunction(ts_jack_library,"jack_port_name");memcpy(&port_name,&symbol,sizeof(symbol));
    assert(connect_ports && port_name);
    TsJackEndpoint *se=ts_jack_endpoint(send),*re=ts_jack_endpoint(ret);
    for (int ch=0;ch<2;++ch)
        assert(!connect_ports(se->client,port_name(se->ports[ch]),port_name(re->ports[ch])));
    /* General I/O remains independently routable while Insert is connected. */
    SDL_AudioSpec master_spec,input_spec;want=desired(0);
    SDL_AudioDeviceID master=ts_native_open(ts_jack_names[0][0],0,&want,&master_spec,0);
    want=desired(1);SDL_AudioDeviceID input=ts_native_open(ts_jack_names[1][0],1,&want,&input_spec,0);
    assert(master && input && master_spec.channels==8 && input_spec.channels==8);
    TsJackEndpoint *me=ts_jack_endpoint(master),*ie=ts_jack_endpoint(input);
    for (int ch=0;ch<8;++ch)
        assert(!connect_ports(me->client,port_name(me->ports[ch]),port_name(ie->ports[ch])));
    ts_native_pause(input,0);ts_native_pause(master,0);
    ts_native_pause(ret,0);ts_native_pause(send,0);
    for (int i=0;i<200 && atomic_load(&heard)<4096;++i) SDL_Delay(10);
    assert(atomic_load(&heard)>=4096);
    ts_native_lock(send);unsigned before=atomic_load(&se->write_block);SDL_Delay(30);
    assert(atomic_load(&se->write_block)==before);ts_native_unlock(send);
    SDL_Delay(30);assert(atomic_load(&se->write_block)>before);
    ts_native_pause(send,1);before=atomic_load(&se->write_block);SDL_Delay(30);
    assert(atomic_load(&se->write_block)==before);
    assert(atomic_load(&ie->read_block)>0 && atomic_load(&re->read_block)>0);
    live_performance(master,send,ret);
    ts_native_close(send);ts_native_close(ret);ts_native_close(master);ts_native_close(input);
    printf("Live JACK stereo SEND -> RETURN passed at %d Hz / %u frames\n",send_spec.freq,send_spec.samples);
    return 0;
}
static float buffers[64][256];static unsigned nports,nclients,closed;
static TsJackClient *mock_open(const char *name,unsigned options,unsigned *status,...)
{assert(strstr(name,"TapeSister") && options==1);*status=0;return (TsJackClient *)(uintptr_t)++nclients;}
static int mock_close(TsJackClient *c){assert(c);++closed;return 0;}
static int mock_active(TsJackClient *c){assert(c);return 0;}
static TsJackFrames mock_rate(TsJackClient *c){assert(c);return 48000;}
static TsJackFrames mock_size(TsJackClient *c){assert(c);return 128;}
static TsJackPort *mock_port(TsJackClient *c,const char *name,const char *type,unsigned long flags,unsigned long size)
{assert(c && name && !strcmp(type,"32 bit float mono audio") && (flags==1 || flags==2) && !size);return (TsJackPort *)(uintptr_t)++nports;}
static void *mock_buffer(TsJackPort *p,TsJackFrames frames)
{assert(frames<=256);return buffers[(uintptr_t)p-1];}
static int mock_process(TsJackClient *c,int (*cb)(TsJackFrames,void *),void *u)
{assert(c && cb && u);return 0;}
static void mock_shutdown(TsJackClient *c,void (*cb)(void *),void *u){assert(c && cb && u);}
static void mocked(void)
{
    ts_jack_library=(void *)1;ts_jack_selected=1;
    ts_jack=(TsJackApi){mock_open,mock_close,mock_active,mock_active,mock_rate,mock_size,
        mock_port,mock_buffer,mock_process,mock_process,mock_process,mock_shutdown};
    assert(ts_native_device_count(0)==2 && ts_native_device_count(1)==2);
    assert(!strcmp(ts_native_current_driver(),"jack"));
    assert(!strcmp(ts_native_device_name(1,1),"TapeSister Insert Return"));
    SDL_AudioSpec spec;char *name=NULL;
    assert(!ts_native_default_info(&name,&spec,0) && spec.channels==8);SDL_free(name);
    SDL_AudioSpec want=desired(0),got;
    assert(!ts_native_open("Missing device",0,&want,&got,0));
    SDL_AudioDeviceID send=ts_native_open(ts_jack_names[0][1],0,&want,&got,0);
    assert(send && got.freq==48000 && got.samples==128 && got.channels==2);
    TsJackEndpoint *e=ts_jack_endpoint(send);
    assert(!ts_jack_process(128,e) && !atomic_load(&called[0]));
    ts_native_pause(send,0);
    for (int i=0;i<100 && !atomic_load(&e->write_block);++i) SDL_Delay(1);
    ts_native_lock(send);unsigned before=atomic_load(&called[0]);
    ts_jack_process(128,e);
    assert(buffers[0][0]==.25f && buffers[1][127]==-.5f);
    assert(ts_native_worker_xruns(send)==0);
    assert(atomic_load(&called[0])==before);ts_native_unlock(send);
    want=desired(1);SDL_AudioDeviceID ret=ts_native_open(ts_jack_names[1][1],1,&want,&got,0);
    TsJackEndpoint *r=ts_jack_endpoint(ret);assert(ret && got.channels==2);
    for (int i=0;i<128;++i) {buffers[2][i]=.25f;buffers[3][i]=-.5f;}
    ts_native_pause(ret,0);ts_jack_process(128,r);
    for (int i=0;i<100 && atomic_load(&heard)<128;++i) SDL_Delay(1);
    assert(atomic_load(&heard)==128);
    ts_native_lock(send);before=atomic_load(&called[0]);
    ts_jack_rate_changed(96000,e);ts_jack_process(128,e);
    assert(atomic_load(&called[0])==before && buffers[0][0]==0);ts_native_unlock(send);
    ts_jack_poll();SDL_Event ev;int removed=0;
    while (SDL_PollEvent(&ev)) if (ev.type==SDL_AUDIODEVICEREMOVED) {assert(ev.adevice.which==send);++removed;}
    assert(removed==1);ts_jack_poll();assert(!SDL_PollEvent(&ev));
    ts_jack_buffer_changed(256,r);assert(atomic_load(&r->lost));
    ts_jack_shutdown(r);assert(atomic_load(&r->lost));
    ts_native_close(send);ts_native_close(ret);assert(closed==2);
    ts_jack_library=NULL;memset(&ts_jack,0,sizeof(ts_jack));ts_jack_selected=0;
    puts("JACK endpoints: enumeration, negotiated timing, stereo ports, pause/lock, server changes and close passed");
}
#endif
int main(int argc,char **argv)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_AUDIODRIVER,"dummy");assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_EVENTS));
#if defined(__linux__)
    if (argc>1 && !strcmp(argv[1],"--live")) return live();
    mocked();
#else
    (void)argc;(void)argv;
#endif
    SDL_Quit();return 0;
}
