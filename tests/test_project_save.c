#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* Exercise the real public saver, injecting filesystem failures only at its
   rename boundary. No production fault switches or alternate save code. */
enum { FAULT_NONE, FAULT_PREPARE, FAULT_MOVE_SECOND, FAULT_PUBLISH,
       FAULT_ROLLBACK_ENTRY, FAULT_ROLLBACK_ROOT, FAULT_VALIDATION, FAULT_MOVE_AMBIGUOUS };
static int fault, user_moves;
#ifndef _WIN32
static int crash_after_move;
#endif
static const char *bundle = "Save Project";
static int fail_rename(const char *from, const char *to)
{
    if (fault == FAULT_PREPARE && !strcmp(from, bundle)) return 1;
    if (strstr(from, "Save Project.tapesister-backup/") &&
        strstr(to, "Save Project.tapesister-partial/")) {
        ++user_moves;
        if (fault == FAULT_MOVE_SECOND && user_moves == 2) return 1;
    }
    if ((fault == FAULT_PUBLISH || fault == FAULT_ROLLBACK_ENTRY || fault == FAULT_ROLLBACK_ROOT) &&
        !strcmp(from, "Save Project.tapesister-partial") && !strcmp(to, bundle)) return 1;
    if (fault == FAULT_ROLLBACK_ENTRY && strstr(from, "Save Project.tapesister-partial/") &&
        strstr(to, "Save Project.tapesister-backup/")) return 1;
    if (fault == FAULT_ROLLBACK_ROOT && !strcmp(from, "Save Project.tapesister-backup")) return 1;
    return 0;
}

static void corrupt_for_validation(const char *to)
{
    if (fault == FAULT_VALIDATION && strstr(to, ".tsr")) {
        FILE *file = fopen(to, "wb");
        if (file) { fputs("invalid staged project", file); fclose(file); }
    }
}
#ifdef _WIN32
static BOOL WINAPI test_move(const char *from, const char *to, DWORD flags)
{
    if (fail_rename(from, to)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    BOOL ok = MoveFileExA(from, to, flags);
    if (ok) {
        corrupt_for_validation(to);
        if (fault == FAULT_MOVE_AMBIGUOUS && strstr(from, "Save Project.tapesister-backup/")) {
            SetLastError(ERROR_WRITE_FAULT); return FALSE;
        }
    }
    return ok;
}
#define MoveFileExA test_move
#else
static int test_rename(const char *from, const char *to)
{
    if (fail_rename(from, to)) { errno = EACCES; return -1; }
    int result = rename(from, to);
    if (!result) {
        corrupt_for_validation(to);
        if (fault == FAULT_MOVE_AMBIGUOUS && strstr(from, "Save Project.tapesister-backup/")) {
            errno = EIO; return -1;
        }
        if (crash_after_move && strstr(from, "Save Project.tapesister-backup/")) _exit(42);
    }
    return result;
}
#define rename test_rename
#endif
#include "../src/ts_sample_pages.c"
#ifdef _WIN32
#undef MoveFileExA
#else
#undef rename
#endif

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)
static const char *project = "Save Project/Save Project.tsr";
static char error[1024];
static TsSamplePages pages;
static TsInstrument active, record;

