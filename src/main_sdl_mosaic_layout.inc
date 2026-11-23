#include "tapesister/mosaic_layout.h"
static const TsUiState *main_layout_ui;
static SDL_Renderer *main_layout_renderer;
static SDL_Window *main_layout_window;
static int mosaic_layout_active(void)
{
    const TsUiState *ui=main_layout_ui;
    return ui && ui->mosaic_open && !ui->tracker_open && !ui_dialog_open(ui) &&
        !ui->master_eq_open && !ui->router_open && !ui->mosaic_live_open &&
        !ui->mosaic_marker_open && !ui->mosaic_views_open && !ui->mosaic_grid_open &&
        !ui->mosaic_array_open && !ui->mosaic_video_open && !ui->mosaic_context_open &&
        !ui->mosaic_loop_open && !ui->mosaic_align_open && !ui->mosaic_scatter_open &&
        !ui->mosaic_spacing_confirm && (!ui->mosaic_gallery_open || ui->mosaic_gallery_detached) &&
        (ts_mosaic_layout_percent(ui->config.mosaic_toolbar_percent)!=100 ||
         ts_mosaic_layout_percent(ui->config.mosaic_sources_percent)!=100);
}
static TsMosaicLayout mosaic_layout_current(void)
{return ts_mosaic_layout_make(main_layout_ui->config.mosaic_toolbar_percent,main_layout_ui->config.mosaic_sources_percent);}
static void mosaic_layout_detail_rect(SDL_Renderer *renderer,SDL_Rect *rect)
{
    if(renderer!=main_layout_renderer || !mosaic_layout_active())return;
    int width,height;SDL_GetRendererOutputSize(renderer,&width,&height);if(width<=0 || height<=0)return;
    TsMosaicLayout layout=mosaic_layout_current();
    int cx=(rect->x+rect->w/2)*640/width,cy=(rect->y+rect->h/2)*400/height;
    for(int i=0;i<TS_MOSAIC_LAYOUT_PARTS;++i) {
        TsUiRect s=layout.source[i],d=layout.destination[i];if(!ts_ui_rect_contains(s,cx,cy))continue;
        double x=(double)rect->x*640/width,y=(double)rect->y*400/height;
        int left=(int)lround((d.x+(x-s.x)*d.w/s.w)*width/640);
        int top=(int)lround((d.y+(y-s.y)*d.h/s.h)*height/400);
        int right=(int)lround((d.x+(x+(double)rect->w*640/width-s.x)*d.w/s.w)*width/640);
        int bottom=(int)lround((d.y+(y+(double)rect->h*400/height-s.y)*d.h/s.h)*height/400);
        *rect=(SDL_Rect){left,top,right-left,bottom-top};return;
    }
}
static void mosaic_layout_adjust(TsUiState *ui,int action)
{
    int toolbar=ts_mosaic_layout_percent(ui->config.mosaic_toolbar_percent);
    int sources=ts_mosaic_layout_percent(ui->config.mosaic_sources_percent);
    if(action==1)toolbar=SDL_max(50,toolbar-10);
    if(action==2)toolbar=SDL_min(100,toolbar+10);
    if(action==3)sources=SDL_max(50,sources-10);
    if(action==4)sources=SDL_min(100,sources+10);
    if(action==5)toolbar=sources=100;
    ui->config.mosaic_toolbar_percent=toolbar;ui->config.mosaic_sources_percent=sources;
}
static void begin_mosaic_layout(TsUiState *ui)
{
    const SDL_MessageBoxButtonData buttons[]={
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Close"},{0,5,"Reset sizes"},
        {0,4,"Sources +"},{0,3,"Sources -"},{0,2,"Toolbar +"},{0,1,"Toolbar -"}};
    char message[400];snprintf(message,sizeof(message),
        "Mosaic toolbar: %d%%    Source bank: %d%%\n\nResize each independently (50-100%%). Canvas zoom keeps its own controls.\nRight-click CFG or press Ctrl+Alt+U to return. Dialogs use standard sizing.",
        ts_mosaic_layout_percent(ui->config.mosaic_toolbar_percent),ts_mosaic_layout_percent(ui->config.mosaic_sources_percent));
    SDL_MessageBoxData dialog={SDL_MESSAGEBOX_INFORMATION,SDL_GetKeyboardFocus(),"TapeSister - Mosaic View Sizes",message,6,buttons,NULL};
    int action=0;if(SDL_ShowMessageBox(&dialog,&action) || !action)return;
    mosaic_layout_adjust(ui,action);char error[160];
    if(!live_test_save_blocked() && !ts_config_save(&ui->config,config_file_path(),error,sizeof(error)))
        snprintf(ui->status,sizeof(ui->status),"VIEW SIZES CHANGED; CONFIG SAVE FAILED: %.110s",error);
    else snprintf(ui->status,sizeof(ui->status),"TOOLBAR %d%% / SOURCES %d%% / CANVAS ZOOM UNCHANGED",ui->config.mosaic_toolbar_percent,ui->config.mosaic_sources_percent);
}
