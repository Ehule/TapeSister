#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sister_test_helpers.h"
#include "tapesister/keyboard_sequence.h"
#include <stdio.h>
#include <stdlib.h>

static void test_gate(int rate)
{
    TsKeyboardSequence s;
    TsKeyboardSequenceSource source = {0};
    float data[1024];
    for (int i=0;i<1024;i+=2) { data[i]=.3f;data[i+1]=-.15f; }
    TsSample sample = {.data=data,.frames=512,.sample_rate=(uint32_t)rate,.channels=2};
    source.count=1;
    source.voices[0]=(TsNoteVoice){.sample=&sample,.step=1,.gain=1,.active=1,
        .looping=1,.direction=1,.range_last=512};
    ts_keyboard_sequence_init(&s);
    TsKeyboardSequenceSettings p=s.settings;
    p.notes[0]=60;p.count=1;p.seconds=2;p.gate=1;
    ts_keyboard_sequence_set(&s,&p);ts_keyboard_sequence_source(&s,&source);
    assert(ts_keyboard_sequence_play(&s));
    TsStereoFrame previous={0};
    for(int i=0;i<rate/2;++i)previous=ts_keyboard_sequence_read(&s,rate);
    double elapsed=s.elapsed;int note=s.current_note;
    float largest=0;
    for(int sweep=0;sweep<12;++sweep) {
        p.gate=sweep%2 ? 1 : .05;
        ts_keyboard_sequence_set(&s,&p);
        assert(s.current_note==note && s.elapsed>=elapsed);
        for(int i=0;i<rate/100;++i) {
            TsStereoFrame f=ts_keyboard_sequence_read(&s,rate);
            largest=fmaxf(largest,fabsf(f.l-previous.l));
            assert(fabsf(f.r+f.l*.5f)<1e-6f);previous=f;
        }
    }
    printf("ARP gate %d Hz maximum edit jump %.8f\n",rate,largest);
    assert(largest <= .3f/(.005f*rate)+.00001f);
}

static void test_keyboard(int rate, int fm)
{
    TsInstrument instrument;char error[160];
    ts_instrument_init(&instrument);
    assert(ts_instrument_activate_silence(&instrument,4096,rate,error,sizeof(error)));
    for(size_t i=0;i<instrument.current.frames;++i)instrument.current.data[i]=.3f;
    TsNoteBank bank;ts_note_bank_init(&bank);bank.workbench_loop=1;
    TsTuning tuning={60,0};TsNoteEvent held,mouse;
    assert(ts_note_event_qwerty(&held,0,60));
    assert(ts_note_bank_start_tuned_event(&bank,&instrument,&tuning,TS_AUDITION_CURRENT,&held,1,rate)==TS_NOTE_STARTED);
    TsStereoFrame previous={0};
    for(int i=0;i<rate/100;++i)previous=ts_note_bank_read_stereo(&bank);
    float largest=0;
    for(int key=1;key<33;++key) {
        assert(ts_note_event_qwerty(&mouse,key,60));
        TsNoteStartResult result=fm ? ts_note_bank_start_sample_event(&bank,&instrument.current,&tuning,&mouse,0,rate) :
            ts_note_bank_start_tuned_event(&bank,&instrument,&tuning,TS_AUDITION_CURRENT,&mouse,0,rate);
        assert(result==TS_NOTE_STARTED);
        for(int i=0;i<rate/400;++i) {
            TsStereoFrame f=ts_note_bank_read_stereo(&bank);
            largest=fmaxf(largest,fabsf(f.l-previous.l));previous=f;
        }
        ts_note_bank_release_event(&bank,&mouse);
        assert(ts_note_bank_count(&bank)==1); /* UI/release semantics remain immediate. */
        for(int i=0;i<rate/400;++i) {
            TsStereoFrame f=ts_note_bank_read_stereo(&bank);
            largest=fmaxf(largest,fabsf(f.l-previous.l));previous=f;
        }
    }
    printf("Keyboard %s %d Hz maximum drag jump %.8f\n",fm?"FM":"tile",rate,largest);
    assert(largest<.015f); /* Baseline jumps by ~0.1, despite its note attack. */
    for(int i=0;i<rate/100;++i)previous=ts_note_bank_read_stereo(&bank);
    assert(fabsf(previous.l-.3f)<1e-5f);
    assert(ts_note_bank_release_latched_event(&bank,&held)==1);
    TsStereoFrame first=ts_note_bank_read_stereo(&bank);
    assert(fabsf(first.l-previous.l)<1e-5f);
    for(int i=0;i<rate/100;++i)previous=ts_note_bank_read_stereo(&bank);
    assert(previous.l==0 && previous.r==0);
    ts_instrument_free(&instrument);
}

