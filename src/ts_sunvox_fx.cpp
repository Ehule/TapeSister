#include "tapesister/sunvox_fx.h"
#include "sundog.h"
#define SVH_INLINES
#include "sunvox_engine_helper.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

const char *g_app_config[] = { nullptr };
const char *g_app_log = nullptr;
const char *g_app_name = "TapeSister";
const char *g_app_name_short = "TapeSister";
int app_global_init() { return sunvox_global_init(); }
int app_global_deinit() { return sunvox_global_deinit(); }

struct TsSunVoxFactory {
    const char *name, *category;
    const unsigned char *data;
    size_t size;
};
#include "sunvox_catalog.inc"

namespace {
constexpr size_t max_patch=8*1024*1024, max_imports=128*1024*1024;
struct Resource {
    uint64_t key=0;
    std::string name, category;
    const unsigned char *data=nullptr;
    size_t size=0, received=0;
    std::vector<unsigned char> owned;
    std::vector<TsSunVoxParameter> params;
    bool complete=false, described=false;
    int factory=-1;
};
std::mutex resources_mutex;
std::once_flag globals_once;
std::vector<std::unique_ptr<Resource>> resources;
size_t imported_bytes=0;

uint64_t hash_bytes(const unsigned char *data,size_t size) {
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<size;++i) h=(h^data[i])*UINT64_C(1099511628211);
    return h;
}
void globals() {
    std::call_once(globals_once,[] {
        sundog_global_init();
        slog_disable(1,1);
        for(size_t i=0;i<sizeof(factories)/sizeof(factories[0]);++i) {
            const auto &f=factories[i];
            std::unique_ptr<Resource> r(new Resource);
            r->key=hash_bytes(f.data,f.size);r->name=f.name;r->category=f.category;
            r->data=f.data;r->size=f.size;r->complete=true;r->factory=(int)i;
            resources.push_back(std::move(r));
        }
    });
}
Resource *find_resource(uint64_t key) {
    for(auto &r:resources) if(r->key==key) return r.get();
    return nullptr;
}
bool patch_shape(const unsigned char *data,size_t size) {
    if(size<16 || size>max_patch || memcmp(data,"SSYN\0\0\0\0",8))return false;
    size_t at=8;bool type=false,finish=false;int chunks=0,chunk=-1;
    while(at+8<=size) {
        uint32_t len=(uint32_t)data[at+4]|((uint32_t)data[at+5]<<8)|
            ((uint32_t)data[at+6]<<16)|((uint32_t)data[at+7]<<24);
        if(len>size-at-8)return false;
        const unsigned char *v=data+at+8;
        const char *scalar[]={"VERS","SFFF","SFIN","SREL","SSCL","SVPR","SMII","SMIC","SMIB","SMIP","CVAL","CHNK","CHNM","CHFF","CHFR"};
        for(const char *tag:scalar)if(!memcmp(data+at,tag,4) && len!=4)return false;
        if(!memcmp(data+at,"SCOL",4) && len!=3)return false;
        if(!memcmp(data+at,"SMIN",4) && (!len || v[len-1]))return false;
        if(!memcmp(data+at,"STYP",4))type=len>1 && v[len-1]==0;
        if(!memcmp(data+at,"CHNK",4)) {
            uint32_t n=(uint32_t)v[0]|((uint32_t)v[1]<<8)|((uint32_t)v[2]<<16)|((uint32_t)v[3]<<24);
            if(n>4096 || chunks)return false;chunks=(int)n;
        }
        if(!memcmp(data+at,"CHNM",4)) {
            uint32_t n=(uint32_t)v[0]|((uint32_t)v[1]<<8)|((uint32_t)v[2]<<16)|((uint32_t)v[3]<<24);
            if(n>=(uint32_t)chunks)return false;chunk=(int)n;
        }
        if((!memcmp(data+at,"CHDT",4) || !memcmp(data+at,"CHFF",4) || !memcmp(data+at,"CHFR",4)) && chunk<0)return false;
        if(!memcmp(data+at,"SEND",4)){if(len || at+8!=size)return false;finish=true;}
        at+=8+len;
    }
    return at==size && type && finish;
}
void select_resource(TsSunVoxControls *c,const Resource *r) {
    memset(c,0,sizeof(*c));c->patch=r->key;c->note=60;c->velocity=128;c->ready=1;
    std::fill(c->value,c->value+TS_SUNVOX_CONTROLS,-1);
}
int stream_control(sunvox_stream_command,void *,sunvox_engine *) { return 0; }
uint32_t read_u32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
std::string patch_name(const unsigned char *data,size_t size) {
    for(size_t at=8;at+8<=size;) {
        uint32_t n=read_u32(data+at+4);
        if(!memcmp(data+at,"SNAM",4)) {
            std::string name;
            for(size_t i=0;i<n && name.size()<79 && data[at+8+i];++i) {
                unsigned char ch=data[at+8+i];
                if(ch>=32 && ch!=127)name+=(char)ch;
            }
            if(!name.empty())return name;
        }
        at+=8+n;
    }
    return "Imported SunVox patch";
}
}
struct TsSunVoxEngine {
    sunvox_engine engine{};
    TsSunVoxControls requested{}, applied{};
    unsigned rate=0, cursor=0;
    int patch=-1, input=-1;
    uint64_t frames=0;
    float input_buffer[TS_SUNVOX_QUANTUM*2]{};
    float output_buffer[TS_SUNVOX_QUANTUM*2]{};
    float dry_buffer[TS_SUNVOX_QUANTUM*2]{};
    size_t patch_bytes=0;
    int initial[TS_SUNVOX_CONTROLS]{};
};

