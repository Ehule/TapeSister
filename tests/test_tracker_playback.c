#include "tapesister/tracker_playback.h"
#include "tapesister/audio_mixer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"tracker playback line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static char error[256];
static TsInstrument *bank;
static TsSamplePages *pages;
static TsTrackerPlayback rt;
static TsTrackerPattern *pattern;
static TsTileId tile;
static void prepare(int rate)
{
    TsTrackerPrepared *p=ts_tracker_playback_prepare(&rt,pages,bank,pattern->id,rate,error,sizeof(error));
    CHECK(p);ts_tracker_prepared_free(ts_tracker_playback_publish(&rt,p));ts_tracker_playback_collect(&rt);
}
static void fixture(int rows)
{
    bank=malloc(sizeof(*bank));pages=malloc(sizeof(*pages));CHECK(bank && pages);
    ts_instrument_init(bank);CHECK(ts_sample_pages_init(pages,error,sizeof(error)));ts_tracker_playback_init(&rt);
    CHECK(ts_instrument_select_bank(bank,0,error,sizeof(error)));
    CHECK(ts_instrument_activate_silence(bank,48000,48000,error,sizeof(error)));
    for(size_t i=0;i<bank->current.frames;++i)bank->current.data[i]=.2f;
    bank->has_loop=1;bank->loop_first=0;bank->loop_last=bank->current.frames;
    CHECK(ts_instrument_sync_selected(bank,error,sizeof(error)));
    tile=bank->bank[0].tile_id;uint8_t alias;TsPatternId id;
    CHECK(ts_sister_tracker_add_pattern(&pages->tracker,rows,&id,error,sizeof(error)));
    CHECK(ts_sister_tracker_bind_tile(&pages->tracker,tile,&alias,error,sizeof(error)));
    pattern=ts_sister_tracker_pattern(&pages->tracker,id);
}
static void cleanup(void)
{
    ts_tracker_playback_free(&rt);CHECK(!rt.sources);
    ts_sample_pages_free(pages);ts_instrument_free(bank);free(pages);free(bank);
}
static TsTrackerCell note(TsTileId id,int pitch)
{return (TsTrackerCell){.tile_id=id,.note_kind=TS_TRACKER_NOTE_PITCH,.note=pitch};}
static TsStereoFrame render(int count,int rate)
{TsStereoFrame f={0};for(int i=0;i<count;++i)f=ts_tracker_playback_read(&rt,rate);return f;}
static void timing(void)
{
    fixture(3);for(int row=0;row<3;++row)pattern->cells[row][0]=note(tile,60+row);
    prepare(48000);CHECK(ts_tracker_playback_start(&rt));
    render(1,48000);CHECK(rt.row==0 && rt.lanes[0].last_note_frame==0);
    render(5759,48000);CHECK(rt.lanes[0].notes_started==1);
    render(1,48000);CHECK(rt.row==1 && rt.lanes[0].last_note_frame==5760);
    render(5760,48000);CHECK(rt.row==2 && rt.lanes[0].last_note_frame==11520);
    render(5760,48000);CHECK(rt.row==0 && rt.lanes[0].last_note_frame==17280);
    pages->tracker.bpm=137;pages->tracker.ticks_per_line=3;prepare(44100);
    CHECK(ts_tracker_playback_start(&rt));unsigned events=0;
    for(int f=0;events<100;++f) {
        render(1,44100);
        if(rt.lanes[0].notes_started!=events) {
            uint64_t expected=(uint64_t)ceil(events*44100.0*2.5/137*3-1e-7);
            CHECK(rt.lanes[0].last_note_frame==expected);++events;
        }
    }
    cleanup();
}
static void pause_rate_and_stop(void)
{
    fixture(2);pattern->cells[0][0]=pattern->cells[1][0]=note(tile,60);
    prepare(48000);CHECK(ts_tracker_playback_start(&rt));render(1000,48000);
    double position=rt.lanes[0].voice.position,until=rt.until_tick;
    uint64_t clock=rt.elapsed_frames;
    ts_tracker_playback_pause(&rt,1);TsStereoFrame f=render(500,48000);
    CHECK(rt.lanes[0].voice.position==position && rt.until_tick==until && rt.elapsed_frames==clock);
    CHECK(fabs(f.l)<1e-6);ts_tracker_playback_pause(&rt,0);render(1,96000);
    CHECK(fabs(rt.until_tick-(until*2-1))<1e-6 && fabs(rt.lanes[0].voice.step-.5)<1e-9);
    CHECK(rt.lanes[0].voice.position==position+.5);
    ts_tracker_playback_stop(&rt);f=render(500,96000);CHECK(!rt.running && !rt.lanes[0].source && fabs(f.l)<1e-6);
    pages->tracker.loop=0;prepare(48000);CHECK(ts_tracker_playback_start(&rt));render(11521,48000);
    CHECK(!rt.running);cleanup();
}
static void inheritance_and_ownership(void)
{
    fixture(5);pages->tracker.ticks_per_line=1;
    pattern->cells[0][0]=pattern->cells[0][1]=note(tile,60);
    pattern->cells[0][3]=note(tile,60);pattern->cells[0][3].has_volume=1;pattern->cells[0][3].volume=0;
    pattern->cells[1][0]=(TsTrackerCell){.tile_id=tile,.has_volume=1,.volume=0x20};
    pattern->cells[2][0]=note(0,60);pattern->cells[3][0].note_kind=TS_TRACKER_NOTE_OFF;
    pattern->cells[4][1].note_kind=TS_TRACKER_NOTE_CUT;
    prepare(48000);CHECK(ts_tracker_playback_start(&rt));
    TsStereoFrame f=render(500,48000);CHECK(rt.lanes[3].gain==0);CHECK(fabs(f.l-.4)<1e-5); /* No lane-count normalization. */
    CHECK(rt.lanes[0].source==rt.lanes[1].source && rt.lanes[0].voice.sample==rt.lanes[1].voice.sample);
    render(461,48000);CHECK(rt.lanes[0].notes_started==1 && rt.lanes[0].volume==0x20);
    render(960,48000);CHECK(rt.lanes[0].notes_started==2 && rt.lanes[0].voice.active);
    render(960,48000);CHECK(!rt.lanes[0].voice.active && rt.lanes[1].voice.active);
    render(960,48000);CHECK(!rt.lanes[1].voice.active && fabs(render(1,48000).l)<1e-6);
    ts_tracker_playback_solo(&rt,2);CHECK(ts_tracker_playback_start(&rt));render(1,48000);
    CHECK(rt.lanes[0].gain==0 && rt.lanes[1].gain==1);
    TsAudioBuses buses={.tracker={.2f,.3f},.tile_performance={.1f,.1f}};TsAudioMixer mix;ts_audio_mixer_init(&mix);
    CHECK(fabs(ts_audio_mixer_render(&mix,&buses).l-.24)<1e-6);
    ts_audio_buses_apply_sister_ownership(&buses,1);CHECK(buses.tracker.l==0 && buses.tile_performance.l==0);
    cleanup();
}
static void missing_and_live_edits(void)
{
    fixture(3);pages->tracker.ticks_per_line=1;
    pattern->cells[0][0]=note(tile,60);
    TsTileId missing=ts_tile_id_new();uint8_t alias;
    CHECK(ts_sister_tracker_bind_tile(&pages->tracker,missing,&alias,error,sizeof(error)));
    pattern->cells[1][0]=note(missing,60);pattern->cells[2][0]=note(0,60);
    prepare(48000);CHECK(ts_tracker_playback_start(&rt));render(961,48000);
    CHECK(!rt.lanes[0].voice.active && (rt.missing_mask&1));render(960,48000);
    CHECK(!rt.lanes[0].voice.active && rt.lanes[0].default_tile==missing);
    pages->tracker.ticks_per_line=6;prepare(48000);
    CHECK(ts_tracker_playback_start(&rt));render(500,48000);
    double position=rt.lanes[0].voice.position;
    pattern->cells[2][0]=note(tile,72);prepare(48000);
    CHECK(rt.lanes[0].voice.position==position && rt.lanes[0].notes_started==1);
    /* A native edit replaces the generation without restarting its head. */
    for(size_t i=0;i<bank->current.frames;++i)bank->current.data[i]=-.2f;
    ts_sample_touch(&bank->current);prepare(48000);
    CHECK(rt.lanes[0].voice.position==position);
    TsStereoFrame previous={.2f,.2f};float maximum=0;
    for(int i=0;i<300;++i){TsStereoFrame f=render(1,48000);maximum=fmaxf(maximum,fabsf(f.l-previous.l));previous=f;}
    CHECK(maximum<.003f && fabs(previous.l+.2)<1e-5);
    /* Publishing lane gain changes never retriggers the playing note. */
    position=rt.lanes[0].voice.position;pages->tracker.lanes[0].muted=1;prepare(48000);
    CHECK(rt.lanes[0].voice.position==position);CHECK(fabs(render(200,48000).l)<1e-6);
    pages->tracker.lanes[0].muted=0;pages->tracker.lanes[0].trim=.5f;prepare(48000);
    CHECK(fabs(render(200,48000).l+.1)<1e-5);
    ts_tracker_playback_solo(&rt,2);CHECK(fabs(render(200,48000).l)<1e-6);
    ts_tracker_playback_solo(&rt,0);render(200,48000);
    position=rt.lanes[0].voice.position;
    CHECK(ts_sample_pages_move_tile(pages,bank,(TsTileLocation){0,0},(TsTileLocation){0,1},error,sizeof(error)));
    prepare(48000);CHECK(rt.lanes[0].sounding_tile==tile && rt.lanes[0].voice.position==position);
    CHECK(ts_instrument_bank_clear(bank,1,error,sizeof(error)));prepare(48000);
    CHECK(!rt.lanes[0].source && (rt.missing_mask&1));cleanup();
}
static void native_reader_metadata(void)
{
    fixture(2);pattern->cells[0][0]=pattern->cells[1][0]=note(tile,60);
    bank->loop_first=100;bank->loop_last=200;bank->loop_mode=TS_LOOP_REVERSE;
    CHECK(ts_instrument_sync_selected(bank,error,sizeof(error)));prepare(48000);
    CHECK(ts_tracker_playback_start(&rt));render(1,48000);
    CHECK(rt.lanes[0].voice.direction==-1 && rt.lanes[0].voice.position==198);
    bank->loop_mode=TS_LOOP_FORWARD;prepare(48000);
    CHECK(rt.lanes[0].voice.direction==1 && rt.lanes[0].voice.position==198);
    render(1,48000);CHECK(rt.lanes[0].voice.position==199);
    bank->has_loop=0;bank->current.sample_rate=24000;prepare(48000);
    CHECK(!rt.lanes[0].voice.looping && rt.lanes[0].voice.direction==1);
    CHECK(rt.lanes[0].voice.step==.5 && fabs(rt.lanes[0].voice.position-199)<1e-8);
    render(1,48000);CHECK(fabs(rt.lanes[0].voice.position-199.5)<1e-8);
    pages->tracker.ticks_per_line=31;prepare(96000);CHECK(ts_tracker_playback_start(&rt));
    render(59521,96000);CHECK(rt.lanes[0].last_note_frame==59520);
    cleanup();
}
static void block_loop(void)
{
    fixture(8);pages->tracker.ticks_per_line=1;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)pattern->cells[y][x]=note(0,60+y);
    for(int x=0;x<8;++x)pattern->cells[0][x]=(TsTrackerCell){.tile_id=tile,.has_volume=1,.volume=32};
    prepare(48000);TsTrackerBlock b={pattern->id,2,3,2,3};
    CHECK(ts_tracker_playback_start_block(&rt,b));render(1,48000);
    CHECK(rt.row==2 && rt.loop_seam && rt.lanes[2].default_tile==tile && rt.lanes[2].volume==32);
    for(int x=0;x<8;++x)CHECK(rt.lanes[x].notes_started==(unsigned)(x==2 || x==3));
    render(400,48000);double until=rt.until_tick,phase=rt.lanes[2].voice.position;
    pattern->cells[2][2].note=90;prepare(48000);
    CHECK(rt.lanes[2].voice.position==phase && rt.lanes[2].notes_started==1);
    b.row0=4;b.row1=6;b.lane0=3;b.lane1=4;CHECK(ts_tracker_playback_queue_block(&rt,b));
    CHECK(rt.row==2 && rt.block.row0==2 && rt.until_tick==until);
    ts_tracker_playback_pause(&rt,1);render(5000,48000);CHECK(rt.until_tick==until && !rt.loop_cycles);
    ts_tracker_playback_pause(&rt,0);render(1519,48000);CHECK(rt.row==3 && !rt.loop_cycles);
    render(1,48000);CHECK(rt.row==4 && rt.loop_cycles==1 && rt.block.lane0==3 && rt.block.lane1==4);
    CHECK(!rt.lanes[2].voice.active && rt.lanes[4].default_tile==tile && rt.lanes[4].volume==32);
    CHECK(rt.lanes[4].last_note_frame==1920 && rt.lanes[3].notes_started==3);
    /* Shortening past the current row is safe and does not replay a row. */
    uint64_t notes=rt.lanes[3].notes_started;CHECK(ts_sister_tracker_set_rows(&pages->tracker,pattern->id,2));
    prepare(48000);CHECK(rt.row==4 && rt.lanes[3].notes_started==notes);
    render(960,48000);CHECK(rt.row==1 && rt.block.row0==1 && rt.block.row1==1 && rt.lanes[3].notes_started==notes+1);
    CHECK(!ts_tracker_playback_queue_block(&rt,(TsTrackerBlock){pattern->id,0,2,0,0}));
    CHECK(ts_sister_tracker_set_rows(&pages->tracker,pattern->id,8));
    pages->tracker.bpm=137;pages->tracker.ticks_per_line=3;prepare(44100);
    b=(TsTrackerBlock){pattern->id,2,4,2,2};CHECK(ts_tracker_playback_start_block(&rt,b));
    uint64_t last=0;int seams=0;
    for(int frame=0;seams<100;++frame) {
        render(1,44100);
        if(rt.loop_seam) {
            uint64_t expected=(uint64_t)ceil(seams*3*44100.0*2.5/137*3-1e-7);
            CHECK((uint64_t)frame==expected && rt.row==2);last=expected;++seams;
        }
    }
    CHECK(last>0 && rt.loop_cycles==99);
    ts_tracker_playback_stop(&rt);CHECK(!rt.block_active);cleanup();
}
static void to_tick(unsigned tick,int rate,int bpm)
{
    uint64_t frame=(uint64_t)ceil(tick*rate*2.5/bpm-1e-7);
    CHECK(frame+1>=rt.elapsed_frames);
    render((int)(frame+1-rt.elapsed_frames),rate);
}
static void rational_clocks(void)
{
    static const unsigned num[]={1,2,3,4,5,7,15,1,17,8,6,5,4,3,2,3,5};
    static const unsigned den[]={2,3,4,5,6,8,16,1,16,7,5,4,3,2,1,1,1};
    static const int tpls[]={1,2,6,31};
    fixture(3);
    for(int row=0;row<3;++row)for(int lane=0;lane<8;++lane)pattern->cells[row][lane]=note(tile,60+row);
    /* These expected event counts come from elapsed rational time, not the
       scheduler implementation. Frequent master wraps must not reset phase. */
    for(unsigned r=0;r<TS_TRACKER_RATIOS;++r)for(unsigned k=0;k<4;++k)for(int test_rate=0;test_rate<2;++test_rate) {
        int rate=test_rate?44100:1000,bpm=test_rate?137:125,tpl=tpls[k];
        pages->tracker.bpm=bpm;pages->tracker.ticks_per_line=tpl;
        pages->tracker.lanes[0].mode=TS_TRACKER_PATTERN;pages->tracker.lanes[0].ratio=r;
        pages->tracker.lanes[1].mode=TS_TRACKER_PATTERN;pages->tracker.lanes[1].ratio=7;
        prepare(rate);CHECK(ts_tracker_playback_start(&rt));
        for(unsigned tick=0;tick<200;++tick) {
            to_tick(tick,rate,bpm);
            uint64_t crossings=(uint64_t)tick*num[r]/(den[r]*tpl);
            CHECK(rt.lanes[0].notes_started==1+crossings);
            CHECK(rt.lanes[0].source_row==crossings%3);
            CHECK(rt.lanes[0].voice.note==60+(int)(crossings%3));
            CHECK(rt.lanes[1].notes_started==rt.lanes[2].notes_started);
            CHECK(rt.lanes[1].source_row==rt.lanes[2].source_row);
        }
    }
    cleanup();
}
static void length_domains(void)
{
    fixture(4);pages->tracker.ticks_per_line=1;
    for(int row=0;row<256;++row)for(int lane=0;lane<8;++lane)pattern->cells[row][lane]=note(tile,row<4?60+row:99);
    pages->tracker.lanes[0].length=3;pages->tracker.lanes[1].length=7;
    pages->tracker.lanes[2].mode=TS_TRACKER_PATTERN;pages->tracker.lanes[2].length=6;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));
    for(unsigned tick=0;tick<=7;++tick) {
        to_tick(tick,1000,125);
        CHECK(rt.lanes[0].source_row==(tick%7)%3);
        CHECK(rt.lanes[1].source_row==tick%7);
        CHECK(rt.lanes[2].source_row==tick%6); /* Remains private at master seam. */
        CHECK(rt.lanes[3].source_row==tick%7);
        for(int lane=0;lane<8;++lane)CHECK(rt.lanes[lane].voice.note!=99);
    }
    CHECK(rt.loop_cycles==1 && rt.lanes[3].notes_started==5);
    /* CONTROL OFF means the physical reel, even with a longer LEN elsewhere. */
    pages->tracker.control_lane=3;pages->tracker.loop=0;prepare(1000);
    CHECK(ts_tracker_playback_start(&rt));to_tick(4,1000,125);CHECK(!rt.running);
    pages->tracker.control_lane=-1;pages->tracker.loop=1;
    pages->tracker.lanes[1].length=2;pages->tracker.lanes[2].length=0;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(3,1000,125);
    CHECK(rt.loop_cycles==1 && rt.lanes[0].source_row==0); /* Longest explicit 3 shortens physical 4. */
    pages->tracker.lanes[0].length=256;pages->tracker.lanes[2].length=256;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(255,1000,125);
    CHECK(rt.lanes[0].source_row==255 && rt.lanes[2].source_row==255 && rt.lanes[0].notes_started==4);
    to_tick(256,1000,125);CHECK(rt.loop_cycles==1 && rt.lanes[0].notes_started==5);
    /* FTL off bypasses LEN only for private tracks. Global bypass covers both. */
    pages->tracker.fasttracks_uses_length=0;prepare(1000);
    CHECK(ts_tracker_playback_start(&rt));to_tick(5,1000,125);
    CHECK(rt.lanes[0].source_row==5 && rt.lanes[2].source_row==1);
    pages->tracker.length_bypass=1;prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(5,1000,125);
    CHECK(rt.loop_cycles==1 && rt.lanes[0].source_row==1 && rt.lanes[2].source_row==1);
    CHECK(pages->tracker.lanes[0].length==256);
    cleanup();
}
static void directions_and_crossings(void)
{
    fixture(4);pages->tracker.ticks_per_line=1;
    for(int row=0;row<4;++row)for(int lane=0;lane<8;++lane)pattern->cells[row][lane]=note(tile,60+row);
    pages->tracker.lanes[0].mode=pages->tracker.lanes[1].mode=TS_TRACKER_PATTERN;
    pages->tracker.lanes[0].direction=TS_TRACKER_REVERSE;
    pages->tracker.lanes[1].direction=TS_TRACKER_PING_PONG;
    pages->tracker.lanes[2].direction=TS_TRACKER_REVERSE; /* Standard ignores private direction. */
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));
    unsigned bounce[]={0,1,2,3,2,1,0,1,2,3,2,1,0};
    for(unsigned tick=0;tick<13;++tick) {
        to_tick(tick,1000,125);
        CHECK(rt.lanes[0].source_row==(4-tick%4)%4);
        CHECK(rt.lanes[1].source_row==bounce[tick]);
        CHECK(rt.lanes[2].source_row==tick%4);
    }
    pages->tracker.lanes[0].length=pages->tracker.lanes[1].length=1;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(8,1000,125);
    CHECK(rt.lanes[0].source_row==0 && rt.lanes[1].notes_started==9);
    /* Five crossings in one tick must not discard inherited controls. */
    CHECK(ts_sister_tracker_set_rows(&pages->tracker,pattern->id,8));
    memset(pattern->cells,0,sizeof(pattern->cells));
    pages->tracker.lanes[0].length=0;pages->tracker.lanes[0].direction=TS_TRACKER_FORWARD;
    pages->tracker.lanes[0].ratio=16;
    pattern->cells[1][0]=(TsTrackerCell){.tile_id=tile,.has_volume=1,.volume=17};
    pattern->cells[2][0]=note(0,72);
    pattern->cells[3][0].note_kind=TS_TRACKER_NOTE_OFF;
    pattern->cells[4][0]=(TsTrackerCell){.has_volume=1,.volume=31};
    pattern->cells[5][0]=note(0,67);
    pages->tracker.fasttracks_uses_length=0;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(1,1000,125);
    CHECK(rt.lanes[0].notes_started==2 && rt.lanes[0].voice.note==67);
    CHECK(rt.lanes[0].volume==31 && rt.lanes[0].default_tile==tile);
    cleanup();
}
static void live_clock_changes(void)
{
    fixture(8);pages->tracker.ticks_per_line=6;
    for(int row=0;row<8;++row)pattern->cells[row][0]=note(tile,60+row);
    pages->tracker.lanes[0].mode=TS_TRACKER_PATTERN;pages->tracker.lanes[0].ratio=0;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(5,1000,125);
    CHECK(rt.lanes[0].accumulator==5 && rt.lanes[0].source_row==0);
    pages->tracker.lanes[0].ratio=1;prepare(1000); /* 5/12 -> 7/18, Tapehead integer normalization. */
    CHECK(rt.lanes[0].accumulator==7 && rt.lanes[0].notes_started==1);
    pages->tracker.ticks_per_line=3;prepare(1000);
    CHECK(rt.lanes[0].accumulator==3 && rt.tick==2);
    pages->tracker.lanes[0].direction=TS_TRACKER_REVERSE;prepare(1000);
    CHECK(rt.lanes[0].accumulator==3 && rt.lanes[0].source_row==0);
    to_tick(8,1000,125);CHECK(rt.lanes[0].source_row==7);
    double until=rt.until_tick,position=rt.lanes[0].voice.position;
    int phase=rt.lanes[0].accumulator;uint64_t count=rt.lanes[0].notes_started,elapsed=rt.elapsed_frames;
    ts_tracker_playback_pause(&rt,1);render(1000,1000);
    CHECK(rt.until_tick==until && rt.lanes[0].voice.position==position && rt.lanes[0].accumulator==phase && rt.elapsed_frames==elapsed);
    ts_tracker_playback_pause(&rt,0);render(1,2000);
    CHECK(rt.lanes[0].accumulator==phase && fabs(rt.until_tick-(until*2-1))<1e-8);
    pages->tracker.lanes[0].length=2;prepare(2000);
    CHECK(rt.lanes[0].source_row==7 && rt.lanes[0].notes_started==count);
    render(360,2000);CHECK(rt.lanes[0].source_row<2 && rt.lanes[0].voice.note<62);
    /* Definitions publish during a literal block but cannot affect its clock. */
    TsTrackerBlock b={pattern->id,2,3,0,0};CHECK(ts_tracker_playback_start_block(&rt,b));
    render(1,2000);count=rt.lanes[0].notes_started;
    pages->tracker.lanes[0].ratio=16;pages->tracker.control_lane=0;prepare(2000);
    render(120,2000);CHECK(rt.row==3 && rt.lanes[0].notes_started==count+1);
    cleanup();
}
static void control_clocks(void)
{
    fixture(4);pages->tracker.ticks_per_line=2;pages->tracker.loop=0;
    for(int row=0;row<4;++row)for(int lane=0;lane<8;++lane)pattern->cells[row][lane]=note(tile,60+row);
    pages->tracker.control_lane=0;pages->tracker.lanes[0].mode=TS_TRACKER_PATTERN;
    pages->tracker.lanes[0].ratio=0;pages->tracker.lanes[0].length=3;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(12,1000,125);
    CHECK(rt.running && rt.control_pending && rt.lanes[0].notes_started==4);
    to_tick(13,1000,125);CHECK(!rt.running);
    /* Reversing, muting and soloing do not remove CONTROL authority. */
    pages->tracker.loop=1;pages->tracker.lanes[0].muted=1;pages->tracker.lanes[0].direction=TS_TRACKER_REVERSE;
    prepare(1000);ts_tracker_playback_solo(&rt,2);CHECK(ts_tracker_playback_start(&rt));to_tick(13,1000,125);
    CHECK(rt.loop_cycles==1 && rt.tick==0 && rt.master_row==0 && rt.lanes[0].gain==0);
    pages->tracker.length_bypass=1;prepare(1000);
    CHECK(!rt.control_pending);CHECK(ts_tracker_playback_start(&rt));to_tick(8,1000,125);CHECK(rt.loop_cycles==1);
    /* Standard CONTROL without LEN owns the physical pattern, not longest LEN. */
    pages->tracker.length_bypass=0;pages->tracker.control_lane=1;pages->tracker.lanes[0].length=7;
    prepare(1000);CHECK(ts_tracker_playback_start(&rt));to_tick(8,1000,125);CHECK(rt.loop_cycles==1);
    cleanup();
}
int main(void)
{rational_clocks();length_domains();directions_and_crossings();live_clock_changes();control_clocks();block_loop();timing();pause_rate_and_stop();inheritance_and_ownership();missing_and_live_edits();native_reader_metadata();puts("SisterTracker playback checks passed");return 0;}
