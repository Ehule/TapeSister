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

int ts_sister_ui_prism_assigned(const TsSisterUiModel *m,int side)
{
    if(m->routing.prism.matrix_active && !m->prism_matrix_edit)return side?m->routing.prism.matrix_to:m->routing.prism.matrix_from;
    return m->parameters.prism.endpoint[side];
}
int ts_sister_ui_prism_state(const TsSisterUiModel *m,int endpoint)
{
    if(!m || endpoint<0 || endpoint>1)return 0;
    int pending=m->prism_browse[endpoint];
    return pending>0 && pending<=TS_PRISM_STATES?pending-1:ts_sister_ui_prism_assigned(m,endpoint);
}
TsPrismCaptureVisual ts_sister_ui_prism_capture_visual(const TsSisterUiModel *m,int endpoint)
{
    if(!m || endpoint<0 || endpoint>1)return TS_PRISM_CAPTURE_EMPTY;
    const TsPrismControls *c=&m->parameters.prism;
    int state=ts_sister_ui_prism_state(m,endpoint);
    if(state<0)return TS_PRISM_CAPTURE_EMPTY;
    if(!(c->captured&(1<<state)))return TS_PRISM_CAPTURE_EMPTY;
    if(!c->morph_enabled && m->prism_edit_endpoint==state+1)
        return prism_patch_matches(c,ts_prism_state_const(c,state)) ? TS_PRISM_CAPTURE_EDITING : TS_PRISM_CAPTURE_DIRTY;
    return TS_PRISM_CAPTURE_READY;
}

const char *ts_sister_ui_prism_edit_status(const TsSisterUiModel *m)
{
    static char label[64];const TsPrismControls *c=&m->parameters.prism;
    for(int side=0;side<2;++side) {
        int state=ts_sister_ui_prism_state(m,side);
        if(state!=ts_sister_ui_prism_assigned(m,side)) {
            snprintf(label,sizeof(label),"CHOOSE %c - %s",'A'+state,
                c->captured&(1<<state)?"CLICK TO MORPH":
                c->morph_enabled?"EMPTY / RIGHT-CLICK CAP TO EDIT":"EMPTY / CLICK CAP TO SAVE");return label;
        }
    }
    if(m->prism_matrix_edit) {
        snprintf(label,sizeof(label),"DRAFT %c / SAVE FOR NEXT VISIT",'A'+m->prism_matrix_edit-1);return label;
    }
    if(m->routing.prism.matrix_active)return "MATRIX / RIGHT CAP TO TAKE OVER";
    if(c->morph_enabled) {
        snprintf(label,sizeof(label),"%c/%c MORPH - SOUND LOCKED",ts_prism_state_letter(c->endpoint[0]),ts_prism_state_letter(c->endpoint[1]));return label;
    }
    int edit=m->prism_edit_endpoint-1;
    const TsPrismPatch *patch=ts_prism_state_const(c,edit);
    if(patch && (c->captured&(1<<edit))) {
        if(prism_patch_matches(c,patch))snprintf(label,sizeof(label),"EDIT %c - CLICK CAP %c TO SAVE",'A'+edit,'A'+edit);
        else snprintf(label,sizeof(label),"EDIT %c - NOT CAPTURED",'A'+edit);
        return label;
    }
    int matches=0;
    for(int state=0;state<TS_PRISM_STATES;++state)
        if((c->captured&(1<<state)) && prism_patch_matches(c,ts_prism_state_const(c,state)))matches=1;
    if(!matches)return "LIVE EDIT - NOT CAPTURED";
    if(!ts_prism_pair_ready(c))return "LIVE EDIT / CAPTURE OTHER END";
    snprintf(label,sizeof(label),"LIVE EDIT / %c+%c READY",ts_prism_state_letter(c->endpoint[0]),ts_prism_state_letter(c->endpoint[1]));return label;
}

