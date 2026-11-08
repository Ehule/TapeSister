"""Apply two bounded allocation changes to the pinned SDK after extraction.

Fail closed if the upstream text changes. The archive itself remains pristine.
No DSP equations, logical delay lengths, or event ordering are changed.
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
def replace(path, before, after, count=1):
    file = root / path
    source = file.read_text()
    if source.count(before) != count:
        raise SystemExit(f"Unexpected pinned SunVox source: {path}")
    file.write_text(source.replace(before, after))

replace("lib_sunvox/sunvox_engine.cpp",
        "    s->stream_control = stream_control;",
        """    // TapeSister: reserve control bursts before any audio rendering, including
    // nested MetaModule engines. Keep upstream growth for larger internal bursts.
    s->psynth_events = SMEM_ALLOC2( sunvox_psynth_event, 256 );
    s->stream_control = stream_control;""")

path = "lib_sunvox/psynth/psynths_reverb.cpp"
replace(path, "static void realloc_filter_bufs( MODULE_DATA* data, psynth_net* pnet )",
        """// TapeSister: reserve the largest legal room/seed combination at setup.
// Logical lengths remain those of the saved patch. Unused storage stays zero,
// exactly as an upstream zero-extending resize would initialize it later.
static size_t tapesister_reverb_capacity( int base, bool right, int rate )
{
    int64_t largest = base / 2 + base - 1; // maximum pseudo_random() % base
    if( right ) largest += STEREO_SPREAD;
    int delta = ( rate << 11 ) / 44100;
    return ( ( largest * (16 * MAX_BUF_SCALE) * delta ) >> 11 ) / 16 + 128;
}
static void realloc_filter_bufs( MODULE_DATA* data, psynth_net* pnet )""")
needle = "f->buf = SMEM_ZALLOC2( PS_STYPE2, f->buf_size );"
file = root / path
source = file.read_text()
if source.count(needle) != 2:
    raise SystemExit("Unexpected pinned Reverb setup")
for kind in ("COMBS", "ALLPASSES"):
    base = "comb" if kind == "COMBS" else "allpass"
    source = source.replace(needle,
        f"f->buf = SMEM_ZALLOC2( PS_STYPE2, tapesister_reverb_capacity( g_{base}_base_size[ i % MAX_{kind} ], i >= MAX_{kind}, pnet->sampling_freq ) );", 1)
file.write_text(source)
