#define SDL_MAIN_HANDLED
#include "tapesister/asio_backend.h"
#include "RtAudio.h"
#include "../src/ts_asio_callback_lock.h"
#include <cassert>
#include <cstdio>
#include <cstring>
RtAudio *RtAudio::instance;
unsigned RtAudio::opens,RtAudio::closes,RtAudio::probes;
bool RtAudio::failStart;
static unsigned captures,plays;
static void lock_deadlines() {
 struct Fake {
  uint64_t ticks=0;unsigned calls=0,clocks=0,busy=0;bool error=false;
  int try_lock() { ++calls;return error?-1:(calls<=busy?1:0); }
  uint64_t now() { ++clocks;return ticks++; }
  void pause() {}
 } fast,shortEdit,longEdit,failed,zero,wrapped;
 assert(ts_asio_callback_lock(fast,10)==TsAsioLock::acquired);
 assert(fast.calls==1 && fast.clocks==0); /* Uncontended path does not read time. */
 shortEdit.busy=4;
 assert(ts_asio_callback_lock(shortEdit,10)==TsAsioLock::recovered);
 assert(shortEdit.calls==5 && shortEdit.ticks<=10);
 longEdit.busy=1000;
 assert(ts_asio_callback_lock(longEdit,10)==TsAsioLock::skipped);
 assert(longEdit.calls==10 && longEdit.ticks==11); /* Never wait for a long edit. */
 failed.error=true;
 assert(ts_asio_callback_lock(failed,10)==TsAsioLock::skipped && failed.calls==1);
 zero.busy=1;assert(ts_asio_callback_lock(zero,0)==TsAsioLock::skipped && zero.calls==1);
 wrapped.ticks=UINT64_MAX-4;wrapped.busy=1000;
 assert(ts_asio_callback_lock(wrapped,10)==TsAsioLock::skipped);
 assert(wrapped.calls==10 && wrapped.clocks==11); /* Unsigned elapsed time survives wrap. */
 assert(ts_asio_lock_budget(1024,44100,1000000000)==500000);
 assert(ts_asio_lock_budget(512,44100,1000000000)==500000);
 assert(ts_asio_lock_budget(128,48000,1000000000)==333333);
 assert(ts_asio_lock_budget(32,192000,1000000000)==20833);
 assert(ts_asio_lock_budget(128,0,1000000000)==0);
}
static void capture(void *,Uint8 *data,int bytes) {
 assert(bytes==128*16*4);++captures;assert(((float *)data)[5]==.25f);
}
static void playback(void *,Uint8 *data,int bytes) {
 assert(bytes==128*16*4);assert(captures==plays+1);++plays;
 unsigned n,ch;const float *in=ts_asio_input_block(&n,&ch);assert(n==128 && ch==16 && in);
 float *out=(float *)data;
 for(unsigned f=0;f<n;++f) {out[f*ch]=in[f*ch+4];out[f*ch+1]=in[f*ch+5];out[f*ch+2]=.4f;out[f*ch+3]=-.3f;}
}
static void rangedCapture(void *,Uint8 *data,int bytes) {
 assert(bytes==128*6*4);
 const float *in=(float *)data;
 for(unsigned f=0;f<128;++f)for(unsigned ch=0;ch<6;++ch)
  assert(in[f*6+ch]==(ch<2?0.f:float(ch+1)));
}
static void rangedPlayback(void *,Uint8 *data,int bytes) {
 assert(bytes==128*10*4);
 unsigned n,ch;const float *in=ts_asio_input_block(&n,&ch);assert(in && ch==6 && n==128);
 float *out=(float *)data;
 for(unsigned f=0;f<128;++f)for(unsigned c=0;c<10;++c)out[f*10+c]=float(c+1);
}
static void ranges() {
 RtAudio::failStart=false;
 assert(ts_asio_set_ranges({3,10},{3,6}));
 SDL_AudioSpec w{},got{},cg{};w.format=AUDIO_F32SYS;w.callback=rangedPlayback;w.samples=256;
 auto out=ts_asio_open(nullptr,0,&w,&got);assert(out && got.channels==10);
 assert(RtAudio::instance->channels==8 && RtAudio::instance->outputFirst==2);
 assert(RtAudio::instance->inputChannels==4 && RtAudio::instance->inputFirst==2);
 assert(!ts_asio_set_ranges({1,16},{1,0}));
 w.callback=rangedCapture;auto in=ts_asio_open(nullptr,1,&w,&cg);assert(in && cg.channels==6);
 ts_asio_pause(in,0);ts_asio_pause(out,0);
 float input[128*4],output[128*8];
 for(unsigned f=0;f<128;++f)for(unsigned ch=0;ch<4;++ch)input[f*4+ch]=float(ch+3);
 RtAudio::instance->tick(output,input);
 for(unsigned f=0;f<128;++f)for(unsigned ch=0;ch<8;++ch)assert(output[f*8+ch]==float(ch+3));
 ts_asio_close(in);ts_asio_close(out);
 assert(ts_asio_set_ranges({1,17},{1,0}));assert(!ts_asio_open(nullptr,0,&w,&got));
 assert(ts_asio_set_ranges({1,0},{1,0}));
}
static int hold(void *semaphore) { ts_asio_lock();SDL_SemPost((SDL_sem *)semaphore);SDL_Delay(50);ts_asio_unlock();return 0; }
int main() {
 lock_deadlines();
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
 Uint64 lockedBegin=SDL_GetPerformanceCounter();
 RtAudio::instance->tick(output,input);
 double lockedUs=1e6*(SDL_GetPerformanceCounter()-lockedBegin)/SDL_GetPerformanceFrequency();
 assert(ts_asio_xruns()==1);for(float x:output)assert(x==0);SDL_WaitThread(thread,nullptr);SDL_DestroySemaphore(sem);
 printf("ASIO fixture: forced 50 ms control hold, actual 128/48000 period %.3f us, retry budget %.3f us, callback wall %.3f us, skips %u, driver reports %u\n",
  1e6*got.samples/got.freq,1e6*ts_asio_lock_budget(got.samples,got.freq,SDL_GetPerformanceFrequency())/SDL_GetPerformanceFrequency(),lockedUs,ts_asio_control_skips(),ts_asio_driver_xruns());
 assert(ts_asio_control_skips()==1 && ts_asio_driver_xruns()==0);
 RtAudio::instance->tick(output,input);assert(plays==201);
 RtAudio::instance->tick(output,input,128,1);
 assert(ts_asio_xruns()==2 && ts_asio_control_skips()==1 && ts_asio_driver_xruns()==1);
 RtAudio::instance->running=false;ts_asio_poll();SDL_Event e;unsigned removed=0;while(SDL_PollEvent(&e))if(e.type==SDL_AUDIODEVICEREMOVED){++removed;ts_asio_close(e.adevice.which);}assert(removed==2 && RtAudio::closes==1);
 ts_asio_rescan();assert(RtAudio::probes>probes);
 w.callback=playback;auto next=ts_asio_open(nullptr,0,&w,&got);assert(next && next!=out);
 assert(!ts_asio_xruns() && !ts_asio_control_skips() && !ts_asio_driver_xruns() && !ts_asio_control_recoveries());
 ts_asio_close(out);assert(RtAudio::instance->isStreamOpen());
 RtAudio::failStart=true;ts_asio_pause(next,0);ts_asio_poll();removed=0;while(SDL_PollEvent(&e))if(e.type==SDL_AUDIODEVICEREMOVED){++removed;ts_asio_close(e.adevice.which);}assert(removed==1);
 ranges();ts_asio_quit();SDL_Quit();puts("ASIO adapter: one clock, native stride, exact unity, callback order, stalls, reset, start failure and stale handles passed");
}
