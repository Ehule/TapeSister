#include "tapesister/autopan.h"
#include <assert.h>
#include <math.h>
int main(void) {
    TsAutoPan a,b;
    TsAutoPanControls c;
    TsStereoFrame in[512],out[512],again[512];
    for(unsigned i=0;i<512;++i)in[i]=(TsStereoFrame){1.0f,1.0f};
    ts_autopan_init(&a,48000,1234);
    ts_autopan_init(&b,48000,1234);
    ts_autopan_default(&c);
    c.depth=0.0f;
    ts_autopan_set(&a,&c);
    ts_autopan_process_span(&a,in,out,512);
    for(unsigned i=0;i<512;++i) {
        assert(fabsf(out[i].l-1.0f)<0.0001f);
        assert(fabsf(out[i].r-1.0f)<0.0001f);
    }
    c.depth=1.0f;c.wave=TS_AUTOPAN_RANDOM_SMOOTH;c.rate_hz=100.0f;
    ts_autopan_set(&a,&c);ts_autopan_set(&b,&c);
    ts_autopan_reset_phase(&a);ts_autopan_reset_phase(&b);
    ts_autopan_process_span(&a,in,out,512);
    ts_autopan_process_span(&b,in,again,512);
    for(unsigned i=0;i<512;++i) {
        assert(isfinite(out[i].l)&&isfinite(out[i].r));
        assert(out[i].l==again[i].l&&out[i].r==again[i].r);
    }
    c.mode=TS_AUTOPAN_DUAL_TREMOLO;c.wave=TS_AUTOPAN_SQUARE;c.phase_offset=0.5f;
    ts_autopan_set(&a,&c);ts_autopan_reset_phase(&a);
    ts_autopan_process_span(&a,in,out,1);
    assert(out[0].l==0.0f && out[0].r==1.0f);
    return 0;
}