int ts_sunvox_factory_count() { return sizeof(factories)/sizeof(factories[0]); }
const char *ts_sunvox_factory_name(int i) {
    return i>=0 && i<ts_sunvox_factory_count()?factories[i].name:"";
}
const char *ts_sunvox_factory_category(int i) {
    return i>=0 && i<ts_sunvox_factory_count()?factories[i].category:"";
}
int ts_sunvox_select(TsSunVoxControls *c,int i) {
    if(!c || i<0 || i>=ts_sunvox_factory_count())return 0;
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    select_resource(c,resources[i].get());return 1;
}
void ts_sunvox_default(TsSunVoxControls *c) {
    int at=0;
    for(int i=0;i<ts_sunvox_factory_count();++i)
        if(!strcmp(factories[i].name,"cube48 ShimmerClassic"))at=i;
    ts_sunvox_select(c,at);
}
int ts_sunvox_import(TsSunVoxControls *c,const char *path) {
    return ts_sunvox_import_ex(c,path,nullptr,0);
}
int ts_sunvox_import_ex(TsSunVoxControls *c,const char *path,char *error,size_t error_size) {
    if(error && error_size)error[0]=0;
    auto fail=[&](const char *message)->int {
        if(error && error_size)snprintf(error,error_size,"%s",message);
        return 0;
    };
    if(!c)return fail("No SunVox effect selected.");
    try {
    std::string filename=path?path:"";
    size_t first=filename.find_first_not_of(" \t\r\n"),last=filename.find_last_not_of(" \t\r\n");
    filename=first==std::string::npos?"":filename.substr(first,last-first+1);
    if(filename.size()>=2 && ((filename.front()=='\"' && filename.back()=='\"') ||
        (filename.front()=='\'' && filename.back()=='\'')))filename=filename.substr(1,filename.size()-2);
    if(filename.empty())return fail("Choose a .sunsynth file with BROWSE, paste its path, or drop it here.");
#ifdef _WIN32
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename.c_str(),-1,nullptr,0);
    if(!count)return fail("The patch path is not valid UTF-8. Choose the file again.");
    std::vector<wchar_t> wide((size_t)count);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename.c_str(),-1,wide.data(),count);
    FILE *f=_wfopen(wide.data(),L"rb");
#else
    FILE *f=fopen(filename.c_str(),"rb");
