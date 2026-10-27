static void recovery_controls(void)
{
    populate();char requested[96],obstruction[TS_BROWSER_PATH_MAX],original[TS_BROWSER_PATH_MAX];
    snprintf(requested,sizeof(requested),"recovery-ui-%llu.tsr",(unsigned long long)SDL_GetPerformanceCounter());
    run_save(requested);snprintf(original,sizeof(original),"%s",ui.project_path);
    char *slash=strrchr(original,'/');assert(slash);
    snprintf(obstruction,sizeof(obstruction),"%.*s.tapesister-backup",(int)(slash-original),original);
    FILE *marker=fopen(obstruction,"wb");assert(marker);fputs("user recovery obstruction\n",marker);assert(!fclose(marker));
    ui.mosaic->gain=.37f;uint64_t before=project_hash(),saved=ui.saved_state_hash;
    for(int choice=0;choice<=3;++choice) {
        ts_browser_close(&ui.browser);ui.file_busy=0;queue_active_project_save(&ui,&file);assert(file.active);
        ui.exit_after_save=1;ui.exit_for_new=1;
        recovery_choices[0]=choice;recovery_choices[1]=0;recovery_choice_count=choice==3?2:1;recovery_choice_at=0;
        run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
        assert(!file.active && !ui.file_busy && !ui.exit_after_save && !ui.exit_for_new && recovery_save_button);
        assert(project_hash()==before && ui.saved_state_hash==saved && !strcmp(ui.project_path,original));
        marker=fopen(obstruction,"rb");assert(marker);fclose(marker);
        if(choice==1)assert(ui.browser.mode==TS_BROWSER_SAVE_RECIPE && strstr(ui.browser.filename,"-recovered.tsr") && !strstr(ui.browser.directory,"tapesister-backup"));
        if(choice==2) {
            assert(!recovery_copy_errors);
            assert(ui.browser.mode==TS_BROWSER_LOAD_WAV && strstr(ui.browser.directory,".tapesister-recovery-"));
            char copy[TS_PROJECT_RECOVERY_PATH_MAX];snprintf(copy,sizeof(copy),"%s/backup",ui.browser.directory);
            marker=fopen(copy,"rb");assert(marker);fclose(marker);
            snprintf(copy,sizeof(copy),"%s/current/%s",ui.browser.directory,path_basename(original));
            TsSampleProjectLoad *prepared=ts_sample_pages_prepare_project(copy,1,error,sizeof(error));assert(prepared);ts_sample_pages_dispose_project(prepared);
        }
        if(choice==3){char *text=SDL_GetClipboardText();assert(text && strstr(text,obstruction));SDL_free(text);}
    }
    ui.file_busy=0;queue_active_project_save(&ui,&file);assert(file.active);
    recovery_choices[0]=-99;recovery_choice_count=1;recovery_choice_at=0;
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(!file.active && !ui.file_busy && strstr(ui.status,"DIALOG UNAVAILABLE") && project_hash()==before);
    /* Opening a transaction path also offers copying without replacing the
       live session. Save Current As belongs only to an attempted save. */
    file=(PendingFileOperation){.active=1,.mode=TS_BROWSER_LOAD_WAV};
    snprintf(file.path,sizeof(file.path),"%.*s.tapesister-backup/%s",(int)(slash-original),original,path_basename(original));
    recovery_choices[0]=0;recovery_choice_count=1;recovery_choice_at=0;
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(!file.active && !recovery_save_button && project_hash()==before && !strcmp(ui.project_path,original));
    /* A damaged manifest must not turn Copy All into a project-load attempt.
       Preserve the raw folder even if its advertised project name is invalid. */
    assert(!remove(obstruction));
    char original_directory[TS_BROWSER_PATH_MAX];
    snprintf(original_directory,sizeof(original_directory),"%.*s",(int)(slash-original),original);
    assert(!rename(original_directory,obstruction));
    char manifest[TS_PROJECT_RECOVERY_PATH_MAX];snprintf(manifest,sizeof(manifest),"%s/manifest.txt",obstruction);
    const char broken_manifest[]="TAPESISTER_PROJECT 2\nproject=broken\n";
    marker=fopen(manifest,"wb");assert(marker);fputs(broken_manifest,marker);assert(!fclose(marker));
    file=(PendingFileOperation){.active=1,.mode=TS_BROWSER_LOAD_WAV};
    snprintf(file.path,sizeof(file.path),"%s/%s",obstruction,path_basename(original));
    recovery_choices[0]=2;recovery_choice_count=1;recovery_choice_at=0;
    run_pending_file_operation(0,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file);
    assert(!recovery_copy_errors && !file.active && !ui.file_busy && project_hash()==before);
    assert(strstr(ui.browser.directory,".tapesister-recovery-"));
    marker=fopen(manifest,"rb");assert(marker);char header[96];size_t bytes=fread(header,1,sizeof(header),marker);fclose(marker);
    assert(bytes==sizeof(broken_manifest)-1 && !memcmp(header,broken_manifest,bytes));
    char invalid[TS_PROJECT_RECOVERY_PATH_MAX];snprintf(invalid,sizeof(invalid),"%s/backup/%s",ui.browser.directory,path_basename(original));
    assert(!load_instrument(0,&audio,&ui,&bank,&pages,&record,0,invalid) && project_hash()==before);
    recovery_choice_count=recovery_choice_at=0;
    assert(project_new(NULL,0,0,&session));assert_empty();
}
