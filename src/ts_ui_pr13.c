#define ts_ui_render ts_ui_render_legacy
#include "ts_ui.c"
#undef ts_ui_render

void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument)
{
    int active;
    ts_ui_render_legacy(fb, ui, instrument);
    if (fb == NULL || ui == NULL || instrument == NULL || ui->browser.mode != TS_BROWSER_CLOSED ||
        ui->config_open || ui->export_choice_open) return;

    active = instrument->active_bank_slot;

    /* PR13 removes Parent/Current/Set Current as user-facing state choices. */
    rect(fb, 247, 205, 288, 23, PAL_DESKTOP);
    frame(fb, 247, 205, 288, 23, RGB(36, 33, 37), RGB(80, 74, 82));
    if (active >= 0 && active < TS_BANK_SLOT_COUNT && instrument->bank[active].occupied) {
        char active_text[40];
        snprintf(active_text, sizeof(active_text), "ACTIVE SLOT %02d  EDIT THE WAVEFORM", active + 1);
        text(fb, 255, 213, active_text, PAL_EFFECT, 1);
    } else {
        text(fb, 255, 213, "CLICK A FAMILY SLOT TO EDIT IT", PAL_EFFECT, 1);
    }

    button(fb, 540, 205, 90,
           active >= 0 && active < TS_BANK_SLOT_COUNT && instrument->bank[active].locked ?
           "UNLOCK" : "LOCK SLOT",
           active >= 0 && active < TS_BANK_SLOT_COUNT && instrument->bank[active].locked);

    /* Make bipolar Drift visually explicit. */
    rect(fb, 230, 233, 100, 24, PAL_DESKTOP);
    text(fb, 230, 233, "DRIFT", RGB(222, 218, 214), 1);
    rect(fb, 230, 246, 100, 6, RGB(12, 12, 12));
    rect(fb, 279, 245, 2, 9, PAL_EFFECT);
    {
        float drift = instrument->process.drift;
        int knob;
        if (drift < 0.0f) drift = 0.0f;
        if (drift > 1.0f) drift = 1.0f;
        knob = 230 + (int)(94.0f * drift);
        rect(fb, knob, 243, 6, 12, PAL_MOUSE);
    }

    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        int x = 10 + (slot % 8) * 77;
        int y = 330 + (slot / 8) * 25;
        if (!instrument->bank[slot].occupied) continue;
        if (instrument->bank[slot].locked) {
            rect(fb, x + 61, y + 3, 8, 8, PAL_NOTE);
            text(fb, x + 63, y + 4, "L", RGB(12, 12, 12), 1);
        }
        if (slot == active) {
            rect(fb, x + 2, y + 21, 68, 2, PAL_EFFECT);
        }
    }
}
