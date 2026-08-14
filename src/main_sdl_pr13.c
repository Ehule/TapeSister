#include "tapesister/pr13.h"
#include "tapesister/ui.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TsInstrument *p13_inst;
static TsUiState *p13_ui;
static int p13_slot = -1;
static int p13_x, p13_min, p13_max, p13_y;
static int p13_seen_undo = -1, p13_seen_redo = -1;

static void p13_rect(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            fb->pixels[yy * TS_UI_WIDTH + xx] = c;
}

static const char *p13_glyph(char c)
{
    switch(c){
    case 'N': return "10001110011010110011100011000110001";
    case 'E': return "11111100001000011110100001000011111";
    case 'W': return "10001100011000110101101011010101010";
    case 'R': return "11110100011000111110101001001010001";
    case 'S': return "01111100001000001110000010000111110";
    case 'D': return "11110100011000110001100011000111110";
    case 'L': return "10000100001000010000100001000011111";
    case 'O': return "01110100011000110001100011000101110";
    case 'C': return "01111100001000010000100001000001111";
    case 'K': return "10001100101010011000101001001010001";
    case 'U': return "10001100011000110001100011000101110";
    default: return "00000000000000000000000000000000000";
    }
}

static void p13_text(TsFramebuffer *fb,int x,int y,const char *s,uint32_t color)
{
    for(;*s;++s,x+=6){const char *g=p13_glyph(*s>='a'&&*s<='z'?*s-32:*s);for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(g[yy*5+xx]=='1')p13_rect(fb,x+xx,y+yy,1,1,color);}
}

static void p13_button(TsFramebuffer *fb,int x,int w,const char *label,int active)
{
    p13_rect(fb,x,205,w,23,active?0xff5d555du:0xff3b383bu);
    p13_rect(fb,x,205,w,2,active?0xffffd265u:0xff8c858cu);
    p13_rect(fb,x,226,w,2,0xff0e0e0eu);
    p13_text(fb,x+7,213,label,active?0xffffd265u:0xfff5f2ebu);
}

static int p13_process_differs(const TsProcessRecipe *a,const TsProcessRecipe *b)
{
    return memcmp(a,b,sizeof(*a))!=0;
}

static int p13_metadata_differs(const TsInstrument *inst, const TsBankSlot *slot)
{
    return slot->tuning.root_note != inst->tuning.root_note ||
           fabsf(slot->tuning.fine_tune_cents - inst->tuning.fine_tune_cents) > 0.0001f ||
           slot->audible_tuning.root_note != inst->audible_tuning.root_note ||
           fabsf(slot->audible_tuning.fine_tune_cents - inst->audible_tuning.fine_tune_cents) > 0.0001f ||
           (slot->has_process && p13_process_differs(&slot->process,&inst->process)) ||
           slot->has_loop != inst->has_loop || slot->loop_first != inst->loop_first ||
           slot->loop_last != inst->loop_last || slot->loop_mode != inst->loop_mode ||
           fabsf(slot->loop_crossfade_ms - inst->loop_crossfade_ms) > 0.0001f;
}

static void p13_reconcile(TsInstrument *inst, TsUiState *ui)
{
    TsBankSlot *slot; char error[160]; int history_changed;
    if (p13_slot < 0 || p13_slot >= TS_BANK_SLOT_COUNT || !inst->bank[p13_slot].occupied) return;
    slot = &inst->bank[p13_slot];
    history_changed = inst->undo_count != p13_seen_undo || inst->redo_count != p13_seen_redo;
    if (ts_pr13_slot_locked(inst, p13_slot)) {
        if (history_changed || p13_metadata_differs(inst, slot)) {
            if (ts_pr13_activate_slot(inst, p13_slot, error, sizeof(error)))
                snprintf(ui->status, sizeof(ui->status), "BANK %02d LOCKED - EDIT DISCARDED", p13_slot + 1);
        }
    } else if (history_changed) {
        ts_pr13_rerender(inst, p13_slot, error, sizeof(error));
    } else if (p13_metadata_differs(inst, slot)) {
        ts_pr13_sync_active_slot(inst, p13_slot, error, sizeof(error));
    }
    p13_seen_undo = inst->undo_count; p13_seen_redo = inst->redo_count;
}

