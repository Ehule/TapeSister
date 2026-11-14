/* E always returns to the routing canvas; text fields and KEYS retain letters. */
static int fx_navigation_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,SisterWindow *s,TsSamplePages *pages,TsInstrument *bank)
{
    if((e->type!=SDL_KEYDOWN && e->type!=SDL_KEYUP) || e->key.keysym.sym!=SDLK_e ||
       (e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT)) || ui->mosaic_source_keys ||
       (ui->mosaic_live_open && ui->mosaic_live_keys) || ts_ui_foreground_panel_open(ui) || ui->midi_learn_active ||
       s->model.preset_manage_open || fx_canvas.label_editing || fx_canvas.properties ||
       matrix_window.rename>=0 || (meta_fx_window.visible && meta_fx_window.edit) || sunvox_editor.focus || sunvox_files.mode)return 0;
    Uint32 id=event_window_id(e);
    int from_mosaic=id==SDL_GetWindowID(main) && ui->mosaic_open;
    int editor=id==s->window_id || (meta_fx_window.visible && id==meta_fx_window.id) ||
        (character_filter_window.visible && id==character_filter_window.id);
    if(!from_mosaic && !editor && id!=matrix_window.id)return 0;
    if(e->type==SDL_KEYUP || e->key.repeat)return 1;
    sister_board_history_finish(device,audio,pages,bank);
    meta_fx_history_finish(device,audio,pages,bank);
    character_filter_history_finish(device,audio,ui,pages,bank);
    if(!matrix_window.visible && !matrix_show(device,audio,ui,pages,bank,-1,-1))return 1;
    fx_view(1,pages,bank);
    if(from_mosaic)fx_selection_from_mosaic(ui,pages,bank);
    meta_fx_hide();character_filter_hide();
    application_window_focus(matrix_window.window,s);
    return 1;
}
