// Compile the pristine pinned handler as an oracle, not a second implementation
// of the optimized loop. The production handler comes from tapesister_sunvox.
#define psynth_delay ts_sunvox_reference_delay
#include "reference/psynths_delay.cpp"
#undef psynth_delay
#include "tapesister/sunvox_fx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

PS_RETTYPE psynth_delay( PSYNTH_MODULE_HANDLER_PARAMETERS );

static int rate_now, quantum_now, delay_now, block_now;
static void check(bool ok, const char* message)
{
    if(ok) return;
    std::fprintf(stderr, "%s: rate=%d quantum=%d delay=%d block=%d\n",
        message, rate_now, quantum_now, delay_now, block_now);
    std::exit(1);
}

struct Delay {
    psynth_net* net = SMEM_ALLOC2(psynth_net,1); // psynth_close owns this allocation
    int id;
    std::vector<float> input[2], output[2];
    Delay(psynth_handler_t handler, int rate) {
        psynth_init(PSYNTH_NET_FLAG_NO_SCOPE | PSYNTH_NET_FLAG_NO_MIDI |
            PSYNTH_NET_FLAG_NO_MODULE_CHANNELS, rate, 125, 6, nullptr, 0x02010404, net);
        id = psynth_add_module(-1, handler, "Delay oracle", 0, 0, 0, 0, 125, 6, net);
        check(id > 0, "module creation");
        for(int ch=0; ch<2; ++ch) {
            input[ch].resize(1040);
            output[ch].resize(1040);
            mod().channels_in[ch] = input[ch].data();
            mod().channels_out[ch] = output[ch].data();
        }
        ctl(8, 9); // samples
        psynth_do_command(id, PS_CMD_SETUP_FINISHED, net);
    }
    ~Delay() { psynth_close(net); }
    psynth_module& mod() { return net->mods[id]; }
    psynth_delay_data& data() { return *static_cast<psynth_delay_data*>(mod().data_ptr); }
    void ctl(int index, int value) {
        const auto& c = mod().ctls[index];
        int raw = c.type == 1 ? value : (value - c.min) * 32768 / (c.max - c.min);
        psynth_handle_ctl_event(id, index, raw, net);
    }
    PS_RETTYPE render(int offset, int frames, bool silent) {
        mod().offset = offset;
        mod().frames = frames;
        for(int ch=0; ch<2; ++ch) mod().in_empty[ch] = silent ? 1040 : 0;
        psynth_event event{};
        event.command = PS_CMD_RENDER_REPLACE;
        return mod().handler(id, &event, net);
    }
};

static void same(Delay& a, Delay& b)
{
    auto& x=a.data(); auto& y=b.data();
    check(x.buf_size==y.buf_size && x.buf_ptr==y.buf_ptr &&
        x.delay[0]==y.delay[0] && x.delay[1]==y.delay[1] &&
        x.buf_clean==y.buf_clean && x.empty_frames_counter==y.empty_frames_counter &&
        x.frame_counter==y.frame_counter && x.ctls_changed==y.ctls_changed &&
        x.evtbuf_wp==y.evtbuf_wp && x.evtbuf_rp==y.evtbuf_rp, "delay state differs");
    check(a.mod().input_channels==b.mod().input_channels &&
        a.mod().output_channels==b.mod().output_channels, "channel count differs");
    for(int ch=0; ch<2; ++ch) {
        check(!std::memcmp(x.buf[ch], y.buf[ch], x.buf_size*sizeof(float)), "history differs");
        check(!std::memcmp(a.output[ch].data(), b.output[ch].data(), 1040*sizeof(float)), "output differs");
        for(float sample : a.output[ch]) check(std::isfinite(sample), "nonfinite sample");
    }
}

int main()
{
    // Initialize the same SDK globals used by the application, exactly once.
    TsSunVoxControls initial;
    check(ts_sunvox_select(&initial,0), "SDK initialization");
    const int rates[]={8000,44100,48000,96000,192000};
    const int quanta[]={64,256,1024};
    const int delays[]={0,1,2,7,63,64,65,255,256,257,749,750,751,1023,1024,1025,8207,32768};
    unsigned random=0x12345678;
    unsigned cases=0;
    for(int rate : rates) for(int q : quanta) for(int delay : delays) {
        rate_now=rate; quantum_now=q; delay_now=delay;
        Delay original(ts_sunvox_reference_delay,rate), optimized(psynth_delay,rate);
        auto ctl=[&](int n,int v) { original.ctl(n,v); optimized.ctl(n,v); };
        ctl(0,0); ctl(2,delay); ctl(3,std::min(32768,delay+3));
        // Enough samples to wrap both rings repeatedly, then drain their tails.
        int blocks=(std::max(rate/64,delay+3)*4+q-1)/q+20;
        for(int block=0; block<blocks; ++block) {
            block_now=block;
            if(block==blocks/3) { ctl(2,1); ctl(3,7); } // retained long ring, short read offsets
            if(block==blocks/2) { ctl(2,delay); ctl(3,std::min(32768,delay+3)); }
            if(block%17==0) { ctl(1,(block%3)*128); ctl(7,(block/17)%2); }
            if(block%19==0) { ctl(0,(block%3)*128); ctl(4,173); ctl(5,231); }
            if(block%23==0) ctl(6,(block/23)%2); // mono/stereo transitions
            if(block%29==0) { ctl(10,(block/29)%2 ? 12000:0); ctl(11,(block/29)%2); ctl(12,(block/58)%2); }
            bool silent=block>=blocks*3/4;
            for(int ch=0; ch<2; ++ch) {
                std::fill(original.output[ch].begin(),original.output[ch].end(),123.25f);
                std::fill(optimized.output[ch].begin(),optimized.output[ch].end(),123.25f);
                for(int i=0; i<1040; ++i) {
                    random=random*1664525u+1013904223u;
                    float sample=silent ? 0 : (int(random>>16)-32768)/131072.0f;
                    // Exact zeros, signed zeros and tiny values as well as noise.
                    if(i%41==0) sample=-0.0f;
                    if(!silent && i%43==0) sample=1e-38f;
                    original.input[ch][i]=optimized.input[ch][i]=sample;
                }
            }
            // Exercise one-sample segments and nonzero offsets, as when the
            // network splits a block around a controller event.
            int split=1+block%(q-1), offset=7;
            check(original.render(offset,split,silent)==optimized.render(offset,split,silent), "render status differs");
            same(original,optimized);
            if(block%7==0) ctl(7,(block/7)%2);
            check(original.render(offset+split,q-split,silent)==optimized.render(offset+split,q-split,silent), "split status differs");
            same(original,optimized);
            for(int ch=0; ch<2; ++ch) {
                for(int i=0;i<offset;++i) check(optimized.output[ch][i]==123.25f,"prefix guard");
                for(int i=offset+q;i<1040;++i) check(optimized.output[ch][i]==123.25f,"suffix guard");
            }
        }
        psynth_do_command(original.id,PS_CMD_CLEAN,original.net);
        psynth_do_command(optimized.id,PS_CMD_CLEAN,optimized.net);
        same(original,optimized);
        ++cases;
    }
    std::printf("SunVox Delay: %u cases bit-identical to pinned original (output and history)\n",cases);
}