static void p13_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *inst)
{
    p13_inst = (TsInstrument *)inst; p13_ui = (TsUiState *)ui;
    p13_reconcile(p13_inst, p13_ui); ts_ui_render(fb, ui, inst);
    p13_rect(fb, 240, 202, 400, 29, 0xff181818u);
    p13_button(fb,247,70,"NEW",0); p13_button(fb,322,92,"RESEED",0);
    p13_button(fb,540,90,(p13_slot>=0&&p13_slot<TS_BANK_SLOT_COUNT&&inst->bank[p13_slot].occupied&&ts_pr13_slot_locked(inst,p13_slot))?"UNLOCK":"LOCK",p13_slot>=0&&p13_slot<TS_BANK_SLOT_COUNT&&inst->bank[p13_slot].occupied&&ts_pr13_slot_locked(inst,p13_slot));
    if (!ui->show_keyboard && !ui->show_recipes) {
        for (int s = 0; s < TS_BANK_SLOT_COUNT; ++s) {
            int x = 10 + (s % 8) * 77, y = 330 + (s / 8) * 25;
            if (inst->bank[s].occupied)
                p13_rect(fb, x + 59, y + 4, 7, 10, ts_pr13_slot_locked(inst, s) ? 0xfff0d060u : 0xff404040u);
            if (s == p13_slot) {
                p13_rect(fb, x, y, 72, 2, 0xffffffffu);
                p13_rect(fb, x, y + 21, 72, 2, 0xffffffffu);
            }
        }
    }
}

static void p13_logical(SDL_Event *e, int *x, int *y)
{
    SDL_Window *w = SDL_GetWindowFromID(e->button.windowID); int ww = TS_UI_WIDTH, wh = TS_UI_HEIGHT;
    if (w) SDL_GetWindowSize(w, &ww, &wh); *x = e->button.x * TS_UI_WIDTH / ww; *y = e->button.y * TS_UI_HEIGHT / wh;
}

static void p13_redirect_click(SDL_Event *e, int x, int y)
{
    SDL_Window *w = SDL_GetWindowFromID(e->button.windowID); int ww = TS_UI_WIDTH, wh = TS_UI_HEIGHT;
    if (w) SDL_GetWindowSize(w, &ww, &wh);
    e->button.x = x * ww / TS_UI_WIDTH;
    e->button.y = y * wh / TS_UI_HEIGHT;
}

static void p13_touch(int x, int y)
{
    p13_min = p13_max = 0;
    if (y >= 233 && y < 257) {
        if (x >= 10 && x < 110) { p13_min = 10; p13_max = 109; }
        else if (x >= 120 && x < 220) { p13_min = 120; p13_max = 219; }
        else if (x >= 230 && x < 330) { p13_min = 230; p13_max = 329; }
    } else if (y >= 261 && y < 285 && p13_ui) {
        if (p13_ui->fx_page == TS_FX_FAMILY && x >= 115 && x < 225) { p13_min=115; p13_max=224; }
        else if (p13_ui->fx_page == TS_FX_NOISE && x >= 118 && x < 298) { p13_min=118; p13_max=297; }
        else if (p13_ui->fx_page == TS_FX_SHAPE) {
            if (x>=104&&x<198){p13_min=104;p13_max=197;} else if(x>=202&&x<282){p13_min=202;p13_max=281;} else if(x>=384&&x<476){p13_min=384;p13_max=475;} else if(x>=480&&x<572){p13_min=480;p13_max=571;}
        } else if (p13_ui->fx_page == TS_FX_DELAY) {
            if(x>=118&&x<210){p13_min=118;p13_max=209;} else if(x>=220&&x<312){p13_min=220;p13_max=311;} else if(x>=322&&x<414){p13_min=322;p13_max=413;} else if(x>=424&&x<516){p13_min=424;p13_max=515;}
        } else if (p13_ui->fx_page == TS_FX_SPACE) {
            if(x>=118&&x<238){p13_min=118;p13_max=237;} else if(x>=250&&x<370){p13_min=250;p13_max=369;} else if(x>=382&&x<502){p13_min=382;p13_max=501;}
        }
    }
    if (p13_max > p13_min) { p13_x = x; p13_y = y; }
}

