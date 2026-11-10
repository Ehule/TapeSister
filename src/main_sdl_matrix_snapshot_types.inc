/* A requested, single-slot snapshot. The audio writer never waits: READY data
   belongs to the UI until it releases the slot. Request fields travel with the
   release/acquire handoff, so ordinary payload fields have no concurrent access. */
enum { MATRIX_ACTIVITY_VOICES=TS_NOTE_BANK_VOICE_CAPACITY+3*TS_PERFORMANCE_VOICE_LIMIT+TS_BANK_SLOT_COUNT+1 };
typedef struct {
    TsSisterParameters parameters;TsMasterEqControls eq;TsEqMorph morph;
    int limiter_enabled;float ceiling,lookahead,release;
} FxProcessorState;
typedef struct {
    FxProcessorState fixed;TsMasterEqControls live_eq;int eq_morph_owned;
    TsMatrixControls controls;TsRouterControls legacy;TsSpatialControls array;
    TsSpeakerSetup speakers;TsCompressorControls compressor;
    TsAudioChannelRange input_range,output_range;
    unsigned inputs,outputs,sample_rate,processors;
    float peaks[TS_MATRIX_ROWS],sends[TS_MATRIX_ROWS],destinations[TS_MATRIX_DESTINATIONS];
    float compressor_gr,compressor_key,effect_gr[TS_MATRIX_EFFECT_CAPACITY],effect_progress[TS_MATRIX_EFFECT_CAPACITY];
    float program,tracker_gain;
    TsRouteActivity activity[MATRIX_ACTIVITY_VOICES],tracks[TS_TRACKER_LANES],tiles[TS_TAPEHEAD_ROUTE_ACTIVITY_COUNT];
    int activity_count;
    TsMosaic *mosaic; /* Identity only on UI; never dereference this pointer there. */
    uint64_t card_id[TS_MOSAIC_EVENTS],frame,epoch;
    float card_source[TS_MOSAIC_EVENTS],card_send[TS_MOSAIC_EVENTS];
} MatrixActivitySnapshot;
typedef struct {
    atomic_int state; /* 0=UI free, 1=requested, 2=audio writing, 3=UI ready. */
    uint64_t requested_epoch;
    MatrixActivitySnapshot value;
} MatrixActivityMailbox;
static uint64_t mx_activity_epoch; /* UI-owned; invalidate pre-edit controls. */
