/* Pre-change algorithm: compare staged placement against the established
   geometry, including copies snapping/spacing against their originals. */
static void reference_mosaic_move_group(TsUiState *ui,double dx,double dt,int snap)
{
    TsMosaic *m=ui->mosaic;double minx=1e30,mint=1e30,maxt=0;
    for(int i=0;i<ui->mosaic_ghost_count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];
        minx=fmin(minx,o->x);mint=fmin(mint,o->start);maxt=fmax(maxt,o->start);
    }
    dx=fmax(dx,-minx);dt=fmin(86400-maxt,fmax(dt,-mint));
    if(snap) {
        double best=7/ui->mosaic_scale,adjust=0;
        for(int j=0;j<ui->mosaic_ghost_count;++j)for(int k=0;k<3;++k) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[j];double t=o->start+dt+o->duration*.5*k;
            if(fabs(t)<best){best=fabs(t);adjust=-t;}
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                const TsMosaicEvent *e=&m->events[i];if(!e->id || mosaic_chosen(ui,i))continue;
                for(int a=0;a<3;++a) {
                    double d=e->start+e->duration*.5*a-t;
                    if(fabs(d)<best){best=fabs(d);adjust=d;}
                }
            }
        }
        dt=fmin(86400-maxt,fmax(-mint,dt+adjust));
    }
    for(int pass=0;ui->mosaic->auto_space && pass<TS_MOSAIC_EVENTS;++pass) {
        double shift=0;
        for(int j=0;j<ui->mosaic_ghost_count;++j) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[j];double t=o->start+dt,x=o->x+dx;
            for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
                const TsMosaicEvent *b=&m->events[i];if(!b->id || mosaic_chosen(ui,i))continue;
                if(t>=b->start+b->duration-1e-9 || b->start>=t+o->duration-1e-9)continue;
                if(x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<x+o->width+TS_MOSAIC_GUTTER)
                    shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-x);
            }
        }
        if(shift<=0)break;dx+=shift;
    }
    for(int j=0;j<ui->mosaic_ghost_count;++j) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[j];TsMosaicEvent *e=ts_mosaic_find(m,o->id);
        if(e){e->start=o->start+dt;e->x=o->x+dx;}
    }
}

static void test_mosaic_group_staging(void)
{
    TsMosaic *saved=ui.mosaic;
    static TsUiState reference;
    static MosaicController copy_control;
    for(int scenario=0;scenario<24;++scenario) {
        TsMosaic *m=ts_mosaic_create(),*legacy=ts_mosaic_create();assert(m && legacy);
        ui.mosaic=m;mosaic_select_only(&ui,0);
        char error[160];TsMosaicSource *source=ts_mosaic_source(m,&instrument.current,error,sizeof(error));assert(source);
        int count=scenario>=20?512:32,copying=scenario%2;
        for(int i=0;i<count;++i) {
            TsMosaicEvent *e=ts_mosaic_add(m,source,(i*17%53)*.25,(i*31%67)*8);assert(e);
            e->duration=1+(i*13%29)*.1;e->width=36+i%5*13;
            if(scenario>=20 || i%3)mosaic_select_add(&ui,e->id);
        }
        m->auto_space=(scenario/2)%2;ui.mosaic_scale=24;
        ui.mosaic_ghost_count=mosaic_selected_events(&ui,ui.mosaic_ghosts);
        *legacy=*m;legacy->sources=NULL;reference=ui;reference.mosaic=legacy;
        double dx=scenario%3==0?-90:13.25,dt=scenario%3==0?-3:.173;
        double planned_x=dx,planned_t=dt;int snap=(scenario/4)%2;
        uint64_t before=ts_mosaic_hash(m);int history=m->history_count;
        mosaic_plan_group(&ui,&planned_x,&planned_t,snap,copying);
        assert(ts_mosaic_hash(m)==before && m->history_count==history);
        assert(mosaic_event_count(m)==count); /* No provisional copies visible. */
        /* Audio may advance after planning without changing its geometry. */
        m->playing=1;ts_mosaic_begin_block(m,32);
        for(int i=0;i<32;++i)ts_mosaic_read(m,48000);
        assert(ts_mosaic_hash(m)==before);
        memset(&copy_control,0,sizeof(copy_control));copy_control.drag_copy=copying;
        assert(mosaic_begin_drag_change(&reference,&copy_control));
        reference_mosaic_move_group(&reference,dx,dt,snap);
        memset(&copy_control,0,sizeof(copy_control));copy_control.drag_copy=copying;
        assert(mosaic_begin_drag_change(&ui,&copy_control));
        mosaic_move_group(&ui,planned_x,planned_t);
        assert(ui.mosaic_selected==reference.mosaic_selected);
        for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            assert(m->events[i].id==legacy->events[i].id);
            assert(m->events[i].x==legacy->events[i].x && m->events[i].start==legacy->events[i].start);
        }
        assert(ts_mosaic_undo(m,0) && ts_mosaic_hash(m)==before);
        assert(ts_mosaic_undo(m,1) && ts_mosaic_hash(m)==ts_mosaic_hash(legacy));
        ts_mosaic_free(legacy);ts_mosaic_free(m);
    }
    ui.mosaic=saved;ui.mosaic_ghost_count=0;mosaic_select_only(&ui,0);
}
