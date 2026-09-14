/* UI-only capture feedback and contextual help. Never consulted by the DSP. */
static int prism_patch_matches(const TsPrismControls *c,const TsPrismPatch *p)
{
#define SAME(field) if(c->field!=p->field)return 0
    SAME(enabled);SAME(mode);SAME(lenses);SAME(spread);SAME(drift);SAME(focus);
    SAME(stereo);SAME(body);SAME(mix);SAME(output_db);SAME(dry_level);
    SAME(mute_mask);SAME(solo_mask);SAME(input_shape);SAME(output_shape);
    SAME(color);SAME(drift_rate);SAME(group_octave);SAME(snap);
#undef SAME
    return !memcmp(c->pitch_offset,p->pitch_offset,sizeof(p->pitch_offset)) &&
           !memcmp(c->pan_offset,p->pan_offset,sizeof(p->pan_offset)) &&
           !memcmp(c->trim_db,p->trim_db,sizeof(p->trim_db)) &&
           !memcmp(c->octave_offset,p->octave_offset,sizeof(p->octave_offset));
}

TsPrismCaptureVisual ts_sister_ui_prism_capture_visual(const TsSisterUiModel *m,int endpoint)
{
    if(!m || endpoint<0 || endpoint>1)return TS_PRISM_CAPTURE_EMPTY;
    const TsPrismControls *c=&m->parameters.prism;
    if(!(c->captured&(1<<endpoint)))return TS_PRISM_CAPTURE_EMPTY;
    if(!c->morph_enabled && m->prism_edit_endpoint==endpoint+1)
        return prism_patch_matches(c,endpoint?&c->b:&c->a) ? TS_PRISM_CAPTURE_EDITING : TS_PRISM_CAPTURE_DIRTY;
    return TS_PRISM_CAPTURE_READY;
}

const char *ts_sister_ui_prism_edit_status(const TsSisterUiModel *m)
{
    const TsPrismControls *c=&m->parameters.prism;
    if(c->morph_enabled)return "A/B MORPH - SOUND LOCKED";
    for(int i=0;i<2;++i) {
        TsPrismCaptureVisual state=ts_sister_ui_prism_capture_visual(m,i);
        if(state==TS_PRISM_CAPTURE_EDITING)return i?"EDIT B - CLICK CAP B TO SAVE":"EDIT A - CLICK CAP A TO SAVE";
        if(state==TS_PRISM_CAPTURE_DIRTY)return i?"EDIT B - NOT CAPTURED":"EDIT A - NOT CAPTURED";
    }
    int matches=((c->captured&1) && prism_patch_matches(c,&c->a)) ||
                ((c->captured&2) && prism_patch_matches(c,&c->b));
    if(!matches)return "LIVE EDIT - NOT CAPTURED";
    return c->captured==3 ? "LIVE EDIT / A+B READY" : "LIVE EDIT / CAPTURE OTHER END";
}

