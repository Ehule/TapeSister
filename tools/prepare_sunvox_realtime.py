"""Apply bounded allocation and exact delay-loop changes to the pinned SDK after extraction.

Fail closed if the upstream text changes. The archive itself remains pristine.
No DSP equations, logical delay lengths, or event ordering are changed.
Patch ASCII markers as bytes so the SDK's UTF-8 text and original line endings
survive unchanged, independent of the Python interpreter's Windows code page.
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
def replace(path, before, after, count=1):
    file = root / path
    before, after = before.encode("ascii"), after.encode("ascii")
    source = file.read_bytes()
    if source.count(before) != count:
        raise SystemExit(f"Unexpected pinned SunVox source: {path}")
    file.write_bytes(source.replace(before, after))

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
needle = b"f->buf = SMEM_ZALLOC2( PS_STYPE2, f->buf_size );"
file = root / path
source = file.read_bytes()
if source.count(needle) != 2:
    raise SystemExit("Unexpected pinned Reverb setup")
for kind in ("COMBS", "ALLPASSES"):
    base = "comb" if kind == "COMBS" else "allpass"
    source = source.replace(needle,
        f"f->buf = SMEM_ZALLOC2( PS_STYPE2, tapesister_reverb_capacity( g_{base}_base_size[ i % MAX_{kind} ], i >= MAX_{kind}, pnet->sampling_freq ) );".encode("ascii"), 1)
file.write_bytes(source)

# Keep a pristine, independently compiled oracle for the native Delay regression.
path = "lib_sunvox/psynth/psynths_delay.cpp"
reference = root / "reference/psynths_delay.cpp"
reference.parent.mkdir(parents=True, exist_ok=True)
reference.write_bytes((root / path).read_bytes())
# The original sample loop reads the delayed sample before writing the input.
# Split only at read/write ring boundaries; let the compiler check aliasing.
# Feedback/allpass, zero wet, dry mixing, events and logical lengths stay upstream.
replace(path, """			    if( data->ctl_inverse ) ctl_wet = -ctl_wet;
			    for( int i = 0; i < frames; i++ )
			    {
				PS_STYPE2 out_val;
				int ptr = buf_ptr - delay;
				if( ptr < 0 ) ptr += buf_size;
				out_val = PS_NORM_STYPE_MUL( cbuf[ ptr ], ctl_wet, 256 );
				out[ i ] = out_val;
				cbuf[ buf_ptr ] = in[ i ];
				buf_ptr++;
				if( buf_ptr >= buf_size ) buf_ptr = 0;
			    }""",
        """			    if( data->ctl_inverse ) ctl_wet = -ctl_wet;
                    // Split at ring boundaries, preserving the read-before-write order.
                    int ptr = buf_ptr - delay;
                    if( ptr < 0 ) ptr += buf_size;
                    for( int i = 0; i < frames; )
                    {
                        int count = frames - i;
                        if( count > buf_size - buf_ptr ) count = buf_size - buf_ptr;
                        if( count > buf_size - ptr ) count = buf_size - ptr;
                        for( int j = 0; j < count; j++ )
                        {
                            PS_STYPE2 out_val = PS_NORM_STYPE_MUL( cbuf[ ptr + j ], ctl_wet, 256 );
                            out[ i + j ] = out_val;
                            cbuf[ buf_ptr + j ] = in[ i + j ];
                        }
                        i += count;
                        ptr += count;
                        if( ptr >= buf_size ) ptr = 0;
                        buf_ptr += count;
                        if( buf_ptr >= buf_size ) buf_ptr = 0;
                    }""")
