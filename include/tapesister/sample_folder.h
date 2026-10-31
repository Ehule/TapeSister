#ifndef TAPESISTER_SAMPLE_FOLDER_H
#define TAPESISTER_SAMPLE_FOLDER_H

#include "tapesister/audio_import.h"
#include "tapesister/sample_pages.h"

typedef struct TsSampleFolderImport TsSampleFolderImport;
typedef struct {
    size_t imported, skipped, failed;
    size_t first_page, last_page; /* One-based destination page numbers. */
} TsSampleFolderSummary;

/* Snapshot empty, unlocked destinations on the control thread. Decode may run
   on a worker: it never reads or changes the live collection. Only ordinary
   WAV/FLAC/MP3/OGG files directly in the folder are considered, in filename order.
   Unsupported entries and unreadable audio are counted and skipped. */
TsSampleFolderImport *ts_sample_folder_prepare(const TsSamplePages *,
    const TsInstrument *active, char *error, size_t error_size);
int ts_sample_folder_decode(TsSampleFolderImport *, const char *directory,
    TsAudioImportCancelCheck, void *, char *error, size_t error_size);
/* Publish once under the audio lock; no allocation, file I/O or destruction.
   Revalidates the collection and every destination before changing anything.
   Dispose after unlocking, including on cancellation or failure. */
int ts_sample_folder_publish(TsSampleFolderImport *, TsSamplePages *,
    TsInstrument *active, char *error, size_t error_size);
TsSampleFolderSummary ts_sample_folder_summary(const TsSampleFolderImport *);
void ts_sample_folder_dispose(TsSampleFolderImport *);

#endif