static int p13_nudge(SDL_Event *e, int d)
{
    SDL_Window *w = SDL_GetWindowFromID(e->wheel.windowID); int ww = TS_UI_WIDTH, wh = TS_UI_HEIGHT;
    if (p13_max <= p13_min) return 0; if (w) SDL_GetWindowSize(w, &ww, &wh);
    p13_x += d; if (p13_x < p13_min) p13_x = p13_min; if (p13_x > p13_max) p13_x = p13_max;
    memset(e, 0, sizeof(*e)); e->type = SDL_MOUSEBUTTONDOWN; e->button.button = SDL_BUTTON_LEFT;
    e->button.windowID = w ? SDL_GetWindowID(w) : 0; e->button.x = p13_x * ww / TS_UI_WIDTH; e->button.y = p13_y * wh / TS_UI_HEIGHT; return 1;
}

static int p13_parent_action(int reseed)
{
    char error[160];
    if(!p13_inst) return 0;
    if(p13_slot<0||p13_slot>=TS_BANK_SLOT_COUNT){
        for(int s=0;s<TS_BANK_SLOT_COUNT;++s)if(!p13_inst->bank[s].occupied){p13_slot=s;break;}
    }
    if(p13_slot<0||p13_slot>=TS_BANK_SLOT_COUNT){
        if(p13_ui)snprintf(p13_ui->status,sizeof(p13_ui->status),"BANK FULL - CLEAR A TILE FIRST");
        return 0;
    }
    if(ts_pr13_generate_parent_in_slot(p13_inst,p13_slot,reseed,error,sizeof(error))){
        p13_seen_undo=p13_inst->undo_count;p13_seen_redo=p13_inst->redo_count;
        if(p13_ui)snprintf(p13_ui->status,sizeof(p13_ui->status),"%s PARENT IN BANK %02d",reseed?"RESEEDED":"NEW",p13_slot+1);
        return 1;
    }
    if(p13_ui)snprintf(p13_ui->status,sizeof(p13_ui->status),"%.150s",error);return 0;
}

static int p13_poll(SDL_Event *e)
{
    while (SDL_PollEvent(e)) {
        if (e->type == SDL_MOUSEWHEEL && p13_max > p13_min) return p13_nudge(e, e->wheel.y >= 0 ? 1 : -1);
        if (e->type == SDL_KEYDOWN && !e->key.repeat && p13_max > p13_min && (e->key.keysym.sym == SDLK_LEFT || e->key.keysym.sym == SDLK_RIGHT)) { e->wheel.windowID = e->key.windowID; return p13_nudge(e, e->key.keysym.sym == SDLK_RIGHT ? 1 : -1); }
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT && p13_ui) {
            int x,y,s; p13_logical(e,&x,&y); p13_touch(x,y);
            if(y>=205&&y<228){
                if(x>=247&&x<317){p13_parent_action(0);continue;}
                if(x>=322&&x<414){p13_parent_action(1);continue;}
                if(x>=540&&x<630&&p13_inst&&p13_slot>=0&&p13_slot<TS_BANK_SLOT_COUNT&&p13_inst->bank[p13_slot].occupied){char error[160];ts_pr13_toggle_slot_lock(p13_inst,p13_slot,error,sizeof(error));snprintf(p13_ui->status,sizeof(p13_ui->status),"BANK %02d %s",p13_slot+1,ts_pr13_slot_locked(p13_inst,p13_slot)?"LOCKED":"UNLOCKED");continue;}
                if(x>=247&&x<630)continue;
            }
            if (!p13_ui->show_keyboard && !p13_ui->show_recipes && p13_inst) {
                s=ts_ui_bank_slot_from_point(x,y);
                if(s>=0){
                    int sx=10+(s%8)*77, sy=330+(s/8)*25; char error[160];
                    if(p13_inst->bank[s].occupied&&x>=sx+57&&x<sx+68&&y>=sy+2&&y<sy+18){ts_pr13_toggle_slot_lock(p13_inst,s,error,sizeof(error));continue;}
                    p13_slot=s;
                    if(p13_inst->bank[s].occupied){
                        if(ts_pr13_activate_slot(p13_inst,s,error,sizeof(error))){
                            p13_inst->family_anchor_slot=s;
                            p13_seen_undo=p13_inst->undo_count;p13_seen_redo=p13_inst->redo_count;
                            /* The core bank audition points at slot source audio. PR13's
                               active Current is the rendered editable tile, so redirect
                               this click to the normal Play All handler instead. */
                            p13_redirect_click(e,20,300);
                            return 1;
                        }
                    }else{
                        snprintf(p13_ui->status,sizeof(p13_ui->status),"EMPTY BANK %02d - NEW CREATES PARENT",s+1);
                    }
                    continue;
                }
            }
        }
        return 1;
    }
    return 0;
}

