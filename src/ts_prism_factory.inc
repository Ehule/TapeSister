/* Related voicings share interval families and conservative levels. The first
   seven retain the original mode order and default sound. No DSP mode IDs change. */
typedef struct {
    const char *name;
    int mode, lenses, octave;
    float spread, drift, focus, stereo, body, rate, mix, output;
} PrismFactory;
#define ORIGINAL(name,mode) {name,mode,12,0,.5f,.15f,0,.8f,.5f,.1f,.8f,0}
static const PrismFactory prism_factory[TS_PRISM_FACTORY_PRESETS] = {
    ORIGINAL("SUPERSAW",TS_PRISM_SUPERSAW),
    ORIGINAL("ENSEMBLE",TS_PRISM_ENSEMBLE),
    ORIGINAL("HARMONIC",TS_PRISM_HARMONIC),
    ORIGINAL("FIFTHS",TS_PRISM_FIFTHS),
    ORIGINAL("OCTAVES",TS_PRISM_OCTAVES),
    ORIGINAL("CLUSTER",TS_PRISM_CLUSTER),
    ORIGINAL("31-TET",TS_PRISM_MICRO),
    {"SUPERSAW LOW",TS_PRISM_SUPERSAW,12,-1,.3f,.12f,.2f,.55f,1.1f,.04f,.85f,-1},
    {"SUPERSAW HIGH",TS_PRISM_SUPERSAW,16,1,.4f,.18f,.1f,.8f,.35f,.08f,.75f,-2},
    {"SUPERSAW WIDE",TS_PRISM_SUPERSAW,24,0,.85f,.3f,.05f,1,.6f,.06f,.85f,-2},
    {"ENSEMBLE LOW",TS_PRISM_ENSEMBLE,8,-1,.25f,.2f,.3f,.5f,1,.035f,.85f,-1},
    {"ENSEMBLE HIGH",TS_PRISM_ENSEMBLE,12,1,.4f,.25f,.2f,.85f,.3f,.07f,.75f,-2},
    {"ENSEMBLE WIDE",TS_PRISM_ENSEMBLE,20,0,.65f,.4f,.1f,1,.6f,.05f,.85f,-2},
    {"HARMONIC LOW",TS_PRISM_HARMONIC,6,-1,.25f,.08f,.3f,.5f,1.2f,.025f,.85f,-1},
    {"HARMONIC HIGH",TS_PRISM_HARMONIC,8,1,.3f,.12f,.2f,.8f,.4f,.04f,.75f,-2},
    {"HARMONIC OPEN",TS_PRISM_HARMONIC,16,0,.5f,.2f,.1f,.95f,.8f,.03f,.85f,-2},
    {"FIFTHS LOW",TS_PRISM_FIFTHS,4,-1,.2f,.05f,.4f,.45f,1.3f,.02f,.85f,-1},
    {"FIFTHS HIGH",TS_PRISM_FIFTHS,6,1,.3f,.1f,.25f,.8f,.4f,.04f,.75f,-2},
    {"FIFTHS OPEN",TS_PRISM_FIFTHS,10,0,.4f,.15f,.15f,.95f,.9f,.025f,.85f,-2},
    {"OCTAVES LOW",TS_PRISM_OCTAVES,4,-1,.15f,.04f,.5f,.35f,1.4f,.02f,.85f,-1},
    {"OCTAVES HIGH",TS_PRISM_OCTAVES,4,1,.2f,.08f,.4f,.7f,.5f,.035f,.75f,-2},
    {"OCTAVES OPEN",TS_PRISM_OCTAVES,8,0,.35f,.12f,.2f,.9f,1,.025f,.85f,-2},
    {"CLUSTER LOW",TS_PRISM_CLUSTER,6,-1,.16f,.12f,.4f,.5f,1,.035f,.8f,-1},
    {"CLUSTER HIGH",TS_PRISM_CLUSTER,8,1,.2f,.18f,.3f,.85f,.4f,.06f,.7f,-2},
    {"CLUSTER MIST",TS_PRISM_CLUSTER,16,0,.35f,.4f,.2f,1,.7f,.04f,.8f,-2},
    {"31-TET LOW",TS_PRISM_MICRO,6,-1,.2f,.06f,.3f,.5f,1.1f,.025f,.8f,-1},
    {"31-TET HIGH",TS_PRISM_MICRO,8,1,.3f,.1f,.2f,.8f,.4f,.045f,.7f,-2},
    {"31-TET HALO",TS_PRISM_MICRO,16,0,.4f,.2f,.1f,1,.8f,.03f,.8f,-2}
};
#undef ORIGINAL

const char *ts_prism_factory_name(int preset)
{
    return preset>=0 && preset<TS_PRISM_FACTORY_PRESETS ? prism_factory[preset].name : "CUSTOM";
}
static TsPrismPatch prism_factory_patch(int preset)
{
    TsPrismPatch c={.dry_level=1,.color=.5f};
    const PrismFactory *f=&prism_factory[preset];
    c.mode=f->mode;c.lenses=f->lenses;c.group_octave=f->octave;
    c.spread=f->spread;c.drift=f->drift;c.focus=f->focus;c.stereo=f->stereo;
    c.body=f->body;c.drift_rate=f->rate;c.mix=f->mix;c.output_db=f->output;
    return c;
}
int ts_prism_factory_apply(TsPrismControls *c,int preset)
{
    if(!c || preset<0 || preset>=TS_PRISM_FACTORY_PRESETS)return 0;
    TsPrismPatch patch=prism_factory_patch(preset);patch.enabled=c->enabled;
    memcpy(c,&patch,sizeof(patch));return 1;
}
int ts_prism_factory_find(const TsPrismControls *c)
{
    if(!c)return -1;
    for(int i=0;i<TS_PRISM_FACTORY_PRESETS;++i) {
        TsPrismPatch patch=prism_factory_patch(i);patch.enabled=c->enabled;
        if(!memcmp(c,&patch,sizeof(patch)))return i;
    }
    return -1;
}
