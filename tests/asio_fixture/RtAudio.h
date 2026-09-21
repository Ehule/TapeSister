/* Deterministic driver boundary fixture; production uses vendored RtAudio. */
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cassert>
enum RtAudioErrorType { RTAUDIO_NO_ERROR, RTAUDIO_WARNING, RTAUDIO_SYSTEM_ERROR };
using RtAudioStreamStatus = unsigned;
using RtAudioErrorCallback = std::function<void(RtAudioErrorType,const std::string &)>;
constexpr unsigned RTAUDIO_FLOAT32 = 16;
class RtAudio {
public:
 enum Api { WINDOWS_ASIO };
 struct DeviceInfo { unsigned ID{},outputChannels{},inputChannels{},currentSampleRate{},preferredSampleRate{};std::string name; };
 struct StreamParameters { unsigned deviceId{},nChannels{},firstChannel{}; };
 struct StreamOptions { std::string streamName; };
 using Callback = int (*)(void *,void *,unsigned,double,RtAudioStreamStatus,void *);
 static RtAudio *instance;
 static unsigned opens,closes,probes;
 static bool failStart;
 bool open{},running{};Callback callback{};void *userdata{};unsigned frames{},channels{};
 RtAudio(Api,RtAudioErrorCallback &&) { instance=this; }
 ~RtAudio() {instance=nullptr;}
 std::vector<unsigned> getDeviceIds() { assert(!open);++probes;return {7,9}; }
 DeviceInfo getDeviceInfo(unsigned id) {DeviceInfo d;d.ID=id;d.outputChannels=d.inputChannels=16;d.currentSampleRate=48000;d.preferredSampleRate=44100;d.name=id==7?"Test MOTU ASIO":"Test Matrix ASIO";return d;}
 bool isStreamOpen() {return open;}
 bool isStreamRunning() {return running;}
 unsigned getStreamSampleRate() {return 48000;}
 RtAudioErrorType openStream(StreamParameters *op,StreamParameters *ip,unsigned fmt,unsigned rate,unsigned *n,Callback cb,void *u,StreamOptions *) {
  assert(!open && op && ip && op->deviceId==ip->deviceId && op->nChannels==16 && ip->nChannels==16 && rate==48000 && fmt==RTAUDIO_FLOAT32);
  open=true;++opens;*n=frames=128;channels=16;callback=cb;userdata=u;return RTAUDIO_NO_ERROR;
 }
 RtAudioErrorType startStream() { running=!failStart;return running?RTAUDIO_NO_ERROR:RTAUDIO_SYSTEM_ERROR; }
 void closeStream() {assert(open);open=running=false;++closes;}
 void tick(float *out,float *in,unsigned n=128,unsigned status=0) {assert(running);callback(out,in,n,0,status,userdata);}
};
