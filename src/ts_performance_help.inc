/* Delayed footer help leaves performance controls and operation messages intact. */
static const char *performance_help(const TsUiState *ui,int x,int y)
{
#define H(X,Y,W,H,T) if(x>=(X) && x<(X)+(W) && y>=(Y) && y<(Y)+(H))return (T)
    if(ui->router_open)return router_help(ui,x,y);
    if(ui->master_eq_open) {
        H(528,340,88,25,"OPEN GLOBAL ROUTER; F9 ALSO OPENS/CLOSES ROUTING");
        H(396,44,78,24,"BYPASS ALL EQ BANDS FOR COMPARISON; YOUR SETTINGS ARE RETAINED");
        H(484,44,96,24,"RESET EQ: CLICK TWICE TO RESTORE FLAT BANDS AND BYPASS; LIMITER/OUT STAY SET");
        H(596,44,24,24,"CLOSE EQ; PROCESSING AND PLAYBACK CONTINUE");
        if(x>=35 && x<606 && y>=70 && y<245 && ts_ui_master_eq_node(ui,x,y)>=0)
            return "DRAG: FREQUENCY/GAIN; WHEEL: Q; SHIFT: FINE; MIDDLE: BYPASS; RIGHT: ZERO GAIN";
        H(43,78,555,156,"COMBINED EQ RESPONSE; NUMBERED NODES SELECT BANDS; AMBER IS THE ACTIVE CURVE");
        for(int i=0;i<5;++i) {
            H(20+i*88,252,76,24,"CLICK: BAND BYPASS; CTRL-CLICK: SOLO THIS FILTER / RESTORE ALL BANDS");
        }
        H(90,278,112,23,"FILTER TYPE: LEFT/RIGHT CLICK CYCLES BELL, SHELVES, PASSES AND NOTCH");
        H(20,303,188,27,"BAND FREQUENCY: DRAG OR WHEEL; SHIFT FOR FINE ADJUSTMENTS; RIGHT-CLICK RESETS");
        H(224,303,188,27,ts_master_eq_has_gain(ui->config.master_eq.band[ui->master_eq_band].type)?
          "BAND GAIN: CUT/BOOST UP TO 12 DB; WHEEL ADJUSTS; SHIFT: FINE; RIGHT: ZERO DB":
          "GAIN DOES NOT APPLY TO HIGH/LOW PASS OR NOTCH; ADJUST FREQUENCY AND Q");
        H(428,303,188,27,"Q: HIGHER IS NARROWER / MORE RESONANT; WHEEL ADJUSTS; SHIFT: FINE; RIGHT: RESET");
        H(20,341,104,23,"TOGGLE THE LINKED STEREO LIMITER AFTER EQ; BOOSTS CAN INCREASE GAIN REDUCTION");
        H(328,337,182,29,"FINAL OUTPUT LEVEL AFTER EQ AND LIMITER; ALSO AFFECTS FILE OUT RECORDING");
        return "";
    }
    if(!ts_ui_keyboard_sequence_visible(ui))return "";
    H(10,313,46,17,"SHOW/HIDE ARP CONTROLS; SHIFT+SPACE STARTS/STOPS ARP EVEN WITH THE PANEL CLOSED");
    if(!ui->keyboard_sequence_open)return "";
    if(ui->fm_open)y-=TS_ARP_PANEL_FM_OFFSET;
    H(20,220,188,26,"ARP VOLUME ONLY: 0-200%; HELD DRONES KEEP THEIR LEVEL; WHEEL / SHIFT: FINE; RIGHT: 100%");
    H(220,220,64,23,"TOGGLE SINE VOLUME LFO FOR ARP ONLY; ITS CLOCK CONTINUES WITH THE PANEL CLOSED");
    H(296,220,164,26,"LFO CYCLE: 50 MS TO 1 HOUR; INDEPENDENT OF STEP TIME; WHEEL / SHIFT: FINE; RIGHT: 4 SEC");
    H(476,220,144,26,"LFO DEPTH: 0% STEADY, 100% FADES TO SILENCE; VOLUME IS THE PEAK; RIGHT: 50%");
    H(20,252,64,23,"START/STOP ARP ONLY; HELD NOTES AND MIDI KEEP PLAYING; SHIFT+SPACE ALSO TOGGLES");
    H(90,252,100,23,"NOTE ORDER: UP, DOWN, UP/DOWN, ORDER OR RANDOM; RIGHT-CLICK CYCLES BACKWARD");
    H(196,252,58,23,"RESET TO THE FIRST STEP; A STOPPED SEQUENCE STAYS SILENT");
    H(260,252,58,23,"CLEAR SELECTED ARP NOTES AND STOP THE SEQUENCE; MANUAL NOTES KEEP PLAYING");
    H(324,252,100,23,"COPY YOUR HELD QWERTY CHORD INTO ARP WITHOUT RELEASING THE ORIGINAL NOTES");
    H(430,252,80,23,"EDIT ON: KEYS SET NOTE ORDER; EDIT OFF: PLAY LIVE; CTRL-CLICK ALWAYS SELECTS");
    H(516,252,64,23,"LOOP REPEATS THE SEQUENCE; ONCE PLAYS ONE TRAVERSAL THEN STOPS");
    H(586,252,34,23,"CLOSE ARP CONTROLS; SEQUENCING CONTINUES; SHIFT+SPACE STARTS/STOPS");
    H(20,281,248,26,"STEP: TIME BETWEEN NOTES, 30 MS TO 1 HOUR; WHEEL ADJUSTS; SHIFT: FINE; RIGHT: RESET");
    H(282,281,136,26,"GATE: HOW MUCH OF EACH STEP SOUNDS, 5-100%; WHEEL ADJUSTS; RIGHT-CLICK: 80%");
#undef H
    return "";
}