static int p13_process(TsInstrument *i,const TsProcessRecipe *p,char *e,size_t n)
{
    return ts_pr13_set_process(i,p13_slot,p,e,n);
}
static int p13_process_t(TsInstrument *i,const TsProcessRecipe *p,const TsTuning *t,char *e,size_t n){return ts_pr13_set_process_and_tuning(i,p13_slot,p,t,e,n);}
static int p13_process_tt(TsInstrument *i,const TsProcessRecipe *p,const TsTuning *t,const TsTuning *a,char *e,size_t n){return ts_pr13_set_process_and_tunings(i,p13_slot,p,t,a,e,n);}
static int p13_family(TsInstrument *i,int a,int r,int *s,char *e,size_t n)
{
    int ok;(void)a;if(p13_slot>=0&&p13_slot<TS_BANK_SLOT_COUNT&&i->bank[p13_slot].occupied&&!ts_pr13_slot_locked(i,p13_slot))ts_pr13_rerender(i,p13_slot,e,n);
    ok=ts_pr13_generate_family_candidate(i,p13_slot,r,s,e,n);
    if(ok&&s&&*s>=0&&ts_pr13_activate_slot(i,*s,e,n)){p13_slot=*s;p13_seen_undo=i->undo_count;p13_seen_redo=i->redo_count;}
    return ok;
}
static int p13_generate_root(TsInstrument *i,TsGeneratorKind k,uint32_t seed,char *e,size_t n)
{
    int ok=ts_instrument_generate(i,k,seed,e,n);if(ok){ts_pr13_neutral_process(&i->process);i->bank[0].process=i->process;i->bank[0].has_process=1;p13_slot=0;p13_seen_undo=i->undo_count;p13_seen_redo=i->redo_count;}return ok;
}
static int p13_load_wav(TsInstrument *i,const char *path,char *e,size_t n)
{
    int ok=ts_instrument_load_wav(i,path,e,n);if(ok){ts_pr13_neutral_process(&i->process);i->bank[0].process=i->process;i->bank[0].has_process=1;p13_slot=0;p13_seen_undo=i->undo_count;p13_seen_redo=i->redo_count;}return ok;
}
static int p13_activate(TsInstrument *i,int s,char *e,size_t n)
{
    int ok=ts_pr13_activate_slot(i,s,e,n);if(ok){p13_slot=s;p13_seen_undo=i->undo_count;p13_seen_redo=i->redo_count;}return ok;
}
static int p13_bank_capture(TsInstrument *i,int s,TsBankCaptureKind k,char *e,size_t n)
{
    return ts_pr13_bank_capture(i,p13_slot,s,k,e,n);
}
static int p13_bank_clear(TsInstrument *i,int s,char *e,size_t n)
{
    int ok=ts_pr13_bank_clear(i,s,e,n);if(ok){p13_slot=s;p13_seen_undo=i->undo_count;p13_seen_redo=i->redo_count;}return ok;
}
static int p13_sample_edit(TsInstrument *i,TsSampleEditKind k,float a,char *e,size_t n)
{
    return ts_pr13_apply_sample_edit(i,p13_slot,k,a,e,n);
}
static int p13_crop(TsInstrument *i,char *e,size_t n)
{
    return ts_pr13_crop_selection(i,p13_slot,e,n);
}
static int p13_tape_drag(TsInstrument *i,TsPostEditKind k,size_t f,size_t l,int64_t d,char *e,size_t n)
{
    return ts_pr13_apply_tape_drag(i,p13_slot,k,f,l,d,e,n);
}

#define SDL_PollEvent p13_poll
#define ts_ui_render p13_render
#define ts_instrument_set_process p13_process
#define ts_instrument_set_process_and_tuning p13_process_t
#define ts_instrument_set_process_and_tunings p13_process_tt
#define ts_instrument_generate_family_candidate p13_family
#define ts_instrument_generate p13_generate_root
#define ts_instrument_load_wav p13_load_wav
#define ts_instrument_set_bank_as_current p13_activate
#define ts_instrument_bank_capture p13_bank_capture
#define ts_instrument_bank_clear p13_bank_clear
#define ts_instrument_apply_sample_edit p13_sample_edit
#define ts_instrument_crop_selection p13_crop
#define ts_instrument_apply_tape_drag p13_tape_drag
#define ts_instrument_save_recipe ts_pr13_save_project
#define ts_instrument_load_recipe ts_pr13_load_project
#include "main_sdl.c"