void ts_sister_ui_prism_help(const TsSisterUiModel *m,int x,int y,char *help,size_t size)
{
    if(!help || !size)return;
    help[0]=0;
    if(!m || m->fx_page!=3 || m->preset_manage_open || m->midi_learn_active)return;
    const TsPrismControls *c=&m->parameters.prism;
#define TIP(message) do { snprintf(help,size,"%s",message);return; } while(0)
    if(m->prism_panel==3) {
        int cell=ts_sister_ui_prism_matrix_cell(x,y);
        if(cell>=0)TIP("CELL: wheel/left/right cycles HOLD, A-L. Middle clears. Shift-click sets sequence length. Changes apply next visit.");
        if(contains(x,y,16,48,70,22))TIP("BACK: return to Prism. Matrix keeps running across pages; playback keys and recording remain available.");
        if(contains(x,y,16,78,62,22))TIP("PLAY: start at step 01. A current blend finishes toward its nearer end first; no abrupt preset recall.");
        if(contains(x,y,86,78,90,22))TIP("STOP/RESET: hold the current sound and reset to step 01. The manual morph fader can take over this exact blend.");
        if(contains(x,y,184,78,62,22))TIP("LOOP: repeat the active sequence. With Loop off, the final step completes and its sound stays held.");
        if(contains(x,y,254,78,86,22))TIP("LENGTH: 1-64 cells, left to right then top to bottom. Wheel adjusts; Shift-click a cell sets the end.");
        if(contains(x,y,348,78,124,22))TIP("STEP: seconds per cell. Morph reaches its destination, then holds for any remaining time. Changes apply next step.");
        if(contains(x,y,480,78,136,22))TIP("MORPH: the existing Prism Time. Matrix caps it at STEP to avoid overlapping transitions; manual Time is unchanged.");
        if(contains(x,y,16,318,600,22))TIP("BANK A-L: open a draft without stopping playback. SAVE updates that letter for its next visit; current morph stays intact.");
        return;
    }
    if(m->prism_matrix_edit && contains(x,y,530,250,86,22))TIP("SAVE: capture this draft in its bank letter. Matrix uses it on the next visit, keeping the current transition intact.");
    if(contains(x,y,534,48,82,22))TIP("MATRIX: 64 steps referencing A-L, with Play, Stop/Reset, Loop and independent step duration.");
    if(contains(x,y,16,250,78,22))TIP("SOUND: lens count, spread, motion, body, wet/dry and glass color. The live diagram stays visible.");
    if(contains(x,y,102,250,92,22))TIP("PERFORM: twelve captured states A-L, two morph ends, ordered lens sequencer, snapping, Drift Rate and group octave.");
    if(contains(x,y,202,250,86,22))TIP("PRESETS: Prism-only bank, NEW / VARY and their lock groups. Sister tape and other FX stay as set.");
    if(m->prism_zoya_pose!=TS_PRISM_ZOYA_POSE_OFF &&
       (contains(x,y,18,108,104,117) || contains(x,y,510,108,112,117)) && m->prism_zoya.visible)
        TIP("NEBULA: amber source / spectral output. Sound controls shape the field; Wet/Dry balance its presence.");
    if(m->prism_panel==1) {
        if(contains(x,y,16,280,66,22) || contains(x,y,88,280,66,22)) {
            int endpoint=x>=88;
            static const char *const states[]={"EMPTY","CAPTURED","EDITING","EDITING - NOT CAPTURED"};
            const char *state=states[ts_sister_ui_prism_capture_visual(m,endpoint)];
            snprintf(help,size,"CAP %c: %s. %s",ts_prism_state_letter(ts_sister_ui_prism_state(m,endpoint)),state,
                c->morph_enabled ? "Right-click edits (even empty). Middle-click clears and unlocks." :
                "Wheel browses A-L; click confirms. Left-click saves edits; right recalls; middle clears.");
            return;
        }
        if(contains(x,y,160,280,138,22))TIP("TO: wheel browses A-L silently; click confirms/morphs over TIME. Change the silent end. The fader takes over.");
        if(contains(x,y,16,308,66,22))TIP("SEQUENCER ON/OFF: ordered members step; other lenses sustain. Mute/solo still apply. Uses its own audio clock.");
        if(contains(x,y,88,308,66,22))TIP("SEQUENCER EDIT: Ctrl-click lens numbers to append/remove steps. With EDIT off, Ctrl-click is additive solo.");
        if(contains(x,y,160,308,66,22))TIP("SEQUENCER RESET: restart at the first eligible step. Keeps the pattern and captured A-L sounds.");
        if(contains(x,y,232,308,66,22))TIP("SEQUENCER CLEAR: empty the ordered pattern. Captured A-L sounds stay intact; lenses return to normal sustain.");
        if(contains(x,y,472,308,144,22))TIP("DRAG SNAP: Free, Semitone, 50 Cent or 31-TET. Left/right-click cycles. Arrow-key step sizes stay independent.");
    }
    if(m->prism_panel==2) {
        if(contains(x,y,16,280,30,22))TIP("PREVIOUS PRISM PRESET: audition sound, keeping A-L. Shift-click recalls the full saved Prism setup, including A-L.");
        if(contains(x,y,276,280,30,22))TIP("NEXT PRISM PRESET: audition sound, keeping A-L. Shift-click recalls the full saved Prism setup, including A-L.");
        if(contains(x,y,52,280,218,22))TIP("PRESET: audition; A-L retained. Shift-click restores full setup. Right-click resets individual lens edits.");
        if(contains(x,y,314,280,90,22))TIP("SAVE NEW: store Prism in a new bank entry. Shift-click overwrites the selected entry. Includes A-L and sequence.");
        if(contains(x,y,412,280,96,22))TIP("NEW: fresh Prism sound. Unlocked trims return to unity; mute/solo and sequence clear. Captured A-L stay stored.");
        if(contains(x,y,516,280,100,22))TIP("VARY: develop the current sound with smaller changes. Respects group/slider locks and retains the sequence and A-L.");
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
            TIP("SOUND LOCKED DURING MORPH. Right-click either CAP button on PERFORM to recall its letter for editing.");
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
        case TS_SISTER_UI_PARAM_PRISM_MORPH: TIP("MORPH: blend selected letters; sound edits lock. Moving this fader takes over a timed move. MIDI learnable.");
        case TS_SISTER_UI_PARAM_PRISM_TIME: TIP("TIME: duration of TO A / TO B, 0.05-120 seconds. Timed morphing follows the audio clock.");
        case TS_SISTER_UI_PARAM_PRISM_SEQ_RATE: TIP("SEQUENCER STEP RATE: 0.05-32 steps/second. Runs independently of morphing and Mosaic transport.");
        case TS_SISTER_UI_PARAM_PRISM_RATE: TIP("DRIFT RATE: 0.005-40 Hz. Lenses have related independent rates/phases; changing speed preserves their motion phase.");
        case TS_SISTER_UI_PARAM_PRISM_OCTAVE: TIP("GROUP OCTAVE: saved in A-L captures. Morph/Matrix glide between registers; the readout follows. DRY stays original.");
        case TS_SISTER_UI_PARAM_BUFFER_SECONDS: TIP("SISTER BUFFER: length of the rolling tape memory in seconds. Independent of Prism's refraction setup.");
        default: break;
        }
    }
    switch(hit.action) {
    case TS_SISTER_UI_ACTION_PRISM_TOGGLE: TIP("PRISM ON/OFF: smoothly engage or bypass live refraction. The diagram shows settings even while bypassed.");
    case TS_SISTER_UI_ACTION_PRISM_MODE:
        if(c->morph_enabled)TIP("MORPH: sound controls are locked. On PERFORM, right-click either CAP to recall that letter for editing.");
        TIP("FACTORY: click next; wheel through 28 sounds. * = edited. LOW/HIGH vary register. Right-click resets lens edits.");
    case TS_SISTER_UI_ACTION_RECORD_FILE: TIP("REC FILE / STOP FILE: record final stereo output with Prism, FX, limiter and OUT gain. Recording continues across pages.");
    case TS_SISTER_UI_ACTION_CAPTURE: TIP("CAPTURE: record the selected Sister tap to the destination below. Separate from Prism CAP, which store Prism settings.");
    case TS_SISTER_UI_ACTION_OVERDUB: TIP("OVERDUB: record the selected Sister tap into the destination's existing material.");
    case TS_SISTER_UI_ACTION_TAP: TIP("CAPTURE TAP: choose the Sister head/mix to capture. With FILE and MIX selected, capture records final OUT.");
    case TS_SISTER_UI_ACTION_CAPTURE_FORMAT: TIP("CAPTURE FORMAT: mono or stereo for the selected tap. REC FILE always records final stereo output.");
    case TS_SISTER_UI_ACTION_DESTINATION: TIP("CAPTURE DESTINATION: current tile, next empty tile, or file. Prism CAP above store settings, not audio.");
    case TS_SISTER_UI_ACTION_PRESET_MANAGE: TIP("FULL SISTER PRESET BANK: manages tape, routing, effects and Prism together. Prism-only presets live on PRESETS above.");
    case TS_SISTER_UI_ACTION_PRESET_PREVIOUS: case TS_SISTER_UI_ACTION_PRESET_NEXT:
        TIP("FULL SISTER PRESET: recall the previous/next complete Sister setup, including tape and effects.");
    case TS_SISTER_UI_ACTION_POWER: TIP("SISTER POWER: enable the tape machine. With POWER off, Prism and ordinary FX can still process the direct program input.");
    case TS_SISTER_UI_ACTION_ROLL: TIP("SISTER ROLL: start/stop the rolling tape transport. Prism's sequencer runs independently.");
    case TS_SISTER_UI_ACTION_HOLD: TIP("SISTER HOLD: freeze writing into the rolling tape memory while its heads continue reading.");
    case TS_SISTER_UI_ACTION_CLEAR: TIP("SISTER CLEAR: clear the rolling tape memory. Sequencer CLEAR is on PERFORM; A-L store Prism settings.");
    case TS_SISTER_UI_ACTION_MONITOR: TIP("SISTER MONITOR: listen to the selected direct/tape-head mix when Sister POWER is on.");
    case TS_SISTER_UI_ACTION_PAGE: TIP("SISTER PAGE: cycle Tape, FX, Fallout and Prism. Playback and file recording continue.");
    case TS_SISTER_UI_ACTION_LIMITER_TOGGLE: TIP("FINAL LIMITER: toggle the linked stereo output limiter. GR shows its gain reduction; OUT meters show final output.");
    case TS_SISTER_UI_ACTION_MASTER_OUTPUT: TIP("FINAL OUT: master output gain after the Sister/Prism/FX signal path. Shared across workspaces.");
    default: break;
    }
#undef TIP
}
