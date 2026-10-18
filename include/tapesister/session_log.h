#ifndef TAPESISTER_SESSION_LOG_H
#define TAPESISTER_SESSION_LOG_H
#include <stddef.h>
#include <stdint.h>

enum { TS_SESSION_OFF, TS_SESSION_STARTING, TS_SESSION_RECORDING,
       TS_SESSION_FULL, TS_SESSION_ERROR, TS_SESSION_STOPPED };
typedef struct {
    int state, export_result; /* 0 idle, 1 pending, 2 saved, 3 failed */
    unsigned dropped_audio, dropped_events, export_serial;
    int export_copy_path; /* Explicit Save Session copies the result to clipboard. */
    uint64_t bytes;
    const char *path; /* Immutable until next start. */
    char export_path[2048]; /* Last completed export, copied under a control mutex. */
} TsSessionLogStatus;

/* Control thread, before audio starts / after all producers stop. NULL directory
   uses SDL's writable per-user Health directory. limit=0 selects 64 MiB. */
int ts_session_log_start(const char *directory, uint64_t limit);
void ts_session_log_stop(void);
int ts_session_log_enabled(void);
void ts_session_log_status(TsSessionLogStatus *status); /* UI/control thread only. */
const char *ts_session_log_state_name(int state);
/* Control/worker producers: one nonblocking try, bounded copy, no disk I/O.
   Text is JSON escaped by the writer. No sample content or file paths required. */
void ts_session_log_event(const char *type, const char *format, ...);
int ts_session_log_text(const char *type, const char *text); /* 1 queued, 0 unavailable/full */
void ts_session_log_control(uint64_t started, uint64_t hold, uint64_t wait,
                            uint64_t frequency, const char *site);
void ts_session_log_discontinuity(const char *reason);
int ts_session_log_export(void); /* Background snapshot; existing saved copy replaced. */
/* UI/control thread only. Save beside project in logs/, or in the recovery
   directory for an unsaved project. Requests queue; stop drains accepted saves.
   copy_path is true for explicit Save Session, false for ordinary project saves. */
int ts_session_log_export_project(const char *project_path, int copy_path);
/* Pure path calculation, no filesystem access; also used by LOG FOLDER. */
int ts_session_log_export_destination(const char *project_path, char *path, size_t size);
/* UI thread, outside audio exclusion: serialize bundle replacement with exports.
   The writer keeps recording while a project save owns this filesystem guard. */
void ts_session_log_project_save_begin(void);
void ts_session_log_project_save_end(void);
/* One serialized output callback producer. Fixed packet, lock-free SPSC queue;
   no clock calls, allocation, formatting, OS services, locks or retry loops. */
void ts_session_log_audio(uint64_t started, uint64_t elapsed,
                          uint32_t rate, uint32_t frames, uint32_t configuration);
#endif
