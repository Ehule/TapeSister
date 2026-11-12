// Compile the pristine pinned handler independently of the optimized library.
#define psynth_echo ts_reference_echo
#include "reference/psynths_echo.cpp"
#undef psynth_echo
#include "psynth_net.h"
#include "tapesister/sunvox_fx.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
PS_RETTYPE psynth_echo(PSYNTH_MODULE_HANDLER_PARAMETERS);
static void check(bool v,const char *what){if(!v){std::fprintf(stderr,"Echo: %s\n",what);std::exit(1);}}
struct Echo {
    psynth_net *net=SMEM_ALLOC2(psynth_net,1);int id;
    std::vector<float> input[2],output[2];
    Echo(psynth_handler_t fn,int rate) {
        psynth_init(PSYNTH_NET_FLAG_NO_SCOPE|PSYNTH_NET_FLAG_NO_MIDI|PSYNTH_NET_FLAG_NO_MODULE_CHANNELS,
            rate,125,6,nullptr,0x02010404,net);
        id=psynth_add_module(-1,fn,"Echo oracle",0,0,0,0,125,6,net);check(id>0,"create");
        for(int c=0;c<2;++c){input[c].resize(1040);output[c].resize(1040);mod().channels_in[c]=input[c].data();mod().channels_out[c]=output[c].data();}
        psynth_do_command(id,PS_CMD_SETUP_FINISHED,net);
    }
    ~Echo(){psynth_close(net);}
    psynth_module &mod(){return net->mods[id];}
    psynth_echo_data &data(){return *static_cast<psynth_echo_data*>(mod().data_ptr);}
    void ctl(int n,int v){auto &c=mod().ctls[n];psynth_handle_ctl_event(id,n,c.type==1?v:(v-c.min)*32768/(c.max-c.min),net);}
    PS_RETTYPE render(int offset,int frames,bool silent){mod().offset=offset;mod().frames=frames;for(int c=0;c<2;++c)mod().in_empty[c]=silent?1040:0;psynth_event e{};e.command=PS_CMD_RENDER_REPLACE;return mod().handler(id,&e,net);}
};
static void same(Echo &a,Echo &b,bool history)
{
    auto x=a.data(),y=b.data();
    for(int c=0;c<2;++c){x.buf[c]=y.buf[c]=nullptr;
        check(!std::memcmp(a.output[c].data(),b.output[c].data(),1040*sizeof(float)),"PCM differs");
        if(history)check(!std::memcmp(a.data().buf[c],b.data().buf[c],x.buf_size*sizeof(float)),"history differs");}
    check(!std::memcmp(&x,&y,sizeof(x)),"state differs");
}
int main(int argc,char **argv)
{
    TsSunVoxControls c;check(ts_sunvox_select(&c,0),"init");
    if(argc==2 && !std::strcmp(argv[1],"--benchmark")) {
        for(int pass=0;pass<6;++pass)for(int version=0;version<2;++version) {
            Echo e(version?psynth_echo:ts_reference_echo,48000);e.ctl(0,0);e.ctl(1,256);e.ctl(2,192);
            e.data().delay_len=8192;e.data().changed=0;
            for(int ch=0;ch<2;++ch)for(int i=0;i<1040;++i)e.input[ch][i]=(i%31-15)*.01f;
            auto start=std::clock();for(int i=0;i<100000;++i)e.render(0,256,false);
            std::printf("echo version=%d ms=%.3f\n",version,1000.*(std::clock()-start)/CLOCKS_PER_SEC);
        }
        return 0;
    }
    unsigned random=1,cases=0;
    for(int rate:{8000,48000,192000})for(int q:{64,256,1024})
    for(int length:{1,2,7,63,64,65,255,256,257,1023,1024,1025,7499}) {
        Echo a(ts_reference_echo,rate),b(psynth_echo,rate);
        // Exact sample lengths isolate wrap boundaries regardless of ctl units.
        for(auto e:{&a,&b}){e->data().delay_len=length;e->data().buf_ptr=length-1;e->data().changed=0;}
        for(int block=0;block<48;++block) {
            auto ctl=[&](int n,int v){a.ctl(n,v);b.ctl(n,v);};
            ctl(0,(block%3)*128);ctl(1,(block%3)*128);ctl(2,block%2?256:173);
            ctl(4,block%2);ctl(6,(block%4)*8192);ctl(7,(block/8)%3);ctl(8,1000+block*23);
            bool silent=block>=32;
            for(int ch=0;ch<2;++ch)for(int i=0;i<1040;++i) {
                random=random*1664525u+1013904223u;
                float v=silent?0:(int(random>>16)-32768)/131072.f;
                if(i%41==0)v=-0.f;if(!silent && i%43==0)v=1e-38f;
                a.input[ch][i]=b.input[ch][i]=v;a.output[ch][i]=b.output[ch][i]=123.25f;
            }
            int split=1+block%(q-1);
            check(a.render(7,split,silent)==b.render(7,split,silent),"status");same(a,b,false);
            check(a.render(7+split,q-split,silent)==b.render(7+split,q-split,silent),"split status");same(a,b,block%8==0 || block==47);
            if(block==16){ctl(5,1);ctl(3,7);} // Real length automation retains history.
            if(block==24){ctl(5,2);ctl(3,750);}
        }
        ++cases;
    }
    std::printf("Echo: %u exact cases, offsets, automation, feedback, filters, tails and split events\n",cases);
}
