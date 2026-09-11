/* Scalar subsets of the pinned CDP8 suite. Source-dependent checks and stereo
   policies live in ts_cdp_portal_expansion.inc. IDs and parameter order persist. */
#define XPAR(id,label,help,flag,type,lo,hi,def) {id,label,help,flag,TS_PORTAL_##type,lo,hi,def}
#define XGAIN XPAR("gain","OUTPUT GAIN","LINEAR OUTPUT MULTIPLIER","",REAL,.1,1,.3)
#define XSEED(flag) XPAR("seed","SEED","POSITIVE SEED REPEATS THE SAME RANDOM CHOICES",flag,INTEGER,1,32767,17)
#define XOUT XPAR("duration","OUTPUT SECONDS","TARGET TIME; A FINAL SEGMENT OR TAIL CAN EXTEND IT","",REAL,.25,20,2)
#define XREPS XPAR("repeats","REPETITIONS","NUMBER OF REPEATS","",INTEGER,2,12,6)
#define XSPLICE(flag) XPAR("splice","SPLICE MS","CROSSFADE AT SEGMENT BOUNDARIES",flag,REAL,2,20,5)
#define XITER XPAR("delay","DELAY SECONDS","AVERAGE TIME BETWEEN COPIES","-d",REAL,.02,1,.15), \
 XPAR("random","TIME RANDOM","RANDOMIZE THE DELAY BETWEEN COPIES","-r",REAL,0,.8,.15), \
 XPAR("pitch","PITCH SCATTER","MAXIMUM RANDOM TRANSPOSITION IN SEMITONES","-p",REAL,0,12,2), \
 XPAR("amplitude","AMP SCATTER","MAXIMUM RANDOM AMPLITUDE REDUCTION","-a",REAL,0,.8,.1), \
 XPAR("fade","FADE PER COPY","PROPORTION LOST WITH EACH REPEAT","-f",REAL,0,.8,.15), \
 XPAR("gain","INPUT GAIN","PRESCALE BEFORE OVERLAPPING COPIES","-g",REAL,.05,.5,.2), XSEED("-s")
#define XFREEZE XPAR("delay","DELAY SECONDS","AVERAGE TIME BETWEEN SEGMENT COPIES","",REAL,.01,.3,.06), \
 XPAR("random","TIME RANDOM","RANDOMIZE COPY TIMING","",REAL,0,.8,.1), \
 XPAR("pitch","PITCH SCATTER","MAXIMUM RANDOM TRANSPOSITION IN SEMITONES","",REAL,0,12,2), \
 XPAR("amplitude","AMP SCATTER","MAXIMUM RANDOM AMPLITUDE REDUCTION","",REAL,0,.8,.1), \
 XPAR("start","START SECONDS","BEGINNING OF THE FROZEN SOURCE REGION","",REAL,0,10,.15), \
 XPAR("end","END SECONDS","END OF THE FROZEN SOURCE REGION","",REAL,.04,20,.35), \
 XPAR("gain","SEGMENT GAIN","GAIN OF THE REPEATED REGION","",REAL,.25,1,.5), XSEED("-s")
#define XDRUNK XOUT, XPAR("start","START SECONDS","INITIAL POSITION IN THE SOURCE","",REAL,.05,10,.5), \
 XPAR("ambitus","TRAVEL SECONDS","MAXIMUM DISTANCE FROM INITIAL POSITION","",REAL,.02,5,.3), \
 XPAR("step","STEP SECONDS","LARGEST STEP BETWEEN READ POSITIONS","",REAL,.005,1,.15), \
 XPAR("clock","CLOCK SECONDS","TIME BETWEEN CHANGES OF DIRECTION","",REAL,.01,.5,.08)
#define XSYLL XPAR("window","WINDOW MS","ENVELOPE WINDOW FOR FINDING SYLLABLE-LIKE SEGMENTS","",INTEGER,10,100,20), \
 XPAR("splice","SPLICE MS","CROSSFADE AT SEGMENT EDGES","",INTEGER,2,15,5)
#define XSYLLREPS XPAR("repeats","REPETITIONS","NUMBER OF VERSIONS OF EACH SEGMENT","",INTEGER,2,8,3), \
 XPAR("random","LENGTH RANDOM","RANDOMIZE SEGMENT LENGTHS; ZERO IS REPEATABLE","",REAL,0,.5,0)