static void put(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file) { CHECK(fwrite(text, 1, strlen(text), file) == strlen(text)); CHECK(!fclose(file)); }
}
static void contents(const char *path, const char *text)
{
    char data[256] = {0};
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file) { size_t n = fread(data, 1, sizeof(data), file); CHECK(n == strlen(text));
        CHECK(n == strlen(text) && !memcmp(data, text, n)); CHECK(!ferror(file)); fclose(file); }
}
static uint64_t hash_file(const char *path)
{
    uint64_t hash = 0;
    CHECK(project_file_hash(path, &hash));
    return hash;
}
static int save(void)
{
    error[0] = 0;
    user_moves = 0;
    return ts_sample_pages_save_project(&pages, &active, &record, NULL, project, error, sizeof(error));
}
static void mkdir_checked(const char *path) { CHECK(TS_PAGES_MKDIR(path) == 0); }
static void clean_case(void)
{
    fault = FAULT_NONE;
    CHECK(remove_tree(bundle));
    CHECK(remove_tree("Save Project.tapesister-partial"));
    CHECK(remove_tree("Save Project.tapesister-backup"));
    ts_mosaic_free(pages.mosaic);
    pages.mosaic = NULL;
    ts_sample_pages_free(&pages);
    ts_instrument_free(&active);
    ts_instrument_free(&record);
}
static void new_case(void)
{
    ts_instrument_init(&active);
    ts_instrument_init(&record);
    CHECK(ts_sample_pages_init(&pages, error, sizeof(error)));
    CHECK(ts_instrument_activate_silence(&active, 32, 48000, error, sizeof(error)));
    for (size_t i = 0; i < active.current.frames; ++i) active.current.data[i] = 0.25f;
    CHECK(save());
}
static void extras(void)
{
    put("Save Project/notes.txt", "personal notes\n");
    mkdir_checked("Save Project/recordings");
    mkdir_checked("Save Project/recordings/empty");
    put("Save Project/recordings/take.wav", "user recording, not a generated WAV\n");
    put("Save Project/.hidden", "hidden user file\n");
    put("Save Project/project-data/notes.txt", "nested project notes\n");
    put("Save Project/samples/page-001/custom.wav", "custom WAV\n");
    mkdir_checked("Save Project/logs");
    put("Save Project/logs/session.jsonl", "session history\n");
}
static void check_extras(void)
{
    contents("Save Project/notes.txt", "personal notes\n");
    contents("Save Project/recordings/take.wav", "user recording, not a generated WAV\n");
    CHECK(project_entry_kind("Save Project/recordings/empty") == PROJECT_ENTRY_DIRECTORY);
    contents("Save Project/.hidden", "hidden user file\n");
    contents("Save Project/project-data/notes.txt", "nested project notes\n");
    contents("Save Project/samples/page-001/custom.wav", "custom WAV\n");
    contents("Save Project/logs/session.jsonl", "session history\n");
}
static void check_load(void)
{
    TsSamplePages loaded;
    TsInstrument sample, rec;
    ts_instrument_init(&sample); ts_instrument_init(&rec);
    CHECK(ts_sample_pages_init(&loaded, error, sizeof(error)));
    CHECK(ts_sample_pages_load_project(&loaded, &sample, &rec, project, error, sizeof(error)));
    CHECK(loaded.page_count == pages.page_count);
    CHECK(ts_sample_hash(&sample.current) == ts_sample_hash(&active.current));
    ts_sample_pages_free(&loaded); ts_instrument_free(&sample); ts_instrument_free(&rec);
}
static void no_recovery(void)
{
    CHECK(project_entry_kind("Save Project.tapesister-backup") == PROJECT_ENTRY_MISSING);
    CHECK(project_entry_kind("Save Project.tapesister-partial") == PROJECT_ENTRY_MISSING);
}

static void test_preservation(void)
{
    new_case(); extras();
    FILE *open_log = NULL;
#ifndef _WIN32
    open_log = fopen("Save Project/logs/session.jsonl", "ab");
    CHECK(open_log != NULL);
#endif
#ifndef _WIN32
    CHECK(chmod("Save Project/notes.txt", 0400) == 0);
#endif
    CHECK(save());
#ifdef _WIN32
    open_log = fopen("Save Project/logs/session.jsonl", "ab");
    CHECK(open_log != NULL);
#endif
    if (open_log) { fputs("still open\n", open_log); CHECK(!fclose(open_log)); }
    contents("Save Project/logs/session.jsonl", "session history\nstill open\n");
    /* Subsequent saves must not adopt previously carried files as generated. */
    CHECK(save()); CHECK(save());
    contents("Save Project/notes.txt", "personal notes\n");
    contents("Save Project/samples/page-001/custom.wav", "custom WAV\n");
    CHECK(project_entry_kind("Save Project/recordings/empty") == PROJECT_ENTRY_DIRECTORY);
#ifndef _WIN32
    struct stat info;
    CHECK(!stat("Save Project/notes.txt", &info) && (info.st_mode & 0777) == 0400);
#endif
    check_load(); no_recovery(); clean_case();
}