static void test_group(int rate)
{
    TsInstrument instrument;
    assert(sister_test_make_tiles(&instrument,1,0,rate,4096));
    for(size_t i=0;i<instrument.current.frames;++i)instrument.current.data[i]=.3f;
    TsPerformanceBank *bank=calloc(1,sizeof(*bank));assert(bank);
    ts_performance_init(bank);bank->keyboard_loop=1;
    TsNoteEvent event;assert(ts_note_event_qwerty(&event,0,60));
    assert(ts_performance_trigger_group_event(bank,&instrument,1,&event,0,rate));
    TsStereoFrame previous={0},raw;
    for(int i=0;i<rate/100;++i)previous=ts_performance_read_stereo(bank,&raw);
    assert(fabsf(previous.l)>.1f);
    ts_performance_release_event(bank,&event);
    assert(ts_performance_count(bank)==0);
    ts_performance_collect_retired(bank);
    TsStereoFrame first=ts_performance_read_stereo(bank,&raw);
    printf("Group/Sister %d Hz release jump %.8f\n",rate,fabsf(first.l-previous.l));
    assert(fabsf(first.l-previous.l)<1e-6f);
    assert(fabsf(raw.l-first.l)<1e-6f);
    for(int i=0;i<rate/100;++i)previous=ts_performance_read_stereo(bank,NULL);
    assert(previous.l==0 && previous.r==0);
    ts_performance_clear(bank);
    assert(ts_performance_read_stereo(bank,NULL).l==0);
    ts_performance_free(bank);free(bank);ts_instrument_free(&instrument);
}

static void test_repeated_edits(void)
{
    TsSisterMachine *m=calloc(1,sizeof(*m));assert(m);
    assert(ts_sister_machine_init(m,48000,2,1));
    TsSisterParameters p=m->parameters;
    p.head1_level=.17f;p.input_gain=.23f;
    p.filter_type=TS_SISTER_FILTER_LOWPASS;p.filter_cutoff_hz=900;
    ts_sister_machine_set_parameters(m,&p);
    for(int i=0;i<3000;++i) {
        if(i%48==0) {p.fx.slot[0].gain_db=(i%96)?3:4;ts_sister_machine_set_parameters(m,&p);}
        ts_sister_machine_process_frame(m,(TsStereoFrame){.1f,.05f},(TsStereoFrame){0});
    }
    printf("Repeated unrelated edits: remaining head/input/filter ramps %u/%u/%u\n",
        m->head[0].level.remaining,m->input_gain.remaining,m->filter_ramp_remaining);
    assert(m->head[0].level.remaining==0 && m->input_gain.remaining==0 && m->filter_ramp_remaining==0);
    assert(fabsf(m->head[0].level.current-.17f)<1e-6f);
    ts_sister_machine_free(m);free(m);
}

static void test_snapshot_batch(void)
{
    TsSisterRuntime *r=calloc(1,sizeof(*r));assert(r);ts_sister_runtime_init(r);
    uint64_t before=atomic_load(&r->snapshot.revision);
    ts_sister_runtime_begin_audio_block(r);
    for(int i=0;i<256;++i)ts_sister_runtime_process_frame(r,NULL);
    assert(atomic_load(&r->snapshot.revision)==before);
    ts_sister_runtime_end_audio_block(r);
    assert(atomic_load(&r->snapshot.revision)==before+2);
    /* Single-frame callers still receive a fresh snapshot. */
    ts_sister_runtime_process_frame(r,NULL);
    assert(atomic_load(&r->snapshot.revision)==before+4);
    ts_sister_runtime_free(r);free(r);
}

int main(int argc,char **argv)
{
    int which=argc>1?atoi(argv[1]):0;
    int rates[]={44100,48000,96000};
    for(int i=0;i<3;++i) {
        if(!which || which==1)test_gate(rates[i]);
        if(!which || which==2){test_keyboard(rates[i],0);test_keyboard(rates[i],1);}
        if(!which || which==3)test_group(rates[i]);
    }
    if(!which || which==4)test_repeated_edits();
    if(!which || which==5)test_snapshot_batch();
    puts("Realtime edit regressions passed");return 0;
}
