/* Outer clock tests use the real note renderer, independently of UI refresh. */
static void prepare_slots(int mode, int loop, double seconds)
{
    ts_keyboard_sequence_init(&sequence);
    TsKeyboardSequenceBank bank = ts_keyboard_sequence_export(&sequence);
    const int slots[] = {5, 2, 0};
    for (int i = 0; i < 3; ++i) {
        int slot = slots[i];
        bank.order[i] = slot;
        bank.slot[slot].count = 3;
        bank.slot[slot].mode = TS_KEYBOARD_SEQUENCE_ORDER;
        bank.slot[slot].seconds = .2;
        bank.slot[slot].gate = 1;
        for (int j = 0; j < 3; ++j) bank.slot[slot].notes[j] = 48 + slot + j * 4;
    }
    bank.order_count = 3;
    bank.sequence = (TsKeyboardSlotSequence){1, mode, loop, seconds};
    ts_keyboard_sequence_set_bank(&sequence, &bank);
    ts_keyboard_sequence_source(&sequence, &source);
    assert(ts_keyboard_sequence_play(&sequence));
}

static void test_slot_sequence(void)
{
    /* Empty live copy is sample-for-sample continuous, even mid-gate/LFO. */
    prepare(TS_KEYBOARD_SEQUENCE_ORDER);
    TsKeyboardSequenceSettings settings = sequence.settings;
    settings.lfo_enabled = 1; settings.lfo_seconds = .05; settings.lfo_depth = .7;
    settings.gate = .6; ts_keyboard_sequence_set(&sequence, &settings); render(19);
    TsKeyboardSequence unchanged = sequence;
    assert(ts_keyboard_sequence_select_slot(&sequence, 5));
    assert(sequence.active_slot == 5 && sequence.bank.selected == 5);
    assert(sequence.elapsed == unchanged.elapsed && sequence.lfo_phase == unchanged.lfo_phase);
    assert(!memcmp(sequence.voices, unchanged.voices, sizeof(sequence.voices)));
    for (int i = 0; i < 200; ++i) {
        TsStereoFrame a = ts_keyboard_sequence_read(&sequence, 1000);
        TsStereoFrame b = ts_keyboard_sequence_read(&unchanged, 1000);
        assert(a.l == b.l && a.r == b.r);
    }
    settings = sequence.settings; settings.notes[0] = 55;
    ts_keyboard_sequence_set(&sequence, &settings);
    assert(sequence.bank.slot[0].notes[0] == 67 && sequence.bank.slot[5].notes[0] == 55);
    ts_keyboard_sequence_stop(&sequence);
    assert(ts_keyboard_sequence_select_slot(&sequence, 6));
    assert(!sequence.running && !sequence.settings.count && sequence.settings.volume == 1 && sequence.settings.seconds == .25);
    assert(ts_keyboard_sequence_select_slot(&sequence, 5));
    assert(!sequence.running && sequence.settings.notes[0] == 55); /* Never erase a stored pattern. */

    const int expected[4][8] = {{0,2,5,0,2,5,0,2}, {0,5,2,0,5,2,0,5},
                               {0,2,5,2,0,2,5,2}, {0,5,2,0,5,2,0,5}};
    for (int mode = 0; mode < 4; ++mode) {
        prepare_slots(mode, 1, .05);
        TsKeyboardSequenceBank saved = ts_keyboard_sequence_export(&sequence);
        for (int step = 0; step < 8; ++step) {
            render(1);
            assert(sequence.active_slot == expected[mode][step]);
            assert(sequence.current_note == 48 + sequence.active_slot); /* Outer < one inner note. */
            render(49);
        }
        TsKeyboardSequenceBank after = ts_keyboard_sequence_export(&sequence);
        assert(!memcmp(&saved, &after, sizeof(saved))); /* Automatic clock never dirties the project. */
    }
    /* ORDER remembers the first-populated order, not numerical position. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_ORDER, 1, .05);
    ts_keyboard_sequence_select_slot(&sequence, 5); render(51);
    assert(sequence.active_slot == 2); render(50); assert(sequence.active_slot == 0);
    prepare_slots(TS_KEYBOARD_SEQUENCE_RANDOM, 1, .05);
    for (int cycle = 0; cycle < 20; ++cycle) {
        unsigned seen = 0;
        for (int i = 0; i < 3; ++i) {
            render(1); assert(!(seen & (1u << sequence.active_slot)));
            seen |= 1u << sequence.active_slot; render(49);
        }
        assert(seen == ((1u << 0) | (1u << 2) | (1u << 5)));
    }
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 0, .05);
    render(150); assert(ts_keyboard_sequence_active(&sequence));
    render(1); assert(!ts_keyboard_sequence_active(&sequence)); render(6); assert(render(20) == 0);

    /* Full 16-slot bounce reaches both endpoints without overrunning its bounded plan. */
    TsKeyboardSequenceBank full; ts_keyboard_sequence_bank_default(&full);
    for (int i = 0; i < 16; ++i) { full.slot[i].count = 1; full.slot[i].notes[0] = 60 + i; }
    full.selected = 15; full.sequence = (TsKeyboardSlotSequence){1, TS_KEYBOARD_SEQUENCE_UP_DOWN, 1, .05};
    ts_keyboard_sequence_set_bank(&sequence, &full); assert(ts_keyboard_sequence_play(&sequence));
    assert(sequence.slot_count == 30);
    render(751); assert(sequence.active_slot == 0);
    render(750); assert(sequence.active_slot == 15);
    for (int i = 1; i < 16; ++i) full.slot[i].count = 0;
    full.selected = 0; full.sequence.loop = 0;
    ts_keyboard_sequence_set_bank(&sequence, &full); assert(ts_keyboard_sequence_play(&sequence));
    assert(sequence.slot_count == 1); render(51); assert(!ts_keyboard_sequence_active(&sequence));

    /* Independent note ONCE can finish while the outer clock waits and advances. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .1);
    settings = sequence.settings; settings.count = 1; settings.loop = 0; settings.seconds = .03;
    ts_keyboard_sequence_set(&sequence, &settings); render(40);
    assert(!sequence.running && sequence.slot_running);
    render(61); assert(sequence.running && sequence.active_slot == 2);
    TsKeyboardSlotSequence q = sequence.bank.sequence; q.enabled = 0;
    ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    assert(sequence.running && !sequence.slot_running);
    render(200); assert(sequence.active_slot == 2);
    q.enabled = 1; ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    render(101); assert(sequence.active_slot == 5);
    ts_keyboard_sequence_stop(&sequence); assert(!sequence.slot_running);
    render(400); assert(sequence.active_slot == 5);

    /* Live time edits are deferred to the next slot, manual selection resets the outer hold. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05); render(20);
    q = sequence.bank.sequence; q.seconds = .2;
    ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    assert(sequence.slot_duration == .05); render(31);
    assert(sequence.active_slot == 2 && sequence.slot_duration == .2);
    render(90); ts_keyboard_sequence_select_slot(&sequence, 5);
    assert(sequence.slot_elapsed == 0 && sequence.slot_running);
    render(200); assert(sequence.active_slot == 5); render(1); assert(sequence.active_slot == 0);

    /* Empty references are skipped, including edits during a running cycle. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05);
    sequence.bank.slot[2].count = 0; render(51); assert(sequence.active_slot == 5);
    settings = sequence.settings; settings.count = 0; ts_keyboard_sequence_set(&sequence, &settings);
    assert(sequence.slot_running && !sequence.running); render(50); assert(sequence.active_slot == 0);
    settings = sequence.settings; settings.count = 0; ts_keyboard_sequence_set(&sequence, &settings);
    assert(!ts_keyboard_sequence_active(&sequence));

    /* Optional minimum: allow one full inner traversal, including bounce's return leg. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05);
    ts_keyboard_sequence_set_slot_policy(&sequence, 1);
    assert(fabs(sequence.slot_duration - .6) < 1e-12);
    render(600); assert(sequence.active_slot == 0); render(1); assert(sequence.active_slot == 2);
    settings = sequence.settings; settings.mode = TS_KEYBOARD_SEQUENCE_UP_DOWN;
    ts_keyboard_sequence_set(&sequence, &settings);
    assert(sequence.slot_duration >= .8);
    double deadline = sequence.slot_duration;
    settings.volume = .7; ts_keyboard_sequence_set(&sequence, &settings);
    assert(sequence.slot_duration == deadline); /* Fader gestures must not postpone the next slot. */

    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05);
    settings = sequence.settings; settings.seconds = .0507;
    ts_keyboard_sequence_set(&sequence, &settings);
    ts_keyboard_sequence_set_slot_policy(&sequence, 1);
    render(153); assert(sequence.active_slot == 0);
    render(1); assert(sequence.active_slot == 2); /* Full three rounded 51 ms notes. */

    /* Fast outer, hour-long inner, simultaneous gain changes: bounded 5 ms transitions. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05);
    for (int i = 0; i < 16; ++i) sequence.bank.slot[i].seconds = 3600;
    sequence.bank.slot[2].volume = 0; sequence.bank.slot[5].volume = 2;
    float previous = 0;
    for (int i = 0; i < 3000; ++i) {
        TsStereoFrame out = ts_keyboard_sequence_read(&sequence, 1000);
        assert(isfinite(out.l) && fabs(out.l - previous) < .121);
        assert(sequence.current_note == 48 + sequence.active_slot);
        previous = out.l;
    }
    /* A real pitched waveform remains continuous at fast multi-slot boundaries. */
    for (int i = 0; i < 256; ++i) { data[2*i] = .3f * (float)sin(i * .13); data[2*i+1] = -data[2*i]; }
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05);
    previous = 0;
    for (int i = 0; i < 10000; ++i) {
        TsStereoFrame out = ts_keyboard_sequence_read(&sequence, 1000);
        assert(isfinite(out.l) && fabs(out.l - previous) < .14);
        previous = out.l;
    }
    for (int i = 0; i < 256; ++i) { data[2*i] = .3f; data[2*i+1] = -.3f; }
    /* A negotiated rate change keeps elapsed seconds and the next deadline. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .05); render(25);
    for (int i = 0; i < 1200; ++i) ts_keyboard_sequence_read(&sequence, 48000);
    assert(sequence.active_slot == 0);
    ts_keyboard_sequence_read(&sequence, 48000); assert(sequence.active_slot == 2);
    /* Fractional periods retain the remainder instead of rounding every slot. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_UP, 1, .0503);
    render(50301); assert(sequence.active_slot == 2); /* 1000 transitions modulo 3. */
    assert(sequence.slot_elapsed < .002);
    q = sequence.bank.sequence; q.seconds = 1200; ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    ts_keyboard_sequence_reset(&sequence); assert(sequence.slot_duration == 1200);
    sequence.slot_elapsed = 1199.999; render(2); assert(sequence.slot_elapsed < .002);
    q.seconds = 0; ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    assert(sequence.bank.sequence.seconds == .05);
    q.seconds = INFINITY; ts_keyboard_sequence_set_slot_sequence(&sequence, &q);
    assert(sequence.bank.sequence.seconds == 10);

    /* Persist configuration/order while running, never elapsed time or current auto selection. */
    prepare_slots(TS_KEYBOARD_SEQUENCE_ORDER, 1, .05); render(51);
    TsSisterProjectState project, loaded; char error[160]; int present;
    ts_sister_project_state_init(&project, 1000);
    project.keyboard_sequence = ts_keyboard_sequence_export(&sequence);
    assert(sequence.active_slot == 5 && project.keyboard_sequence.selected == 0);
    assert(ts_sister_project_state_save_file(&project, "test-arp-chain.ini", error, sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded, "test-arp-chain.ini", 1000, &present, error, sizeof(error)));
    assert(loaded.keyboard_sequence.selected == project.keyboard_sequence.selected);
    assert(loaded.keyboard_sequence.order_count == 3);
    assert(!memcmp(loaded.keyboard_sequence.order, project.keyboard_sequence.order, sizeof(project.keyboard_sequence.order)));
    assert(loaded.keyboard_sequence.sequence.enabled == 1 && loaded.keyboard_sequence.sequence.loop == 1);
    assert(loaded.keyboard_sequence.sequence.seconds == .05 && loaded.keyboard_sequence.sequence.mode == TS_KEYBOARD_SEQUENCE_ORDER);
    ts_keyboard_sequence_set_bank(&sequence, &loaded.keyboard_sequence);
    assert(!ts_keyboard_sequence_active(&sequence) && sequence.active_slot == 0);
    assert(sequence.bank.sequence.enabled && sequence.bank.sequence.mode == TS_KEYBOARD_SEQUENCE_ORDER);
    assert(ts_keyboard_sequence_bank_read(&sequence.bank, "Arp.SlotOrder", "0,16") == -1);
    assert(ts_keyboard_sequence_bank_read(&sequence.bank, "Arp.Sequence.Seconds", "nan") == -1);
    assert(ts_keyboard_sequence_bank_read(&sequence.bank, "Arp.Sequence.Mode", "99999999999999999999999") == -1);
    FILE *legacy = fopen("test-arp-chain.ini", "wb"); assert(legacy);
    fputs("TapeSister Sister Project State\nVersion=26\nPageCount=1\nArp.SelectedSlot=9\n"
          "Arp.Slot.9.Notes=67,71,74\nArp.Slot.2.Notes=60\n", legacy); fclose(legacy);
    assert(ts_sister_project_state_load_file(&loaded, "test-arp-chain.ini", 1000, &present, error, sizeof(error)));
    assert(loaded.keyboard_sequence.selected == 9 && loaded.keyboard_sequence.slot[9].notes[1] == 71);
    assert(!loaded.keyboard_sequence.sequence.enabled && loaded.keyboard_sequence.sequence.seconds == 10);
    assert(loaded.keyboard_sequence.order_count == 2 && loaded.keyboard_sequence.order[0] == 2 && loaded.keyboard_sequence.order[1] == 9);
    remove("test-arp-chain.ini");
}