#define XFOLD XPAR("start_bin","FIRST BIN","START OF THE REGION; BIN WIDTH IS SAMPLE RATE / 1024","",INTEGER,1,400,20), \
 XPAR("bins","BIN COUNT","NUMBER OF BINS TO TRANSFORM; MUST FIT ANALYSIS","",INTEGER,8,400,256)
#define XFORMANTGAIN XPAR("gain","OUTPUT GAIN","GAIN AFTER THE FORMANT TRANSFORMATION","-g",REAL,.1,1,.5)

static const TsPortalProcess expansion_processes[] = {
 {"modify.speed.5","TAPE ACCELERATION","Continuously change tape speed and pitch. Acceleration continues past the goal time. Strong deceleration stops at CDP's minimum speed, possibly before the source ends.","speed",1,5,3,1,
  {XPAR("ratio","TARGET RATIO","SPEED RATIO REACHED AT GOAL TIME","",REAL,.5,3,1.5),XPAR("goal","GOAL SECONDS","OUTPUT TIME WHEN TARGET SPEED IS REACHED","",REAL,.1,4,.5),XPAR("start","START SECONDS","TIME BEFORE ACCELERATION BEGINS","-s",REAL,0,2,.05)},TS_PORTAL_TIME,"modify"},
 {"modify.stack","TRANSPOSITION STACK","Layer transposed copies of the source at a fixed semitone interval. Lean sets the highest layer's level relative to the lowest; attack aligns their transients.","stack",1,0,6,1,
  {XPAR("interval","INTERVAL SEMITONES","PITCH INTERVAL BETWEEN SUCCESSIVE LAYERS","",REAL,-12,12,7),XPAR("count","LAYERS","NUMBER OF TRANSPOSED COPIES","",INTEGER,2,6,3),XPAR("lean","HIGH LAYER LEVEL","HIGHEST LAYER RELATIVE TO LOWEST","",REAL,.1,2,.7),XPAR("attack","ATTACK SECONDS","SOURCE ATTACK TIME USED TO ALIGN LAYERS","",REAL,0,2,0),XGAIN,XPAR("length","LENGTH FRACTION","PROPORTION OF THE RESULT TO RETAIN","",REAL,.1,1,1)},TS_PORTAL_TIME,"modify"},
 {"extend.zigzag.1","ZIGZAG TRAVERSAL","Read forward and backward through a source region with seeded random turnarounds. One shared traversal is used for stereo; output can outlast the requested time.","zigzag",1,1,7,1,
  {XPAR("start","START SECONDS","BEGINNING OF THE TRAVERSAL REGION","",REAL,0,10,.1),XPAR("end","END SECONDS","END OF THE TRAVERSAL REGION","",REAL,.1,20,.8),XOUT,XPAR("minimum","MIN SEGMENT SEC","SHORTEST TRAVERSAL BETWEEN TURNS","",REAL,.04,1,.1),XSPLICE("-s"),XPAR("maximum","MAX SEGMENT SEC","LONGEST TRAVERSAL BETWEEN TURNS","-m",REAL,.04,2,.4),XSEED("-r")},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.drunk.1","DRUNK TRAVERSAL","Wander around an initial source position, reversing direction at clock intervals. Ambitus limits travel, step limits each move, and seed recalls the same walk.","drunk",1,1,7,1,
  {XDRUNK,XSPLICE("-s"),XSEED("-r")},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.drunk.2","DRUNK WITH PAUSES","A seeded source walk interrupted by sober stretches that keep travelling in one direction. The sober segment controls shape the contrast with the wandering sections.","drunk",1,2,11,1,
  {XDRUNK,XPAR("min_ticks","MIN CLOCK TICKS","SHORTEST WALK BEFORE A SOBER SEGMENT","",INTEGER,2,16,4),XPAR("max_ticks","MAX CLOCK TICKS","LONGEST WALK BEFORE A SOBER SEGMENT","",INTEGER,3,32,8),XSPLICE("-s"),XSEED("-r"),XPAR("sober_min","MIN SOBER SEC","SHORTEST SOBER SEGMENT","-l",REAL,.04,1,.1),XPAR("sober_max","MAX SOBER SEC","LONGEST SOBER SEGMENT","-h",REAL,.05,2,.2)},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.iterate.1","ITERATE TO TIME","Overlap copies with seeded timing, pitch and level variation until the requested time. The final copy can extend the output; fade progressively thins the repetitions.","iterate",1,1,8,1,
  {XOUT,XITER},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.iterate.2","ITERATE COPIES","Overlap a chosen number of source copies with seeded timing, pitch and amplitude variation. Gain controls overlap level; fade reduces later repetitions.","iterate",1,2,8,1,
  {XREPS,XITER},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.freeze.1","FREEZE TO TIME","Repeat a region of the source to extend it into a texture, retaining material around it. Seeded delay, pitch and amplitude scatter animate the frozen region.","freeze",1,1,9,1,
  {XOUT,XFREEZE},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.freeze.2","FREEZE COPIES","Repeat a source region a chosen number of times, retaining the surrounding material. Seeded timing and pitch scatter turn a small event into a larger gesture.","freeze",1,2,9,1,
  {XREPS,XFREEZE},TS_PORTAL_STRUCTURE,"extend"},
 {"extend.baktobak","BACK-TO-BACK","Play the source tail backward into a chosen join point, then forward out again. The splice smooths the meeting of the reversed and forward gestures.","baktobak",1,0,2,1,
  {XPAR("join","JOIN SECONDS","TIME IN SOURCE WHERE REVERSE MEETS FORWARD","",REAL,.02,10,.25),XSPLICE("")},TS_PORTAL_STRUCTURE,"extend"},
 {"bounce.bounce","BOUNCING REPEATS","Repeat the source with progressively shorter gaps and decreasing level, like a bouncing object. Long sounds overlap or shorten to fit the bounce sequence.","bounce",1,0,6,1,
  {XPAR("count","BOUNCES","NUMBER OF REPEATS AFTER THE INITIAL SOUND","",INTEGER,2,16,6),XPAR("gap","FIRST GAP SEC","TIME BETWEEN THE FIRST TWO ENTRIES","",REAL,.04,2,.3),XPAR("shorten","GAP RATIO","MULTIPLIER FOR EACH SUCCESSIVE GAP","",REAL,.4,.95,.75),XPAR("end_level","LAST LEVEL","FINAL BOUNCE LEVEL RELATIVE TO FIRST","",REAL,.01,.8,.1),XPAR("curve","DECAY CURVE","CURVATURE OF LEVEL REDUCTION","",REAL,.25,4,1),XPAR("minimum","MIN SOUND SEC","LOWER LIMIT ON SHORTENED BOUNCE SOUNDS","-s",REAL,.02,.1,.03)},TS_PORTAL_STRUCTURE,"bounce"},
 {"dvdwind.dvdwind","FAST SCAN","Speed through a sound by retaining short chunks and skipping material between them. Unlike tape transposition, each retained chunk keeps its original playback rate.","dvdwind",1,0,2,1,
  {XPAR("speed","SCAN SPEED","RATE OF TRAVEL THROUGH THE SOURCE","",REAL,1.1,8,2),XPAR("chunk","CHUNK MS","LENGTH OF EACH RETAINED CHUNK","",REAL,10,100,30)},TS_PORTAL_TIME,"dvdwind"},
 {"filter.lohi.1","LOW / HIGH PASS","Filter using passband and stopband edges with a specified attenuation. Pass below stop makes a low-pass; pass above stop makes a high-pass.","lohi",1,1,5,1,
  {XPAR("attenuation","REJECTION DB","ATTENUATION IN THE STOPBAND","",REAL,-60,-12,-40),XPAR("pass","PASS EDGE HZ","FREQUENCY AT THE PASSBAND EDGE","",REAL,100,10000,1000),XPAR("stop","STOP EDGE HZ","FREQUENCY AT THE STOPBAND EDGE","",REAL,100,10000,2500),XPAR("tail","TAIL SECONDS","EXTRA TIME FOR FILTER RINGING","-t",REAL,.01,1,.1),XPAR("gain","INPUT GAIN","PRESCALE BEFORE FILTERING","-s",REAL,.1,1,.5)},TS_PORTAL_FILTER,"filter"},
 {"newdelay.newdelay","TUNED FEEDBACK DELAY","A very short feedback delay tuned to a MIDI pitch. Repetitions combine into a pitched resonator; mix sets the delayed contribution and feedback controls ringing.","newdelay",1,0,3,1,
  {XPAR("pitch","MIDI PITCH","PITCH OF THE DELAY COMB","",REAL,24,84,48),XPAR("mix","DELAY MIX","CONTRIBUTION OF THE DELAYED SIGNAL","",REAL,.05,.9,.4),XPAR("feedback","FEEDBACK","POSITIVE FEEDBACK; BOUNDED TO KEEP TAILS FINITE","",REAL,0,.8,.5)},TS_PORTAL_DELAY,"newdelay"},
 {"silend.silend.1","APPEND SILENCE","Add silence after the sound, retaining the original samples. Useful for leaving room after a gesture in a chain or preparing a longer tile.","silend",1,1,1,1,
  {XPAR("silence","SILENCE SECONDS","AMOUNT OF SILENCE TO APPEND","",REAL,.01,10,.25)},TS_PORTAL_STRUCTURE,"silend"},
 {"envspeak.envspeak.1","SYLLABLE REPEATS","Find syllable-like envelope segments and repeat each one. This follows amplitude changes, not language; window and splice determine the scale of segmentation.","envspeak",1,1,4,1,
  {XSYLL,XSYLLREPS},TS_PORTAL_ENVELOPE,"envspeak"},
 {"envspeak.envspeak.2","SYLLABLE REVERSE REPEAT","Follow each envelope segment with its reversed version. Window controls which gestures count as segments; splices soften the joins.","envspeak",1,2,2,1,
  {XSYLL},TS_PORTAL_ENVELOPE,"envspeak"},
 {"envspeak.envspeak.5","SYLLABLE SHRINK TAIL","Repeat each envelope segment while progressively shortening it from the end. Attacks survive while the repeated tails become smaller.","envspeak",1,5,4,1,
  {XSYLL,XSYLLREPS},TS_PORTAL_ENVELOPE,"envspeak"},
 {"envspeak.envspeak.6","SYLLABLE SHRINK HEAD","Repeat each envelope segment while progressively removing material from its beginning. Later repetitions reveal the end of each gesture.","envspeak",1,6,4,1,
  {XSYLL,XSYLLREPS},TS_PORTAL_ENVELOPE,"envspeak"},
 {"tremenv.tremenv","AFTER-PEAK TREMOLO","Introduce tremolo after the source's peak. Window controls peak detection; squeeze narrows the tremolo pulses for increasingly chopped tails.","tremenv",1,0,4,0,
  {XPAR("rate","RATE HZ","TREMOLO CYCLES PER SECOND","",REAL,.5,30,8),XPAR("depth","DEPTH","AMOUNT OF AMPLITUDE MODULATION","",REAL,0,1,.6),XPAR("window","WINDOW MS","ENVELOPE WINDOW USED TO LOCATE THE PEAK","",REAL,2,40,20),XPAR("squeeze","PULSE SQUEEZE","NARROW THE TREMOLO PULSES","",REAL,1,8,2)},TS_PORTAL_ENVELOPE,"tremenv"},
 {"gate.gate.1","THRESHOLD GATE","Replace quiet material with silence using a level threshold. Works on mono audio; low thresholds retain more of the source.","gate",1,1,1,0,
  {XPAR("threshold","THRESHOLD DB","LEVEL BELOW WHICH MATERIAL IS SILENCED","",REAL,-72,-12,-36)},TS_PORTAL_LEVEL,"gate"},
 {"gate.gate.2","REMOVE QUIET MATERIAL","Delete quiet material below a threshold and close the gaps. This shortens the source; it works on mono audio.","gate",1,2,1,1,
  {XPAR("threshold","THRESHOLD DB","LEVEL BELOW WHICH MATERIAL IS REMOVED","",REAL,-72,-12,-36)},TS_PORTAL_STRUCTURE,"gate"},
 {"clip.clip.1","PEAK-RESTORED CLIP","Clip at an absolute amplitude, then restore the original peak level. This adds distortion and raises the quieter parts relative to the clipped peaks.","clip",1,1,1,0,
  {XPAR("threshold","CLIP LEVEL","ABSOLUTE AMPLITUDE THRESHOLD; MUST BE BELOW SOURCE PEAK","",REAL,.005,.8,.05)},TS_PORTAL_LOFI,"clip"},
 {"clip.clip.2","HALF-WAVE CLIP","Clip each half-wave at a fraction of its own amplitude, then restore its level. This changes wave shape independently of the overall envelope; mono only.","clip",1,2,1,0,
  {XPAR("fraction","CLIP FRACTION","FRACTION OF EACH HALF-WAVE TO RETAIN","",REAL,.1,.9,.5)},TS_PORTAL_LOFI,"clip"},
 {"quirk.quirk.1","HALF-WAVE QUIRK","Reshape amplitude inside each half-wave using a power curve. Low powers fill out wave shapes; high powers make them narrower. Partial edge waves may be removed.","quirk",1,1,1,1,
  {XPAR("power","POWER","EXPONENT OF THE AMPLITUDE CURVE","",REAL,.25,4,2)},TS_PORTAL_LOFI,"quirk"},
 {"quirk.quirk.2","SIGNAL QUIRK","Apply amplitude curvature relative to the signal's overall range. This changes the envelope as well as wave shape; incomplete edge waves may be removed. Mono only.","quirk",1,2,1,1,
  {XPAR("power","POWER","EXPONENT OF THE AMPLITUDE CURVE","",REAL,.25,4,2)},TS_PORTAL_LOFI,"quirk"},
 {"spec.magnify","SPECTRAL MAGNIFY","Hold the spectrum at one source moment as a sustained texture. Choose a moment with energy; a quiet window gives a quiet drone. Mono analysis and resynthesis are automatic.","magnify",1,0,2,1,
  {XPAR("position","SOURCE SECONDS","MOMENT WHOSE SPECTRUM WILL BE HELD","",REAL,0,10,.08),XOUT},TS_PORTAL_SPECTRAL,"spec"},
 {"spec.gate","SPECTRAL GATE","Remove spectral components below an amplitude threshold, revealing stronger partials. Low thresholds retain more detail; high thresholds can approach silence.","gate",1,0,1,0,
  {XPAR("threshold","PARTIAL THRESHOLD","AMPLITUDE BELOW WHICH SPECTRAL COMPONENTS ARE REMOVED","",REAL,.0001,.1,.005)},TS_PORTAL_SPECTRAL,"spec"},
 {"blur.drunk","SPECTRAL WALK","Wander through analysis windows rather than reading audio forward in time. Step range controls the jumps; output duration controls the length of the reconstructed texture.","drunk",1,0,4,1,
  {XPAR("range","STEP WINDOWS","MAXIMUM JUMP THROUGH ANALYSIS WINDOWS","",INTEGER,1,32,8),XPAR("start","START SECONDS","INITIAL POSITION IN THE ANALYSIS","",REAL,0,10,.1),XOUT,XPAR("no_zero","KEEP MOVING","DO NOT REPEAT AN UNCHANGED WINDOW POSITION","-z",SWITCH,0,1,1)},TS_PORTAL_SPECTRAL,"blur"},
 {"blur.scatter","SPECTRAL SCATTER","Retain scattered groups of spectral components. Keep count controls density and block width groups neighbouring bins. Choices vary each render; output normalization is optional.","scatter",1,0,3,0,
  {XPAR("keep","KEEP BLOCKS","NUMBER OF SPECTRAL BLOCKS TO RETAIN","",INTEGER,1,256,32),XPAR("width","BLOCK WIDTH HZ","FREQUENCY WIDTH OF EACH BLOCK","-b",REAL,50,1000,100),XPAR("no_normalize","NO NORMALIZE","RETAIN LEVEL LOSS FROM REMOVING PARTIALS","-n",SWITCH,0,1,1)},TS_PORTAL_SPECTRAL,"blur"},
 {"caltrain.caltrain","HIGH-BAND BLUR","Blur upper spectral regions more than lower ones. The dividing frequency preserves lower-frequency definition while higher partials form a haze.","caltrain",1,0,2,0,
  {XPAR("blur","BLUR SECONDS","TIME SPAN USED FOR SPECTRAL BLURRING","",REAL,.01,2,.1),XPAR("frequency","DIVIDE HZ","FREQUENCY DIVIDING THE BLUR REGIONS","",REAL,100,10000,1000)},TS_PORTAL_SPECTRAL,"caltrain"},
 {"superaccu.superaccu.2","TUNED SPECTRAL SUSTAIN","Sustain each spectral band until louder data arrives, initially tuning resonances to equal temperament. Decay is the amplitude retained each second; larger values leave longer tails.","superaccu",1,2,1,1,
  {XPAR("decay","RETAIN PER SECOND","FRACTION OF AMPLITUDE RETAINED AFTER ONE SECOND","-d",REAL,.001,.2,.01)},TS_PORTAL_SPECTRAL,"superaccu"},
 {"spectstr.stretch","DECORRELATED STRETCH","Stretch spectral time with controlled frequency randomization intended to reduce resynthesis artifacts. Compare transients and sustained tones; randomized results can differ each render.","stretch",1,0,3,1,
  {XPAR("ratio","TIME RATIO","OUTPUT DURATION MULTIPLIER","",REAL,.5,4,2),XPAR("proportion","VARY PROPORTION","PROPORTION OF SPECTRAL DATA TO VARY","",REAL,0,1,.2),XPAR("random","FREQUENCY RANDOM","AMOUNT OF RANDOM FREQUENCY VARIATION","",REAL,0,.5,.1)},TS_PORTAL_SPECTRAL,"spectstr"},
 {"specfold.specfold.1","FOLD SPECTRAL REGION","Fold a selected region of spectral bins repeatedly, remapping its internal frequency structure. First bin and count locate the region within the spectrum.","specfold",1,1,3,0,
  {XFOLD,XPAR("folds","FOLD COUNT","NUMBER OF FOLDS; MUST FIT THE REGION","",INTEGER,1,6,2)},TS_PORTAL_SPECTRAL,"specfold"},
 {"specfold.specfold.2","INVERT SPECTRAL REGION","Reverse the ordering of spectral data inside a selected region. Frequencies exchange positions within that region rather than reversing the sound in time.","specfold",1,2,2,0,
  {XFOLD},TS_PORTAL_SPECTRAL,"specfold"},
 {"specfold.specfold.3","PERMUTE SPECTRAL REGION","Permute spectral data inside a selected region using a positive seed. Recall the seed to repeat the same frequency rearrangement.","specfold",1,3,3,0,
  {XFOLD,XPAR("seed","SEED","POSITIVE SEED REPEATS THE PERMUTATION","",INTEGER,1,64,17)},TS_PORTAL_SPECTRAL,"specfold"},
 {"specfnu.specfnu.1","NARROW FORMANTS","Steepen the sides of spectral-envelope peaks to emphasize formants. Harmonic and unpitched material respond differently; gain sets the resulting output level.","specfnu",1,1,2,0,
  {XPAR("narrow","NARROWING","POWER USED TO NARROW FORMANT PEAKS","",REAL,1,16,4),XFORMANTGAIN},TS_PORTAL_SPECTRAL,"specfnu"},
 {"specfnu.specfnu.3","INVERT FORMANTS","Turn formant-envelope peaks into troughs and troughs into peaks. A nonzero modulation rate cycles between the original and inverted envelopes.","specfnu",1,3,2,0,
  {XPAR("vibrate","MODULATION HZ","ZERO HOLDS THE INVERSION; POSITIVE VALUES CYCLE IT","",REAL,0,8,0),XFORMANTGAIN},TS_PORTAL_SPECTRAL,"specfnu"},
 {"specfnu.specfnu.4","ROTATE FORMANTS","Move formant peaks and their frequencies around the formant region, wrapping at its boundaries. Positive and negative rates move in opposite directions.","specfnu",1,4,2,0,
  {XPAR("speed","ROTATIONS PER SEC","SIGNED RATE OF FORMANT ROTATION","",REAL,-2,2,.1),XFORMANTGAIN},TS_PORTAL_SPECTRAL,"specfnu"}
};
#undef XPAR
#undef XGAIN
#undef XSEED
#undef XOUT
#undef XREPS
#undef XSPLICE
#undef XITER
#undef XFREEZE
#undef XDRUNK
#undef XSYLL
#undef XSYLLREPS
#undef XFOLD
#undef XFORMANTGAIN
