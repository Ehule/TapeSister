#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define TAPESISTER_MIDI_QUEUE_TEST 1
#include "../src/ts_midi_input.c"
#include <SDL.h>
#include <assert.h>
static void notify_event(void *unused) {
    (void)unused;SDL_Event e;SDL_zero(e);e.type=SDL_USEREVENT;assert(SDL_PushEvent(&e)==1);
}
static int send_note(void *p) {
    SDL_Delay(10);
    TsMidiEvent e;assert(ts_midi_decode_short_message(0x90,60,100,&e));midi_enqueue(p,&e);return 0;
}
int main(void) {
    SDL_SetMainReady();assert(!SDL_Init(SDL_INIT_TIMER|SDL_INIT_EVENTS));
    TsMidiInput *input=ts_midi_input_create();assert(input);ts_midi_input_set_wake(input,notify_event,NULL);
    SDL_Thread *sender=SDL_CreateThread(send_note,"test MIDI wake",input);assert(sender);
    SDL_Event event;assert(SDL_WaitEventTimeout(&event,2000));assert(event.type==SDL_USEREVENT);
    TsMidiEvent note;assert(ts_midi_input_poll(input,&note));assert(note.note.midi_note==60 && note.action==TS_MIDI_ACTION_NOTE_ON);
    assert(!ts_midi_input_poll(input,&note));SDL_WaitThread(sender,NULL);
    /* Full queue stays bounded; wake follows publication, never precedes it. */
    for(int i=0;i<TS_MIDI_QUEUE_CAPACITY;i++)midi_enqueue(input,&note);
    assert(atomic_load(&input->dropped)==1);
    assert(ts_midi_input_poll(input,&note));assert(note.action==TS_MIDI_ACTION_PANIC);
    assert(!ts_midi_input_poll(input,&note));
    assert(ts_midi_decode_short_message(0x90,64,100,&note));midi_enqueue(input,&note);
    assert(ts_midi_input_poll(input,&note) && note.note.midi_note==64);
    ts_midi_input_destroy(input);SDL_Quit();puts("MIDI queue publication wakes an idle event wait");return 0;
}