static void test_conflicts(void)
{
    new_case(); extras();
    /* A new page would claim a user path which was never owned before. */
    put("Save Project/project-data/page-002.tsr", "user-owned collision\n");
    uint64_t before = hash_file(project);
    CHECK(ts_sample_pages_append(&pages, error, sizeof(error)));
    CHECK(!save()); CHECK(strstr(error, "page-002.tsr") != NULL);
    CHECK(hash_file(project) == before); check_extras();
    contents("Save Project/project-data/page-002.tsr", "user-owned collision\n"); no_recovery();
    CHECK(!remove("Save Project/project-data/page-002.tsr")); CHECK(save());
    check_load(); clean_case();

    new_case();
    mkdir_checked("Save Project/project-data/page-002.tsr");
    put("Save Project/project-data/page-002.tsr/keep.txt", "inside user folder\n");
    CHECK(ts_sample_pages_append(&pages, error, sizeof(error)));
    CHECK(!save()); contents("Save Project/project-data/page-002.tsr/keep.txt", "inside user folder\n");
    no_recovery(); clean_case();

    new_case();
    CHECK(!remove("Save Project/samples/page-001/01_SILENCE.wav"));
    CHECK(TS_PAGES_RMDIR("Save Project/samples/page-001") == 0);
    put("Save Project/samples/page-001", "user file blocks required directory\n");
    CHECK(!save()); contents("Save Project/samples/page-001", "user file blocks required directory\n");
    no_recovery(); clean_case();

    new_case();
    put("Save Project/Save Project.tsr", "externally replaced project\n");
    CHECK(!save()); contents(project, "externally replaced project\n"); no_recovery(); clean_case();
}

static void test_modified_and_stale_files(void)
{
    new_case();
    put("Save Project/samples/page-001/01_SILENCE.wav", "externally edited audio\n");
    CHECK(!save()); contents("Save Project/samples/page-001/01_SILENCE.wav", "externally edited audio\n");
    /* If the next project no longer writes this name, retain the edited file. */
    CHECK(ts_instrument_bank_clear_all(&active, error, sizeof(error)));
    CHECK(save()); contents("Save Project/samples/page-001/01_SILENCE.wav", "externally edited audio\n");
    CHECK(save()); contents("Save Project/samples/page-001/01_SILENCE.wav", "externally edited audio\n");
    clean_case();

    new_case();
    CHECK(ts_sample_pages_append_and_switch(&pages, &active, NULL, error, sizeof(error))); CHECK(save());
    put("Save Project/samples/page-002/keep.txt", "notes in removed page\n");
    CHECK(ts_sample_pages_remove_last_and_switch(&pages, &active, 0, error, sizeof(error)));
    CHECK(save());
    CHECK(project_entry_kind("Save Project/project-data/page-002.tsr") == PROJECT_ENTRY_MISSING);
    contents("Save Project/samples/page-002/keep.txt", "notes in removed page\n");
    check_load(); clean_case();
}

static void test_failures(void)
{
    const int faults[] = {FAULT_PREPARE, FAULT_MOVE_SECOND, FAULT_PUBLISH, FAULT_VALIDATION};
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        new_case(); extras();
        uint64_t before = hash_file(project), manifest = hash_file("Save Project/manifest.txt");
        fault = faults[i]; CHECK(!save()); CHECK(error[0]); fault = FAULT_NONE;
        CHECK(hash_file(project) == before); CHECK(hash_file("Save Project/manifest.txt") == manifest);
        check_extras(); check_load(); no_recovery();
        CHECK(save()); check_extras(); clean_case();
    }
}

