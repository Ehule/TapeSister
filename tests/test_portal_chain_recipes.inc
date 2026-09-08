static void test_chain_recipes(void)
{
    static TsPortalLibrary library,loaded,before;
    char error[256];const char *path="portal-chain-roundtrip.recipes";
    TsPortalRecipe *chain=&library.recipes[0],scalar;
    snprintf(chain->process_id,sizeof(chain->process_id),"@chain");snprintf(chain->name,sizeof(chain->name),"MY CHAIN");
    chain->version=1;chain->stage_count=3;
    const char *ids[]={"extend.scramble.1","blur.blur","modify.loudness.1"};
    for(unsigned i=0;i<3;++i) {
        ts_portal_recipe_default(&scalar,ts_portal_process_find(ids[i]));
        ts_portal_step_set(&chain->stages[i],&scalar);
    }
    chain->stages[1].bypass=1;chain->stages[2].values[0]=.25;
    library.pins[31]=*chain;ts_portal_recipe_exact(&library.pins[31]);
    ts_portal_recipe_default(&library.recipes[5],ts_portal_process_at(0));
    assert(ts_portal_recipe_validate(chain,error,sizeof(error)));
    assert(ts_portal_library_save(&library,path,error,sizeof(error)));
    assert(ts_portal_library_load(&loaded,path,error,sizeof(error)));
    assert(!memcmp(&library,&loaded,sizeof(library)));
    for(unsigned i=0;i<3;++i)assert(!loaded.pins[31].stages[i].exposed);
    before=loaded;
    const char *bad[]={"TSCDPPORTAL 2\nC R 0 9 |TOO MANY\n", "TSCDPPORTAL 2\nC R 0 1 |TRUNCATED\n",
        "TSCDPPORTAL 2\nC R 0 0 |EMPTY\n", "TSCDPPORTAL 2\nC R 33 1 |BAD SLOT\n", "TSCDPPORTAL 999\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        FILE *f=fopen(path,"wb");assert(f);fputs(bad[i],f);fclose(f);
        assert(!ts_portal_library_load(&loaded,path,error,sizeof(error)));
        assert(!memcmp(&loaded,&before,sizeof(loaded)));
    }
    scalar=*chain;scalar.stage_count=9;assert(!ts_portal_recipe_validate(&scalar,error,sizeof(error)));
    scalar=*chain;memset(scalar.stages[1].process_id,'x',sizeof(scalar.stages[1].process_id));assert(!ts_portal_recipe_validate(&scalar,error,sizeof(error)));
    scalar=*chain;snprintf(scalar.stages[1].process_id,64,"@chain");assert(!ts_portal_recipe_validate(&scalar,error,sizeof(error)));
    scalar=*chain;scalar.stages[0].values[0]=NAN;assert(!ts_portal_recipe_validate(&scalar,error,sizeof(error)));
    scalar=*chain;scalar.stages[0].bypass=2;assert(!ts_portal_recipe_validate(&scalar,error,sizeof(error)));
    static TsPortalUi ui;ts_portal_ui_init(&ui);ui.library=library;ui.tab=1;
    snprintf(ui.query,sizeof(ui.query),"spectral");ui.family=TS_PORTAL_SPECTRAL+1;
    assert(ts_portal_filter_slot(&ui,0,&scalar)==0 && scalar.stage_count==3);
    /* Old-format files remain old-format when they contain only scalar recipes. */
    memset(&library,0,sizeof(library));ts_portal_recipe_default(&library.recipes[3],ts_portal_process_at(0));
    assert(ts_portal_library_save(&library,path,error,sizeof(error)));
    FILE *f=fopen(path,"rb");char header[64];assert(f && fgets(header,sizeof(header),f));fclose(f);
    assert(!strcmp(header,"TSCDPPORTAL 1\n"));memset(&loaded,0,sizeof(loaded));
    assert(ts_portal_library_load(&loaded,path,error,sizeof(error)) && !memcmp(&loaded,&library,sizeof(library)));
    remove(path);
}
