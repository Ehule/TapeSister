#include "RtAudio.h"
RtAudio *RtAudio::instance;
unsigned RtAudio::opens, RtAudio::closes, RtAudio::probes;
unsigned RtAudio::maxInputs=16,RtAudio::maxOutputs=16;
bool RtAudio::failStart;
extern "C" unsigned fixture_asio_opens(void) { return RtAudio::opens; }
extern "C" unsigned fixture_asio_closes(void) { return RtAudio::closes; }
extern "C" void fixture_asio_fail_start(int fail) { RtAudio::failStart = fail != 0; }
extern "C" void fixture_asio_tick(float *out,float *in) { RtAudio::instance->tick(out,in); }
