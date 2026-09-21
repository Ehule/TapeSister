#define SDL_MAIN_HANDLED
#include "tapesister/asio_backend.h"
#include "RtAudio.h"
#include <cassert>
#include <cstdio>
#include <cstring>
RtAudio *RtAudio::instance;
unsigned RtAudio::opens,RtAudio::closes,RtAudio::probes;
bool RtAudio::failStart;
static unsigned captures,plays;
static void capture(void *,Uint8 *data,int bytes) {
 assert(bytes==128*16*4);++captures;assert(((float *)data)[5]==.25f);
}
static void playback(void *,Uint8 *data,int bytes) {
 assert(bytes==128*16*4);assert(captures==plays+1);++plays;
 unsigned n,ch;const float *in=ts_asio_input_block(&n,&ch);assert(n==128 && ch==16 && in);
 float *out=(float *)data;
 for(unsigned f=0;f<n;++f) {out[f*ch]=in[f*ch+4];out[f*ch+1]=in[f*ch+5];out[f*ch+2]=.4f;out[f*ch+3]=-.3f;}
}
static int hold(void *semaphore) { ts_asio_lock();SDL_SemPost((SDL_sem *)semaphore);SDL_Delay(50);ts_asio_unlock();return 0; }
int main() {
 assert(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER));
 assert(ts_asio_count(0)==2 && ts_asio_count(1)==2);
 SDL_AudioSpec w{},got{},cg{};w.freq=44100;w.samples=512;w.channels=2;w.format=AUDIO_F32SYS;w.callback=playback;
 assert(!ts_asio_open("missing",0,&w,&got));
 auto out=ts_asio_open("Test Matrix ASIO",0,&w,&got);assert(out && got.freq==48000 && got.samples==128 && got.channels==16);
 char *active=nullptr;assert(!ts_asio_default_info(&active,&cg,1));assert(!strcmp(active,"Test Matrix ASIO") && cg.freq==48000 && cg.samples==128);SDL_free(active);
 unsigned probes=RtAudio::probes;ts_asio_rescan();assert(RtAudio::probes==probes);
 w.callback=capture;assert(!ts_asio_open("Test MOTU ASIO",1,&w,&cg));
 auto in=ts_asio_open(nullptr,1,&w,&cg);assert(in && cg.freq==got.freq && cg.samples==got.samples && RtAudio::opens==1);
 assert(!ts_asio_open(nullptr,1,&w,&cg));
 ts_asio_pause(in,0);ts_asio_pause(out,0);
 float input[128*16]{},output[128*16];
 for(unsigned f=0;f<128;++f){input[f*16+4]=-.5f;input[f*16+5]=.25f;}
 for(int block=0;block<200;++block){RtAudio::instance->tick(output,input);for(unsigned f=0;f<128;++f){assert(output[f*16]==-.5f && output[f*16+1]==.25f && output[f*16+2]==.4f && output[f*16+3]==-.3f);for(int ch=4;ch<16;++ch)assert(output[f*16+ch]==0);}}
 assert(captures==200 && plays==200 && ts_asio_xruns()==0);
 SDL_sem *sem=SDL_CreateSemaphore(0);SDL_Thread *thread=SDL_CreateThread(hold,"hold",sem);SDL_SemWait(sem);
 RtAudio::instance->tick(output,input);assert(ts_asio_xruns()==1);for(float x:output)assert(x==0);SDL_WaitThread(thread,nullptr);SDL_DestroySemaphore(sem);
 RtAudio::instance->tick(output,input);assert(plays==201);
 RtAudio::instance->running=false;ts_asio_poll();SDL_Event e;unsigned removed=0;while(SDL_PollEvent(&e))if(e.type==SDL_AUDIODEVICEREMOVED){++removed;ts_asio_close(e.adevice.which);}assert(removed==2 && RtAudio::closes==1);
 ts_asio_rescan();assert(RtAudio::probes>probes);
 w.callback=playback;auto next=ts_asio_open(nullptr,0,&w,&got);assert(next && next!=out);
 ts_asio_close(out);assert(RtAudio::instance->isStreamOpen());
 RtAudio::failStart=true;ts_asio_pause(next,0);ts_asio_poll();removed=0;while(SDL_PollEvent(&e))if(e.type==SDL_AUDIODEVICEREMOVED){++removed;ts_asio_close(e.adevice.which);}assert(removed==1);
 ts_asio_quit();SDL_Quit();puts("ASIO adapter: one clock, native stride, exact unity, callback order, stalls, reset, start failure and stale handles passed");
}