static void test_failed_rollback(void)
{
    for (int f = FAULT_ROLLBACK_ENTRY; f <= FAULT_ROLLBACK_ROOT; ++f) {
        new_case(); extras();
        uint64_t before = hash_file(project);
        fault = f; CHECK(!save()); fault = FAULT_NONE;
        CHECK(strstr(error, "recovery required"));
        CHECK(project_entry_kind("Save Project.tapesister-backup") == PROJECT_ENTRY_DIRECTORY);
        CHECK(project_entry_kind("Save Project.tapesister-partial") == PROJECT_ENTRY_DIRECTORY);
        CHECK(hash_file("Save Project.tapesister-backup/Save Project.tsr") == before);
        const char *notes = f == FAULT_ROLLBACK_ENTRY ?
            "Save Project.tapesister-partial/notes.txt" : "Save Project.tapesister-backup/notes.txt";
        contents(notes, "personal notes\n");
        /* Restart-equivalent retry must not erase staging with the only copy. */
        CHECK(!save()); CHECK(strstr(error, "backup")); contents(notes, "personal notes\n");
        if (f == FAULT_ROLLBACK_ENTRY) {
            /* Manual recovery may relocate the backup before the next retry.
               The protected stage must still never be auto-cleaned. */
            CHECK(!rename("Save Project.tapesister-backup", "relocated-backup"));
            CHECK(!save()); CHECK(strstr(error, "staging")); contents(notes, "personal notes\n");
            CHECK(remove_tree("relocated-backup"));
        }
        clean_case();
    }
}

static void test_ambiguous_move(void)
{
    new_case();
    put("Save Project/notes.txt", "sole user copy\n");
    fault = FAULT_MOVE_AMBIGUOUS; CHECK(!save()); fault = FAULT_NONE;
    CHECK(strstr(error, "recovery required"));
    contents("Save Project.tapesister-partial/notes.txt", "sole user copy\n");
    CHECK(project_entry_kind("Save Project.tapesister-backup") == PROJECT_ENTRY_DIRECTORY);
    CHECK(!save()); contents("Save Project.tapesister-partial/notes.txt", "sole user copy\n");
    clean_case();
}

static void test_recovery_guards(void)
{
    new_case();
    mkdir_checked("Save Project.tapesister-partial");
    put("Save Project.tapesister-partial/keep.txt", "unowned stage\n");
    CHECK(!save()); contents("Save Project.tapesister-partial/keep.txt", "unowned stage\n");
    put("Save Project.tapesister-partial/.tapesister-partial", "not a TapeSister marker\n");
    CHECK(!save()); contents("Save Project.tapesister-partial/keep.txt", "unowned stage\n");
    CHECK(remove_tree("Save Project.tapesister-partial"));
    mkdir_checked("Save Project.tapesister-partial");
    put("Save Project.tapesister-partial/.tapesister-partial", "TapeSister partial project\n");
    CHECK(save()); no_recovery();
    put("Save Project.tapesister-backup", "backup name belongs to user\n");
    CHECK(!save()); contents("Save Project.tapesister-backup", "backup name belongs to user\n");
    CHECK(!remove("Save Project.tapesister-backup"));
    put("Save Project/.tapesister-partial", "user file with reserved name\n");
    CHECK(!save()); contents("Save Project/.tapesister-partial", "user file with reserved name\n");
    clean_case();
}

static void test_links(void)
{
    new_case();
    mkdir_checked("outside"); put("outside/keep.txt", "outside the project\n");
#ifdef _WIN32
    int available = CreateSymbolicLinkA("Save Project/link", "..\\outside", SYMBOLIC_LINK_FLAG_DIRECTORY | 2) != 0;
#else
    int available = symlink("../outside", "Save Project/link") == 0;
    CHECK(available);
#endif
    if (available) {
        CHECK(save()); CHECK(project_entry_kind("Save Project/link") == PROJECT_ENTRY_OTHER);
        contents("Save Project/link/keep.txt", "outside the project\n");
        contents("outside/keep.txt", "outside the project\n");
    } else puts("SKIP Windows symlink creation: privilege unavailable");
#ifndef _WIN32
    CHECK(!symlink("missing", "Save Project/dangling")); CHECK(save());
    CHECK(project_entry_kind("Save Project/dangling") == PROJECT_ENTRY_OTHER);
    CHECK(!symlink("missing", "Save Project.tapesister-backup"));
    CHECK(!save()); CHECK(!unlink("Save Project.tapesister-backup"));
    CHECK(!symlink("outside", "Save Project.tapesister-partial"));
    CHECK(!save()); contents("outside/keep.txt", "outside the project\n");
    CHECK(!unlink("Save Project.tapesister-partial"));
    /* A symlink replacing a managed output is user material, not an owner. */
    CHECK(!remove("Save Project/samples/page-001/01_SILENCE.wav"));
    CHECK(!symlink("../../../outside/keep.txt", "Save Project/samples/page-001/01_SILENCE.wav"));
    CHECK(!save()); contents("outside/keep.txt", "outside the project\n");
#endif
    clean_case(); CHECK(remove_tree("outside"));
}