void ts_sister_ui_prism_help(const TsSisterUiModel *m,int x,int y,char *help,size_t size)
{
    if(!help || !size)return;
    help[0]=0;
    if(!m || m->fx_page!=3 || m->preset_manage_open || m->midi_learn_active)return;
    const TsPrismControls *c=&m->parameters.prism;
#define TIP(message) do { snprintf(help,size,"%s",message);return; } while(0)
    if(contains(x,y,16,250,78,22))TIP("SOUND: lens count, spread, motion, body, wet/dry and glass color. The live diagram stays visible.");
    if(contains(x,y,102,250,92,22))TIP("PERFORM: capture A/B, morph, ordered lens sequencer, drag snapping, Drift Rate and group octave.");
    if(contains(x,y,202,250,86,22))TIP("PRESETS: Prism-only bank, NEW / VARY and their lock groups. Sister tape and other FX stay as set.");
    if(m->prism_panel==1) {
        if(contains(x,y,16,280,66,22) || contains(x,y,88,280,66,22)) {
            int endpoint=x>=88;
            static const char *const states[]={"EMPTY","CAPTURED","EDITING","EDITING - NOT CAPTURED"};
            const char *state=states[ts_sister_ui_prism_capture_visual(m,endpoint)];
            snprintf(help,size,"CAP %c: %s. %s",'A'+endpoint,state,
                c->morph_enabled ? "SOUND LOCKED. Right-click to recall and edit, then left-click to recapture." :
                "Left-click saves the live sound here. Right-click restores the stored sound for editing.");
            return;
        }
        if(contains(x,y,160,280,138,22))TIP("TO A / TO B: morph to the captured endpoint over TIME. Capture both first; moving A/B takes over.");
        if(contains(x,y,16,308,66,22))TIP("SEQUENCER ON/OFF: ordered members step; other lenses sustain. Mute/solo still apply. Uses its own audio clock.");
        if(contains(x,y,88,308,66,22))TIP("SEQUENCER EDIT: Ctrl-click lens numbers to append/remove steps. With EDIT off, Ctrl-click is additive solo.");
        if(contains(x,y,160,308,66,22))TIP("SEQUENCER RESET: restart at the first eligible step. Keeps the pattern and captured A/B sounds.");
        if(contains(x,y,232,308,66,22))TIP("SEQUENCER CLEAR: empty the ordered pattern. Captured A/B sounds stay intact; lenses return to normal sustain.");
        if(contains(x,y,472,308,144,22))TIP("DRAG SNAP: Free, Semitone, 50 Cent or 31-TET. Left/right-click cycles. Arrow-key step sizes stay independent.");
    }
    if(m->prism_panel==2) {
        if(contains(x,y,16,280,30,22))TIP("PREVIOUS PRISM PRESET: recall refraction settings only. The bottom bank stores the full Sister setup.");
        if(contains(x,y,276,280,30,22))TIP("NEXT PRISM PRESET: recall refraction settings only. Tape, routing and other effects stay as set.");
        if(contains(x,y,52,280,218,22))TIP("PRISM PRESET: click to recall. Right-click resets individual lens edits while keeping global controls.");
        if(contains(x,y,314,280,90,22))TIP("SAVE NEW: store Prism in a new bank entry. Shift-click overwrites the selected entry. Includes A/B and sequence.");
        if(contains(x,y,412,280,96,22))TIP("NEW: fresh Prism sound. Unlocked trims return to unity; mute/solo and sequence clear. Captured A/B stay stored.");
        if(contains(x,y,516,280,100,22))TIP("VARY: develop the current sound with smaller changes. Respects group/slider locks and retains the sequence and A/B.");
        if(y>=308 && y<330 && x>=16 && x<616) {
            static const char *const descriptions[]={
                "PITCH LOCK: NEW/VARY retain mode, Spread, Focus, per-lens pitches/octaves, Group Octave and drag snap.",
                "MOTION LOCK: NEW/VARY retain Drift, Drift Rate, Stereo and per-lens pan offsets.",
                "GLASS LOCK: NEW/VARY retain input/output shapes and Color.",
                "MIX LOCK: NEW/VARY retain Body, Wet, Dry Level, Output, lens trims and mute/solo.",
                "COUNT LOCK: NEW/VARY retain the number of lenses.",
                "SEQUENCE LOCK: NEW/VARY retain the ordered pattern, on/off state and step rate."};
            int index=(x-16)/101;
            if(index<6 && (x-16)%101<95)TIP(descriptions[index]);
        }
    }
    if((contains(x,y,128,108,41,117) || contains(x,y,102,228,92,10)))
        TIP("INPUT GLASS: left-click next / right-click previous shape. Shapes change pitch intervals and tone; COLOR sets tone depth.");
    if((contains(x,y,442,108,41,117) || contains(x,y,416,228,92,10)))
        TIP("OUTPUT GLASS: left-click next / right-click previous. Maps pitch/pan and colors shifted voices before recombining.");
    if(ts_sister_ui_prism_strip_hit(x,y)>=0 || ts_sister_ui_prism_hit(m,x,y)>=0) {
        if(m->prism_seq_edit && ts_sister_ui_prism_strip_hit(x,y)>=0)
            TIP("LENS / SEQ EDIT: Ctrl-click appends/removes a step in click order. Shift-click mutes. Plain click selects for arrow keys.");
        if(ts_sister_ui_prism_strip_hit(x,y)>=0)
            TIP("SELECT: Up/Down 1 cent; Shift 1 semitone; Ctrl+Shift 1 octave. Left/Right .01 pan; Shift .10; Ctrl+Shift hard L/R.");
        TIP("DRAG: pitch/pan. Shift-click mute; Ctrl-click solo; right-click reset. Wheel trims 1 dB; Shift-wheel 0.1 dB.");
    }
    if(contains(x,y,TS_PRISM_SUSTAIN_X,TS_PRISM_FOOTER_Y,TS_PRISM_SUSTAIN_W,TS_PRISM_FOOTER_H))
        TIP("SUSTAIN: released keyboard/MIDI notes continue. Turn off to release them. Shared across workspaces; Shift+S toggles.");
    if(contains(x,y,560,350,70,17))TIP("OUTPUT STATUS: GR is gain reduction from the final linked limiter. LIM OFF means the limiter is bypassed.");
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(m,x,y);
    if(hit.action==TS_SISTER_UI_ACTION_PARAMETER) {
        if(c->morph_enabled && hit.index>=TS_SISTER_UI_PARAM_PRISM_LENSES && hit.index<=TS_SISTER_UI_PARAM_PRISM_OCTAVE &&
           hit.index!=TS_SISTER_UI_PARAM_PRISM_MORPH && hit.index!=TS_SISTER_UI_PARAM_PRISM_TIME && hit.index!=TS_SISTER_UI_PARAM_PRISM_SEQ_RATE)
            TIP("SOUND LOCKED DURING A/B MORPH. Right-click CAP A or CAP B on PERFORM to recall an endpoint for editing.");
        switch(hit.index) {
        case TS_SISTER_UI_PARAM_PRISM_LENSES: TIP("LENSES: 2-24 related wet voices. Lens 01 is the body anchor. The original DRY branch is separate.");
        case TS_SISTER_UI_PARAM_PRISM_SPREAD: TIP("SPREAD: widen the pitch intervals and ray separation, up to 200%. Focus contracts the fine intervals.");
        case TS_SISTER_UI_PARAM_PRISM_DRIFT: TIP("DRIFT: independent pitch wandering per lens, up to 200%. RATE on PERFORM controls its speed. Rays follow the audio.");
        case TS_SISTER_UI_PARAM_PRISM_FOCUS: TIP("FOCUS: converge fine pitch edits and added time toward zero. Intentional octave bands and octave transpositions remain.");
        case TS_SISTER_UI_PARAM_PRISM_STEREO: TIP("STEREO: automatic lateral spread of the lenses. Individual pan edits add to this placement through the output glass.");
        case TS_SISTER_UI_PARAM_PRISM_BODY: TIP("BODY: strengthen wet lens 01 and lower-octave voices, up to 300%. This is separate from the original DRY lane.");
        case TS_SISTER_UI_PARAM_PRISM_MIX: TIP("WET: crossfade original DRY branch with the refracted wet sum. At 100% the original branch is absent; lens 01 remains wet.");
        case TS_SISTER_UI_PARAM_PRISM_OUTPUT: TIP("OUTPUT: -12 to +12 dB trim on the refracted wet sum. Final Sister OUT and its linked limiter follow.");
        case TS_SISTER_UI_PARAM_PRISM_DRY: TIP("DRY LEVEL: original signal trim, up to 200%, before the Wet crossfade. At Wet 100% this branch is silent.");
        case TS_SISTER_UI_PARAM_PRISM_COLOR: TIP("COLOR: blend the input/output glass tone on shifted voices. Stock body and original dry remain clean.");
        case TS_SISTER_UI_PARAM_PRISM_MORPH: TIP("A/B: morph captured sounds; sound edits lock. Moving this fader takes over a timed move. MIDI learnable.");
        case TS_SISTER_UI_PARAM_PRISM_TIME: TIP("TIME: duration of TO A / TO B, 0.05-120 seconds. Timed morphing follows the audio clock.");
        case TS_SISTER_UI_PARAM_PRISM_SEQ_RATE: TIP("SEQUENCER STEP RATE: 0.05-32 steps/second. Runs independently of A/B morphing and Mosaic transport.");
        case TS_SISTER_UI_PARAM_PRISM_RATE: TIP("DRIFT RATE: 0.005-40 Hz. Lenses have related independent rates/phases; changing speed preserves their motion phase.");
        case TS_SISTER_UI_PARAM_PRISM_OCTAVE: TIP("GROUP OCTAVE: transpose every wet lens up/down three octaves after glass mapping, even at full Focus. DRY stays original.");
        case TS_SISTER_UI_PARAM_BUFFER_SECONDS: TIP("SISTER BUFFER: length of the rolling tape memory in seconds. Independent of Prism's refraction setup.");
        default: break;
        }
    }
    switch(hit.action) {
    case TS_SISTER_UI_ACTION_PRISM_TOGGLE: TIP("PRISM ON/OFF: smoothly engage or bypass live refraction. The diagram shows settings even while bypassed.");
    case TS_SISTER_UI_ACTION_PRISM_MODE:
        if(c->morph_enabled)TIP("A/B MORPH: sound controls are locked. On PERFORM, right-click CAP A/B to recall an endpoint for editing.");
        TIP("MODE: cycle seven pitch geometries. Right-click restores individual lens edits to stock, keeping global controls.");
    case TS_SISTER_UI_ACTION_RECORD_FILE: TIP("REC FILE / STOP FILE: record final stereo output with Prism, FX, limiter and OUT gain. Recording continues across pages.");
    case TS_SISTER_UI_ACTION_CAPTURE: TIP("CAPTURE: record the selected Sister tap to the destination below. Separate from CAP A/B, which store Prism settings.");
    case TS_SISTER_UI_ACTION_OVERDUB: TIP("OVERDUB: record the selected Sister tap into the destination's existing material.");
    case TS_SISTER_UI_ACTION_TAP: TIP("CAPTURE TAP: choose the Sister head/mix to capture. With FILE and MIX selected, capture records final OUT.");
    case TS_SISTER_UI_ACTION_CAPTURE_FORMAT: TIP("CAPTURE FORMAT: mono or stereo for the selected tap. REC FILE always records final stereo output.");
    case TS_SISTER_UI_ACTION_DESTINATION: TIP("CAPTURE DESTINATION: current tile, next empty tile, or file. CAP A/B above store settings, not audio.");
    case TS_SISTER_UI_ACTION_PRESET_MANAGE: TIP("FULL SISTER PRESET BANK: manages tape, routing, effects and Prism together. Prism-only presets live on PRESETS above.");
    case TS_SISTER_UI_ACTION_PRESET_PREVIOUS: case TS_SISTER_UI_ACTION_PRESET_NEXT:
        TIP("FULL SISTER PRESET: recall the previous/next complete Sister setup, including tape and effects.");
    case TS_SISTER_UI_ACTION_POWER: TIP("SISTER POWER: enable the tape machine. With POWER off, Prism and ordinary FX can still process the direct program input.");
    case TS_SISTER_UI_ACTION_ROLL: TIP("SISTER ROLL: start/stop the rolling tape transport. Prism's sequencer runs independently.");
    case TS_SISTER_UI_ACTION_HOLD: TIP("SISTER HOLD: freeze writing into the rolling tape memory while its heads continue reading.");
    case TS_SISTER_UI_ACTION_CLEAR: TIP("SISTER CLEAR: clear the rolling tape memory. Sequencer CLEAR is on PERFORM; A/B stores Prism settings.");
    case TS_SISTER_UI_ACTION_MONITOR: TIP("SISTER MONITOR: listen to the selected direct/tape-head mix when Sister POWER is on.");
    case TS_SISTER_UI_ACTION_PAGE: TIP("SISTER PAGE: cycle Tape, FX, Fallout and Prism. Playback and file recording continue.");
    case TS_SISTER_UI_ACTION_LIMITER_TOGGLE: TIP("FINAL LIMITER: toggle the linked stereo output limiter. GR shows its gain reduction; OUT meters show final output.");
    case TS_SISTER_UI_ACTION_MASTER_OUTPUT: TIP("FINAL OUT: master output gain after the Sister/Prism/FX signal path. Shared across workspaces.");
    default: break;
    }
#undef TIP
}