void ts_ui_performance_hover(TsUiState *ui,int x,int y,uint32_t now,int blocked)
{
    if(!ui)return;
    if(blocked || x<0 || y<0 || ui->master_eq_drag || ui->keyboard_sequence_drag ||
       ui->master_output_dragging || ui->midi_learn_active || ui->file_busy ||
       ui->browser.mode!=TS_BROWSER_CLOSED || ui->portal.open || ui->import_preview_open ||
       ui->config_open || ui->palette_open || ui->transform_open || ui->drone_open ||
       ui->mosaic_edit_choice || ui->exit_confirm_open || ui->project_overwrite_confirm_open ||
       ui->overdub_confirm_open || ui->load_selection_choice_open || ui->export_choice_open ||
       ui->fm_bank_choice_open || ui->fm_full_choice_open || ui->renaming_bank_slot>=0 ||
       ui->renaming_recipe_slot>=0 || ui->exchange_dialog!=TS_UI_EXCHANGE_NONE) {
        ui->performance_tooltip[0]=ui->performance_hover[0]=0;return;
    }
    const char *help=performance_help(ui,x,y);
    if(strcmp(help,ui->performance_hover)) {
        snprintf(ui->performance_hover,sizeof(ui->performance_hover),"%s",help);
        snprintf(ui->performance_hover_status,sizeof(ui->performance_hover_status),"%s",ui->status);
        ui->performance_hover_due=now+600u;ui->performance_tooltip[0]=0;
    } else if(strcmp(ui->performance_hover_status,ui->status)) {
        snprintf(ui->performance_hover_status,sizeof(ui->performance_hover_status),"%s",ui->status);
        ui->performance_hover_due=now+2400u;ui->performance_tooltip[0]=0;
    }
    if(*help && (int32_t)(now-ui->performance_hover_due)>=0)
        snprintf(ui->performance_tooltip,sizeof(ui->performance_tooltip),"%s",help);
}