static void strip_inventory(void)
{
    FILE *input = fopen("Save Project/manifest.txt", "rb");
    FILE *output = fopen("legacy-manifest.txt", "wb");
    char line[4096];
    CHECK(input && output);
    if (input && output) while (fgets(line, sizeof(line), input)) {
        if (!strncmp(line, "managed_files=", 14)) break;
        fputs(line, output);
    }
    if (input) fclose(input);
    if (output) fclose(output);
    CHECK(!remove("Save Project/manifest.txt"));
    CHECK(!rename("legacy-manifest.txt", "Save Project/manifest.txt"));
}

static void test_legacy_migration(void)
{
    new_case();
    pages.mosaic = ts_mosaic_create(); CHECK(pages.mosaic != NULL);
    TsMasterEqControls eq; ts_master_eq_default(&eq);
    TsMosaicSource *audio = ts_mosaic_source_eq(pages.mosaic, &active.current,
        &active.current, &eq, error, sizeof(error)); CHECK(audio != NULL);
    CHECK(ts_mosaic_add(pages.mosaic, audio, 0, 0) != NULL);
    CHECK(ts_instrument_sync_selected(&active, error, sizeof(error)));
    TsTileId tile = active.bank[active.selected_slot].tile_id;
    uint8_t alias;
    CHECK(ts_sister_tracker_bind_tile(&pages.tracker, tile, &alias, error, sizeof(error)));
    TsPatternId pattern_id;
    CHECK(ts_sister_tracker_add_pattern(&pages.tracker, 16, &pattern_id, error, sizeof(error)));
    CHECK(ts_sister_tracker_insert_order(&pages.tracker, 0, pattern_id, error, sizeof(error)));
    TsTrackerPattern *pattern = ts_sister_tracker_pattern(&pages.tracker, pattern_id); CHECK(pattern != NULL);
    pattern->cells[0][0] = (TsTrackerCell){.tile_id=tile, .note_kind=TS_TRACKER_NOTE_PITCH, .note=60};
    TsMosaicSource *score = ts_pattern_source(pages.mosaic, &pages, &active, pattern->id, error, sizeof(error));
    CHECK(score && score->pattern && score->pattern->asset_count == 1);
    CHECK(ts_mosaic_add(pages.mosaic, score, 1, 100) != NULL);
    CHECK(save());
    strip_inventory(); extras();
    /* Exercise exact legacy source, EQ-dry and pattern asset ownership. */
    CHECK(save()); check_extras(); check_load();
    TsMosaic *loaded = ts_mosaic_create(); CHECK(loaded != NULL);
    CHECK(ts_mosaic_load(loaded, "Save Project/project-data", error, sizeof(error)));
    CHECK(loaded->events[0].source->eq_dry.data != NULL);
    CHECK(loaded->events[1].source->pattern && loaded->events[1].source->pattern->asset_count == 1);
    ts_mosaic_free(loaded);
    strip_inventory();
    TsMosaic *old = pages.mosaic;
    pages.mosaic = NULL;
    CHECK(save()); check_extras();
    CHECK(project_entry_kind("Save Project/project-data/mosaic-000.wav") == PROJECT_ENTRY_MISSING);
    CHECK(project_entry_kind("Save Project/project-data/mosaic-000-dry.wav") == PROJECT_ENTRY_MISSING);
    CHECK(project_entry_kind("Save Project/project-data/pattern-001-0000.wav") == PROJECT_ENTRY_MISSING);
    CHECK(project_entry_kind("Save Project/project-data/pattern-001.score") == PROJECT_ENTRY_MISSING);
    ts_mosaic_free(old); clean_case();
}