#endif
    if(!f)return fail(errno==ENOENT?"Patch file not found. Check the path or choose it with BROWSE.":
        errno==EACCES?"Permission denied reading the patch file.":"Cannot open the patch file for reading.");
    std::unique_ptr<FILE,decltype(&fclose)> file(f,fclose);
    if(fseek(f,0,SEEK_END))return fail("Cannot read the patch file. Choose a regular .sunsynth file.");
    long bytes=ftell(f);rewind(f);
    if(bytes<0)return fail("Cannot determine the patch file size.");
    if((size_t)bytes>max_patch)return fail("Patch exceeds the 8 MiB per-file limit.");
    if(bytes<16)return fail("Patch is empty or truncated; expected a .sunsynth module.");
    std::vector<unsigned char> data((size_t)bytes);
    if(fread(data.data(),1,data.size(),f)!=data.size())return fail("Could not read the complete patch file.");
    if(!memcmp(data.data(),"SVOX",4))return fail("This is a .sunvox song. Export it as a .sunsynth MetaModule first.");
    if(!patch_shape(data.data(),data.size()))return fail("Invalid or truncated .sunsynth module. Export it again from SunVox.");
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    uint64_t key=hash_bytes(data.data(),data.size());Resource *r=find_resource(key);
    if(r && (r->size!=data.size() || (r->complete && memcmp(r->data,data.data(),data.size()))))
        return fail("Patch identity conflicts with a cached file. Save your project and restart TapeSister.");
    if(!r) {
        if(imported_bytes+data.size()>max_imports)return fail("Imported patches reached the 128 MiB cache limit. Save your project and restart TapeSister.");
        std::unique_ptr<Resource> next(new Resource);r=next.get();
        r->key=key;r->name=patch_name(data.data(),data.size());r->category="Imported";
        r->size=data.size();r->owned=std::move(data);r->data=r->owned.data();r->complete=true;
        resources.push_back(std::move(next));imported_bytes+=r->size;
    } else if(!r->complete) {
        // A failed project load may have reserved this key with partial bytes.
        // A fully validated import repairs it without charging the cache twice.
        r->name=patch_name(data.data(),data.size());r->owned=std::move(data);
        r->data=r->owned.data();r->received=0;r->complete=true;
    }
    select_resource(c,r);return 1;
    } catch(const std::bad_alloc &) {
        return fail("Not enough memory to import this patch.");
    }
}
const char *ts_sunvox_name(const TsSunVoxControls *c) {
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);return r && r->complete?r->name.c_str():"SunVox FX";
}
const char *ts_sunvox_category(const TsSunVoxControls *c) {
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);return r && r->complete?r->category.c_str():"";
}
int ts_sunvox_valid(const TsSunVoxControls *c) {
    if(!c || !c->patch || !c->ready || c->note<0 || c->note>119 || c->velocity<0 || c->velocity>256 ||
        c->held<0 || c->held>1)return 0;
    for(int v:c->value)if(v < -1 || v>32768)return 0;
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);return r && r->complete;
}
int ts_sunvox_parameter_count(const TsSunVoxControls *c) {
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);return r?(int)r->params.size():0;
}
int ts_sunvox_parameter(const TsSunVoxControls *c,int i,TsSunVoxParameter *out) {
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);
    if(!r || i<0 || i>=(int)r->params.size())return 0;
    *out=r->params[i];return 1;
}
TsSunVoxEngine *ts_sunvox_new(const TsSunVoxControls *c,unsigned rate) {
    if(!ts_sunvox_valid(c) || rate<8000 || rate>192000)return nullptr;
    Resource *r;
    {std::lock_guard<std::mutex> guard(resources_mutex);r=find_resource(c->patch);}
    std::unique_ptr<TsSunVoxEngine> s(new TsSunVoxEngine);
    s->rate=rate;s->patch_bytes=r->size;
    sunvox_engine_init(SUNVOX_FLAG_CREATE_MODULES|SUNVOX_FLAG_ONE_THREAD|SUNVOX_FLAG_MAIN|
        SUNVOX_FLAG_NO_GUI|SUNVOX_FLAG_NO_SCOPE|SUNVOX_FLAG_NO_MIDI,rate,
        nullptr,nullptr,stream_control,nullptr,&s->engine);
    sfs_file file=sfs_open_in_memory((void *)r->data,r->size);
    s->patch=sunvox_load_module_from_fd(-1,0,0,0,file,0,&s->engine);sfs_close(file);
    if(s->patch<1){sunvox_engine_close(&s->engine);return nullptr;}
    s->input=psynth_add_module(-1,get_module_handler_by_name("Input",&s->engine),"Input",0,
        0,0,0,s->engine.bpm,s->engine.speed,s->engine.net);
    if(s->input<1){sunvox_engine_close(&s->engine);return nullptr;}
    psynth_do_command(s->input,PS_CMD_SETUP_FINISHED,s->engine.net);
    psynth_make_link(s->patch,s->input,s->engine.net);
    psynth_make_link(0,s->patch,s->engine.net);
    s->engine.net->global_volume=256;
    // Input defaults to mono in SunVox. Set stereo before the first audio frame.
    psynth_module *input=psynth_get_module(s->input,s->engine.net);
    *input->ctls[1].val=1;
    psynth_module *mod=psynth_get_module(s->patch,s->engine.net);
    {
        std::lock_guard<std::mutex> guard(resources_mutex);
        if(!r->described) {
            int count=std::min<int>(TS_SUNVOX_CONTROLS,mod->ctls_num);
            for(int i=0;i<count;++i) {
                TsSunVoxParameter p{};auto &ctl=mod->ctls[i];
                snprintf(p.name,sizeof(p.name),"%s",ctl.name?ctl.name:"Control");
                snprintf(p.unit,sizeof(p.unit),"%s",ctl.label?ctl.label:"");
                p.minimum=ctl.min;p.maximum=ctl.max;p.initial=*ctl.val;p.offset=ctl.show_offset;p.discrete=ctl.type==1;
                r->params.push_back(p);
            }
            r->described=true;
        }
    }
    for(int i=0;i<std::min<int>(TS_SUNVOX_CONTROLS,mod->ctls_num);++i) {
        auto &ctl=mod->ctls[i];
        s->initial[i]=ctl.type==1?*ctl.val:ctl.max==ctl.min?0:
            (int)((int64_t)(*ctl.val-ctl.min)*32768/(ctl.max-ctl.min));
    }
    s->requested=*c;s->applied=*c;
    std::fill(s->applied.value,s->applied.value+TS_SUNVOX_CONTROLS,-2);
    s->applied.held=0;
    return s.release();
}
void ts_sunvox_free(TsSunVoxEngine *s) {
    if(s){sunvox_engine_close(&s->engine);delete s;}
}
void ts_sunvox_set(TsSunVoxEngine *s,const TsSunVoxControls *c) { if(s)s->requested=*c; }
void ts_sunvox_process(TsSunVoxEngine *s,float l,float r,float wet[2],float dry[2]) {
    unsigned at=s->cursor*2;
    wet[0]=s->output_buffer[at];wet[1]=s->output_buffer[at+1];
    dry[0]=s->dry_buffer[at];dry[1]=s->dry_buffer[at+1];
    s->input_buffer[at]=std::isfinite(l)?l:0;s->input_buffer[at+1]=std::isfinite(r)?r:0;
    if(++s->cursor<TS_SUNVOX_QUANTUM)return;
    s->cursor=0;
    stime_ticks_t t=(stime_ticks_t)(s->frames*stime_ticks_per_second()/s->rate);
    psynth_module *m=psynth_get_module(s->patch,s->engine.net);
    for(int i=0;i<std::min<int>(TS_SUNVOX_CONTROLS,m->ctls_num);++i) {
        int v=s->requested.value[i];
        if(v!=s->applied.value[i] && (v>=0 || s->applied.value[i]>=0)) {
            psynth_event event{};event.command=PS_CMD_SET_GLOBAL_CONTROLLER;
            event.controller.ctl_num=i;event.controller.ctl_val=v;
            // SunVox controller events use raw values for enum controls.
            if(v<0)event.controller.ctl_val=s->initial[i];
            else if(m->ctls[i].type==1)
                event.controller.ctl_val=m->ctls[i].min+(int)((int64_t)v*(m->ctls[i].max-m->ctls[i].min)/32768);
            sunvox_add_psynth_event_UNSAFE(s->patch,&event,&s->engine);
        }
    }
    if(s->requested.held!=s->applied.held ||
       (s->requested.held && s->requested.note!=s->applied.note)) {
        if(s->applied.held)svh_send_event(&s->engine,t,0,128,0,s->patch+1,0,0);
        if(s->requested.held)svh_send_event(&s->engine,t,0,s->requested.note+1,s->requested.velocity,s->patch+1,0,0);
    }
    s->applied=s->requested;
    sunvox_render_data data{};
    data.in_type=sound_buffer_float32;data.in_buffer=s->input_buffer;data.in_channels=2;
    data.buffer_type=sound_buffer_float32;data.buffer=s->output_buffer;
    data.frames=TS_SUNVOX_QUANTUM;data.channels=2;data.out_time=t;
    if(!sunvox_render_piece_of_sound(&data,&s->engine) || data.silence)
        memset(s->output_buffer,0,sizeof(s->output_buffer));
    for(float &value:s->output_buffer)if(!std::isfinite(value))value=0;
    memcpy(s->dry_buffer,s->input_buffer,sizeof(s->dry_buffer));
    s->frames+=TS_SUNVOX_QUANTUM;
}
size_t ts_sunvox_memory(const TsSunVoxEngine *s) { return s?sizeof(*s)+s->patch_bytes:0; }

