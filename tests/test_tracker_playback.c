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
int main(void)
{timing();pause_rate_and_stop();inheritance_and_ownership();missing_and_live_edits();native_reader_metadata();puts("SisterTracker playback checks passed");return 0;}