static void test_interruption(void)
{
#ifndef _WIN32
    new_case(); extras();
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) { crash_after_move = 1; (void)save(); _exit(1); }
    int status = 0;
    if (pid > 0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
        CHECK(!save()); CHECK(strstr(error, "backup"));
        const char *relative[] = {"notes.txt", "recordings/take.wav", ".hidden", "project-data/notes.txt",
                                  "samples/page-001/custom.wav", "logs/session.jsonl"};
        const char *text[] = {"personal notes\n", "user recording, not a generated WAV\n", "hidden user file\n",
                             "nested project notes\n", "custom WAV\n", "session history\n"};
        for (int i = 0; i < 6; ++i) {
            char path[256];
            snprintf(path, sizeof(path), "Save Project.tapesister-backup/%s", relative[i]);
            if (project_entry_kind(path) == PROJECT_ENTRY_MISSING)
                snprintf(path, sizeof(path), "Save Project.tapesister-partial/%s", relative[i]);
            contents(path, text[i]);
        }
    }
    clean_case();
#endif
}

static void test_windows_locked_file(void)
{
#ifdef _WIN32
    new_case(); extras();
    uint64_t before = hash_file(project);
    HANDLE lock = CreateFileA("Save Project/notes.txt", GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CHECK(!save());
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    CHECK(hash_file(project) == before); check_extras(); no_recovery();
    CHECK(save()); clean_case();
#endif
}

static void test_windows_unicode_entry(void)
{
#ifdef _WIN32
    new_case();
    const wchar_t *path = L"Save Project/\x0416\x0438\x0432\x0430.wav";
    FILE *file = _wfopen(path, L"wb"); CHECK(file != NULL);
    if (file) { fputs("Unicode user material\n", file); CHECK(!fclose(file)); }
    char name[128], text[128] = {0};
    int supported = project_windows_name(L"\x0416\x0438\x0432\x0430.wav", name, sizeof(name));
    uint64_t before = hash_file(project);
    CHECK(save() == supported);
    if (!supported) { CHECK(strstr(error, "code page")); CHECK(hash_file(project) == before); }
    file = _wfopen(path, L"rb"); CHECK(file != NULL);
    if (file) { CHECK(fgets(text, sizeof(text), file) != NULL); CHECK(!strcmp(text, "Unicode user material\n")); fclose(file); }
    no_recovery(); CHECK(DeleteFileW(path));
    CHECK(save()); clean_case();
#endif
}

static void test_corrupt_inventory(void)
{
    new_case(); extras();
    FILE *file = fopen("Save Project/manifest.txt", "ab"); CHECK(file != NULL);
    if (file) { fputs("managed=0000000000000000,../outside\n", file); fclose(file); }
    uint64_t before = hash_file(project);
    CHECK(!save()); CHECK(hash_file(project) == before); check_extras(); no_recovery(); clean_case();
}

int main(void)
{
    char work[128];
#ifdef _WIN32
    snprintf(work, sizeof(work), "project-save-test-%lu-%lu", (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    mkdir_checked(work); CHECK(_chdir(work) == 0);
#else
    snprintf(work, sizeof(work), "project-save-test-XXXXXX");
    CHECK(mkdtemp(work) != NULL); CHECK(chdir(work) == 0);
#endif
    test_preservation(); test_conflicts(); test_modified_and_stale_files();
    test_failures(); test_failed_rollback(); test_recovery_guards(); test_links(); test_corrupt_inventory();
    test_legacy_migration(); test_interruption(); test_windows_locked_file(); test_ambiguous_move(); test_windows_unicode_entry();
#ifdef _WIN32
    CHECK(_chdir("..") == 0);
#else
    CHECK(chdir("..") == 0);
#endif
    CHECK(TS_PAGES_RMDIR(work) == 0);
    if (failures) { fprintf(stderr, "%d project-save failures\n", failures); return 1; }
    puts("project save preservation, conflicts, validation, rollback and recovery passed");
    return 0;
}
