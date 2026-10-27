/* Control-thread prompt. Disk inspection/copying happens without an audio
   lock. No choice opens another project or replaces unsaved session data. */
static int project_recovery_guard(TsUiState *ui,PendingFileOperation *pending)
{
    int saving=pending->mode==TS_BROWSER_SAVE_RECIPE;
    if(!saving && (pending->mode!=TS_BROWSER_LOAD_WAV || !path_is_tsr(pending->path)))return 0;
    TsProjectRecovery info;char error[1024],message[TS_PROJECT_RECOVERY_PATH_MAX*4+1600];
    if(!ts_sample_pages_recovery_info(pending->path,&info,error,sizeof(error)) || !info.attention)return 0;
    /* A copied stage can be inspected normally. Its retained marker requires
       Save As, rather than trying to overwrite that evidence in place. */
    if(!saving && !info.from_transaction && info.present==(1u<<TS_PROJECT_RECOVERY_CURRENT))return 0;
    const char *labels[]={"CURRENT LOCATION","BACKUP LOCATION (may hold the previous save)","STAGED LOCATION (possibly incomplete)"};
    size_t used=(size_t)snprintf(message,sizeof(message),
        "An interrupted save or leftover recovery entry needs attention.\n\n"
        "User-added files may be split between locations. No version is automatically chosen.\n"
        "Originals will not be deleted, replaced, or merged.\n\n");
    if(info.current_marker)used+=(size_t)snprintf(message+used,sizeof(message)-used,
        "The current location still has a transaction marker. Use Save current as to keep edits elsewhere.\n\n");
    for(int i=0;i<TS_PROJECT_RECOVERY_COUNT;++i)
        used+=(size_t)snprintf(message+used,sizeof(message)-used,"%s%s:\n%s\n\n",labels[i],
            !(info.present&(1u<<i))?" [not found]":info.folders&(1u<<i)?" [folder]":info.files&(1u<<i)?" [file]":" [link/special/unreadable]",info.directory[i]);
    snprintf(message+used,sizeof(message)-used,
        "COPY ALL: create a new workspace, then browse the copies. Your current session stays open.\n"
        "Copying takes time and space; links or unreadable entries stop it safely. Stop recordings and other writers first.\n\n"
        "%s"
        "Keep all originals until you have checked every project and collected extra files from ALL locations.",
        saving?"SAVE CURRENT AS: save the in-memory project elsewhere. Extra files stay in the old folders.\n\n":"");
    SDL_MessageBoxButtonData buttons[4]={
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT|SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,0,"Cancel - keep copies"},
        {0,2,"Copy all for recovery"}, {0,3,"Copy paths"}, {0,1,"Save current as..."}};
    SDL_MessageBoxData dialog={SDL_MESSAGEBOX_WARNING,SDL_GetKeyboardFocus(),"TapeSister project recovery",message,
        saving?4:3,buttons,NULL};
    int choice=0,unavailable=0;
    do {
        if(SDL_ShowMessageBox(&dialog,&choice)<0){choice=0;unavailable=1;SDL_SetClipboardText(message);break;}
        if(choice==3)SDL_SetClipboardText(message);
    } while(choice==3);
    pending->active=pending->presented=0;ui->file_busy=0;ui->file_busy_label[0]=0;
    ui->browser.overwrite_armed=0;
    if(saving){ui->exit_after_save=ui->exit_for_new=0;}
    if(choice==1 && saving) {
        browser_open(ui,TS_BROWSER_SAVE_RECIPE);
        char parent[TS_BROWSER_PATH_MAX];
        if(!parent_directory_of(info.directory[0],parent,sizeof(parent)))snprintf(parent,sizeof(parent),".");
        snprintf(ui->browser.directory,sizeof(ui->browser.directory),"%s",parent);
        ts_browser_refresh(&ui->browser);
        char name[TS_BROWSER_NAME_MAX+1];
        const char *original=path_basename(info.project);
        size_t length=strlen(original);if(length>4)length-=4;
        snprintf(name,sizeof(name),"%.*s-recovered.tsr",(int)SDL_min(length,sizeof(name)-16),original);
        ts_browser_set_filename(&ui->browser,name);
        snprintf(ui->status,sizeof(ui->status),"SAVE CURRENT PROJECT ELSEWHERE - RECOVERY ORIGINALS KEPT");
    } else if(choice==2) {
        char workspace[TS_BROWSER_PATH_MAX];
        if(ts_sample_pages_copy_recovery(info.project,workspace,sizeof(workspace),error,sizeof(error))) {
            browser_open(ui,TS_BROWSER_LOAD_WAV);
            snprintf(ui->browser.directory,sizeof(ui->browser.directory),"%s",workspace);
            ts_browser_refresh(&ui->browser);
            int copied=SDL_SetClipboardText(workspace)==0;
            snprintf(ui->browser.message,sizeof(ui->browser.message),"OPEN A TSR TO INSPECT; USE FILE MANAGER FOR EXTRA FILES");
            snprintf(ui->status,sizeof(ui->status),copied?"RECOVERY COPIES CREATED - CURRENT SESSION KEPT; PATH COPIED":
                "RECOVERY COPIES CREATED - CURRENT SESSION KEPT; PATH SHOWN IN BROWSER");
        } else {
            char failure[TS_BROWSER_PATH_MAX+1280];
            snprintf(failure,sizeof(failure),"%s\n\nOriginals are unchanged. Any incomplete workspace is kept here:\n%s",error,workspace[0]?workspace:"[no workspace created]");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Recovery copy incomplete",failure,SDL_GetKeyboardFocus());
            snprintf(ui->status,sizeof(ui->status),"RECOVERY COPY FAILED - ORIGINALS KEPT: %.105s",error);
        }
    } else snprintf(ui->status,sizeof(ui->status),unavailable?
        "RECOVERY DIALOG UNAVAILABLE - COPIES KEPT; RECOVERY DETAILS COPIED TO CLIPBOARD":
        "RECOVERY CANCELLED - CURRENT SESSION AND ALL COPIES KEPT");
    return 1;
}