int ts_sunvox_write(FILE *file,const char *prefix,const TsSunVoxControls *c) {
    if(!file || !prefix || !ts_sunvox_valid(c))return 0;
    std::lock_guard<std::mutex> guard(resources_mutex);
    Resource *r=find_resource(c->patch);
    if(fprintf(file,"%s.Format=1\n%s.Patch=%016llx\n",prefix,prefix,(unsigned long long)c->patch)<0)return 0;
    if(r->factory<0) {
        if(fprintf(file,"%s.Size=%zu\n",prefix,r->size)<0)return 0;
        for(size_t at=0;at<r->size;at+=256) {
            if(fprintf(file,"%s.Data%zu=",prefix,at)<0)return 0;
            for(size_t i=at;i<std::min(at+256,r->size);++i)
                if(fprintf(file,"%02x",r->data[i])<0)return 0;
            if(fputc('\n',file)==EOF)return 0;
        }
        if(fprintf(file,"%s.Complete=1\n",prefix)<0)return 0;
    }
    for(int i=0;i<TS_SUNVOX_CONTROLS;++i)
        if(c->value[i]>=0 && fprintf(file,"%s.Ctl%d=%d\n",prefix,i,c->value[i])<0)return 0;
    return fprintf(file,"%s.Note=%d,%d,%d\n",prefix,c->note,c->velocity,c->held)>=0;
}
int ts_sunvox_read(TsSunVoxControls *c,const char *key,const char *value) {
    globals();std::lock_guard<std::mutex> guard(resources_mutex);
    if(!strcmp(key,"Format")) {
        if(strcmp(value,"1"))return -1;
        memset(c,0,sizeof(*c));std::fill(c->value,c->value+TS_SUNVOX_CONTROLS,-1);
        c->note=60;c->velocity=128;return 1;
    }
    char extra;unsigned long long number=0;
    if(!strcmp(key,"Patch")) {
        if(strlen(value)!=16 || sscanf(value,"%16llx%c",&number,&extra)!=1 || !number)return -1;
        c->patch=number;
        Resource *r=find_resource(c->patch);c->ready=r && r->factory>=0;
        return 1;
    }
    if(!strcmp(key,"Size")) {
        if(sscanf(value,"%llu%c",&number,&extra)!=1 || number<16 || number>max_patch || !c->patch)return -1;
        c->ready=0;c->received=0;
        Resource *r=find_resource(c->patch);
        if(r) {
            if(r->size!=number)return -1;
            if(!r->complete)r->received=0;
        } else {
            if(imported_bytes+number>max_imports)return -1;
            std::unique_ptr<Resource> next(new Resource);r=next.get();
            r->key=c->patch;r->size=(size_t)number;r->owned.resize(r->size);r->data=r->owned.data();
            r->category="Imported";imported_bytes+=r->size;resources.push_back(std::move(next));
        }
        return 1;
    }
    if(!strncmp(key,"Data",4)) {
        if(sscanf(key+4,"%llu%c",&number,&extra)!=1)return -1;
        Resource *r=find_resource(c->patch);size_t n=strlen(value);
        if(!r || !n || n%2 || n>512 || number>r->size || n/2>r->size-number ||
            number!=c->received)return -1;
        auto nibble=[](char ch)->int {
            if(ch>='0' && ch<='9')return ch-'0';
            if(ch>='a' && ch<='f')return ch-'a'+10;
            return -1;
        };
        for(size_t i=0;i<n;i+=2) {
            int a=nibble(value[i]),b=nibble(value[i+1]);if(a<0 || b<0)return -1;
            unsigned char byte=(unsigned char)(a*16+b);
            if(r->complete){if(r->data[number+i/2]!=byte)return -1;}
            else r->owned[number+i/2]=byte;
        }
        if(!r->complete)r->received+=n/2;
        c->received+=n/2;
        return 1;
    }
    if(!strcmp(key,"Complete")) {
        Resource *r=find_resource(c->patch);
        if(strcmp(value,"1") || !r || c->received!=r->size)return -1;
        if(!r->complete) {
            if(r->received!=r->size || !patch_shape(r->data,r->size) || hash_bytes(r->data,r->size)!=r->key)return -1;
            r->name=patch_name(r->data,r->size);r->complete=true;
        }
        c->ready=1;c->received=0;return 1;
    }
    if(!strncmp(key,"Ctl",3)) {
        int at=-1,v=-1;
        if(sscanf(key+3,"%d%c",&at,&extra)!=1 || at<0 || at>=TS_SUNVOX_CONTROLS ||
            sscanf(value,"%d%c",&v,&extra)!=1 || v<0 || v>32768)return -1;
        c->value[at]=v;return 1;
    }
    if(!strcmp(key,"Note")) {
        int note,velocity,held;
        if(sscanf(value,"%d,%d,%d%c",&note,&velocity,&held,&extra)!=3 ||
            note<0 || note>119 || velocity<0 || velocity>256 || held<0 || held>1)return -1;
        c->note=note;c->velocity=velocity;c->held=held;return 1;
    }
    return -1;
}
