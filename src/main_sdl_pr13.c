#include "tapesister/pr13.h"
#include "tapesister/ui.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static TsInstrument *p13_inst;
static TsUiState *p13_ui;
static int p13_slot = -1;
static int p13_x, p13_min, p13_max, p13_y;

static void p13_rect(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            fb->pixels[yy * TS_UI_WIDTH + xx] = c;
}

static void p13_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *inst)
{
    ts_ui_render(fb, ui, inst);
    p13_inst = (TsInstrument *)inst;
    p13_ui = (TsUiState *)ui;
    p13_rect(fb, 247, 205, 383, 23, 0x00181818u);
    if (!ui->show_keyboard && !ui->show_recipes) {
        for (int s = 0; s < TS_BANK_SLOT_COUNT; ++s) if (inst->bank[s].occupied) {
            int x = 10 + (s % 8) * 77;
            int y = 330 + (s / 8) * 25;
            p13_rect(fb, x + 59, y + 4, 7, 10,
                     ts_pr13_slot_locked(inst, s) ? 0x00f0d060u : 0x00404040u);
            if (s == p13_slot) {
                p13_rect(fb, x, y, 72, 2, 0x00ffffffu);
                p13_rect(fb, x, y + 21, 72, 2, 0x00ffffffu);
            }
        }
    }
}

static void p13_logical(SDL_Event *e, int *x, int *y)
{
    SDL_Window *w = SDL_GetWindowFromID(e->button.windowID);
    int ww = TS_UI_WIDTH, wh = TS_UI_HEIGHT;
    if (w) SDL_GetWindowSize(w, &ww, &wh);
    *x = e->button.x * TS_UI_WIDTH / ww;
    *y = e->button.y * TS_UI_HEIGHT / wh;
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
            if (x>=104&&x<198){p13_min=104;p13_max=197;} else if(x>=202&&x<282){p13_min=202;p13_max=281;}
            else if(x>=384&&x<476){p13_min=384;p13_max=475;} else if(x>=480&&x<572){p13_min=480;p13_max=571;}
        } else if (p13_ui->fx_page == TS_FX_DELAY) {
            if(x>=118&&x<210){p13_min=118;p13_max=209;} else if(x>=220&&x<312){p13_min=220;p13_max=311;}
            else if(x>=322&&x<414){p13_min=322;p13_max=413;} else if(x>=424&&x<516){p13_min=424;p13_max=515;}
        } else if (p13_ui->fx_page == TS_FX_SPACE) {
            if(x>=118&&x<238){p13_min=118;p13_max=237;} else if(x>=250&&x<370){p13_min=250;p13_max=369;}
            else if(x>=382&&x<502){p13_min=382;p13_max=501;}
        }
    }
    if (p13_max > p13_min) { p13_x = x; p13_y = y; }
}

static int p13_nudge(SDL_Event *e, int d)
{
    SDL_Window *w = SDL_GetWindowFromID(e->wheel.windowID);
    int ww = TS_UI_WIDTH, wh = TS_UI_HEIGHT;
    if (p13_max <= p13_min) return 0;
    if (w) SDL_GetWindowSize(w, &ww, &wh);
    p13_x += d;
    if (p13_x < p13_min) p13_x = p13_min;
    if (p13_x > p13_max) p13_x = p13_max;
    memset(e, 0, sizeof(*e));
    e->type = SDL_MOUSEBUTTONDOWN;
    e->button.button = SDL_BUTTON_LEFT;
    e->button.windowID = w ? SDL_GetWindowID(w) : 0;
    e->button.x = p13_x * ww / TS_UI_WIDTH;
    e->button.y = p13_y * wh / TS_UI_HEIGHT;
    return 1;
}

static int p13_poll(SDL_Event *e)
{
    while (SDL_PollEvent(e)) {
        if (e->type == SDL_MOUSEWHEEL && p13_max > p13_min)
            return p13_nudge(e, e->wheel.y >= 0 ? 1 : -1);
        if (e->type == SDL_KEYDOWN && !e->key.repeat && p13_max > p13_min &&
            (e->key.keysym.sym == SDLK_LEFT || e->key.keysym.sym == SDLK_RIGHT)) {
            e->wheel.windowID = e->key.windowID;
            return p13_nudge(e, e->key.keysym.sym == SDLK_RIGHT ? 1 : -1);
        }
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT && p13_ui) {
            int x,y,s;
            p13_logical(e,&x,&y);
            p13_touch(x,y);
            if (y>=205&&y<228&&x>=247&&x<630) continue;
            if (!p13_ui->show_keyboard && !p13_ui->show_recipes && p13_inst) {
                s=ts_ui_bank_slot_from_point(x,y);
                if(s>=0&&p13_inst->bank[s].occupied){
                    int sx=10+(s%8)*77, sy=330+(s/8)*25;
                    char error[160];
                    if(x>=sx+57&&x<sx+68&&y>=sy+2&&y<sy+18){
                        ts_pr13_toggle_slot_lock(p13_inst,s,error,sizeof(error));
                        continue;
                    }
                    if(ts_instrument_set_bank_as_current(p13_inst,s,error,sizeof(error))){
                        p13_inst->process.body=0.0f; p13_inst->process.edge=0.0f; p13_inst->process.drift=0.5f;
                        p13_slot=s;
                    }
                }
            }
        }
        return 1;
    }
    return 0;
}

static int p13_process(TsInstrument *i,const TsProcessRecipe *p,char *e,size_t n){return ts_pr13_set_process(i,p13_slot,p,e,n);}
static int p13_process_t(TsInstrument *i,const TsProcessRecipe *p,const TsTuning *t,char *e,size_t n){return ts_pr13_set_process_and_tuning(i,p13_slot,p,t,e,n);}
static int p13_process_tt(TsInstrument *i,const TsProcessRecipe *p,const TsTuning *t,const TsTuning *a,char *e,size_t n){return ts_pr13_set_process_and_tunings(i,p13_slot,p,t,a,e,n);}
static int p13_family(TsInstrument *i,int a,int r,int *s,char *e,size_t n){(void)a;return ts_pr13_generate_family_candidate(i,p13_slot,r,s,e,n);}

#define SDL_PollEvent p13_poll
#define ts_ui_render p13_render
#define ts_instrument_set_process p13_process
#define ts_instrument_set_process_and_tuning p13_process_t
#define ts_instrument_set_process_and_tunings p13_process_tt
#define ts_instrument_generate_family_candidate p13_family
#define ts_instrument_save_recipe ts_pr13_save_project
#define ts_instrument_load_recipe ts_pr13_load_project
#include "main_sdl.c"
