/* Exact file ownership, not directory ownership. This is deliberately part of
   the save transaction: user entries are renamed only after validation, and
   every rename can be undone before the old bundle is restored. */
typedef struct {
    char *path;
    uint64_t hash;
    int fingerprint;
} ProjectFile;

typedef struct {
    ProjectFile *files;
    size_t count, capacity;
} ProjectFiles;

enum { PROJECT_ENTRY_ERROR = -1, PROJECT_ENTRY_MISSING, PROJECT_ENTRY_FILE,
       PROJECT_ENTRY_DIRECTORY, PROJECT_ENTRY_OTHER };

static int project_entry_kind(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD code = GetLastError();
        return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ?
            PROJECT_ENTRY_MISSING : PROJECT_ENTRY_ERROR;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return PROJECT_ENTRY_OTHER;
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? PROJECT_ENTRY_DIRECTORY : PROJECT_ENTRY_FILE;
#else
    struct stat info;
    if (lstat(path, &info) != 0)
        return errno == ENOENT ? PROJECT_ENTRY_MISSING : PROJECT_ENTRY_ERROR;
    return S_ISDIR(info.st_mode) ? PROJECT_ENTRY_DIRECTORY :
           S_ISREG(info.st_mode) ? PROJECT_ENTRY_FILE : PROJECT_ENTRY_OTHER;
#endif
}

static int project_file_error(char *error, size_t size, const char *message, const char *path)
{
    if (error && size) snprintf(error, size, "%s: %s", message, path);
    return 0;
}

static int project_join(char *out, size_t size, const char *base, const char *relative)
{
    int n = snprintf(out, size, "%s/%s", base, relative);
    return n >= 0 && (size_t)n < size;
}

static void project_files_free(ProjectFiles *list)
{
    for (size_t i = 0; i < list->count; ++i) free(list->files[i].path);
    free(list->files);
    memset(list, 0, sizeof(*list));
}

static ProjectFile *project_files_find(ProjectFiles *list, const char *path)
{
    for (size_t i = 0; i < list->count; ++i)
        if (path_component_equal(list->files[i].path, path)) return &list->files[i];
    return NULL;
}

/* Only serialized relative paths enter the ownership list. Never let a corrupt
   manifest turn a parent, alternate stream, or absolute path into an owner. */
static int project_relative_valid(const char *path)
{
    const char *component = path;
    if (!path[0] || path[0] == '/') return 0;
    for (const unsigned char *p = (const unsigned char *)path;; ++p) {
        if (*p && (*p < 32 || *p == 127 || *p == '\\' || *p == ':')) return 0;
        if (!*p || *p == '/') {
            size_t n = (const char *)p - component;
#ifdef _WIN32
            /* Win32 strips trailing dots/spaces. Never inspect one entry and
               then rename/delete a different entry through that alias. */
            if (n && (component[n - 1] == '.' || component[n - 1] == ' ')) return 0;
#endif
            if (!n || (n == 1 && component[0] == '.') ||
                (n == 2 && component[0] == '.' && component[1] == '.')) return 0;
            if (!*p) return 1;
            component = (const char *)p + 1;
        }
    }
}

static int project_files_add(ProjectFiles *list, const char *path,
                             uint64_t hash, int fingerprint)
{
    if (!project_relative_valid(path)) return 0;
    if (project_files_find(list, path)) return 1;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 32;
        ProjectFile *files = realloc(list->files, capacity * sizeof(*files));
        if (!files) return 0;
        list->files = files;
        list->capacity = capacity;
    }
    char *copy = malloc(strlen(path) + 1);
    if (!copy) return 0;
    strcpy(copy, path);
    list->files[list->count++] = (ProjectFile){copy, hash, fingerprint};
    return 1;
}

static int project_file_hash(const char *path, uint64_t *hash)
{
    unsigned char buffer[16384];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint64_t value = UINT64_C(14695981039346656037);
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0)
        for (size_t i = 0; i < n; ++i)
            value = (value ^ buffer[i]) * UINT64_C(1099511628211);
    int ok = !ferror(file);
    if (fclose(file)) ok = 0;
    *hash = value;
    return ok;
}

typedef int (*ProjectVisitor)(const char *, void *, char *, size_t);

/* Enumerate without following links or reparse points. Enumeration failures
   must abort the save; a partial listing is never permission to discard data. */
static int project_children(const char *base, const char *relative,
                            ProjectVisitor visit, void *context,
                            char *error, size_t size)
{
    char directory[TS_PROJECT_PATH_MAX], child[TS_PROJECT_PATH_MAX];
    if (!project_join(directory, sizeof(directory), base, relative))
        return project_file_error(error, size, "Project path too long", relative);
    int ok = 1;
#ifdef _WIN32
    char pattern[TS_PROJECT_PATH_MAX];
    if (!project_join(pattern, sizeof(pattern), directory, "*")) return 0;
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
        return project_file_error(error, size, "Cannot inspect project folder", relative);
    }
    do {
        if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..")) continue;
        int n = snprintf(child, sizeof(child), "%s%s%s", relative,
                         relative[0] ? "/" : "", entry.cFileName);
        if (n < 0 || (size_t)n >= sizeof(child) || !visit(child, context, error, size)) { ok = 0; break; }
    } while (FindNextFileA(search, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
    FindClose(search);
#else
    DIR *dir = opendir(directory);
    if (!dir) return project_file_error(error, size, "Cannot inspect project folder", relative);
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) { if (errno) ok = 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int n = snprintf(child, sizeof(child), "%s%s%s", relative,
                         relative[0] ? "/" : "", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child) || !visit(child, context, error, size)) { ok = 0; break; }
    }
    if (closedir(dir)) ok = 0;
#endif
    if (!ok && error && size && !error[0])
        project_file_error(error, size, "Cannot inspect project folder", relative);
    return ok;
}

typedef struct { const char *base; ProjectFiles *files; } ProjectInventory;

static int project_inventory_visit(const char *relative, void *context, char *error, size_t size)
{
    ProjectInventory *inventory = context;
    char path[TS_PROJECT_PATH_MAX];
    uint64_t hash;
    if (!project_join(path, sizeof(path), inventory->base, relative)) return 0;
    int kind = project_entry_kind(path);
    if (kind == PROJECT_ENTRY_DIRECTORY)
        return project_children(inventory->base, relative, project_inventory_visit, context, error, size);
    if (!strcmp(relative, "manifest.txt") || !strcmp(relative, ".tapesister-partial")) return 1;
    if (kind != PROJECT_ENTRY_FILE || !project_file_hash(path, &hash) ||
        !project_files_add(inventory->files, relative, hash, 1))
        return project_file_error(error, size, "Cannot inventory generated file", relative);
    return 1;
}

static int project_write_inventory(FILE *manifest, const char *staging,
                                    char *error, size_t size)
{
    ProjectFiles files = {0};
    ProjectInventory inventory = {staging, &files};
    int ok = project_children(staging, "", project_inventory_visit, &inventory, error, size);
    if (ok) ok = fprintf(manifest, "managed_files=1\n") > 0;
    for (size_t i = 0; ok && i < files.count; ++i)
        ok = fprintf(manifest, "managed=%016llx,%s\n",
            (unsigned long long)files.files[i].hash, files.files[i].path) > 0;
    if (ok) ok = fprintf(manifest, "managed_end=%zu\n", files.count) > 0;
    project_files_free(&files);
    return ok;
}

/* Older bundles have no inventory. Infer only exact files declared by their
   manifest and Mosaic score; never claim a directory or a filename wildcard. */
static int project_legacy_mosaic(ProjectFiles *files, const char *directory)
{
    char path[TS_PROJECT_PATH_MAX], line[2048], relative[128];
    if (!project_join(path, sizeof(path), directory, "project-data/mosaic.tsm")) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int pattern = 0, source = -1, ok = 1, format;
    if (!fgets(line, sizeof(line), file) ||
        sscanf(line, "TAPESISTER_MOSAIC %d", &format) != 1 || format < 1 || format > 14) ok = 0;
    while (ok && fgets(line, sizeof(line), file)) {
        unsigned long long id;
        int index, editable;
        double start, duration, x, width;
        if (sscanf(line, "PATTERN %d", &pattern) == 1) continue;
        if (sscanf(line, "%llu %d %lf %lf %lf %lf", &id, &index,
                   &start, &duration, &x, &width) == 6) {
            if (!id || index < 0 || index >= TS_MOSAIC_EVENTS) { ok = 0; break; }
            source = index;
            snprintf(relative, sizeof(relative), "project-data/mosaic-%03d.wav", source);
            ok = project_files_add(files, relative, 0, 0);
            if (pattern) {
                const char *suffix[] = {"score", "cells", "assets"};
                for (int i = 0; ok && i < 3; ++i) {
                    snprintf(relative, sizeof(relative), "project-data/pattern-%03d.%s", source, suffix[i]);
                    ok = project_files_add(files, relative, 0, 0);
                }
                if (!ok || !project_join(path, sizeof(path), directory, relative)) { ok = 0; break; }
                FILE *assets = fopen(path, "rb");
                int count = -1, version = 0;
                unsigned pid;
                int root, linear;
                double duration;
                if (assets) {
                    if (fscanf(assets, "TSPATTERN %d %u %d %d %lf %d",
                               &version, &pid, &root, &linear, &duration, &count) != 6) count = -1;
                    fclose(assets);
                }
                if ((version != 1 && version != 2) || count < 0 || count > 2048) { ok = 0; break; }
                for (int i = 0; ok && i < count; ++i) {
                    snprintf(relative, sizeof(relative), "project-data/pattern-%03d-%04d.wav", source, i);
                    ok = project_files_add(files, relative, 0, 0);
                }
            }
            pattern = 0;
        } else if (sscanf(line, "EQ %d", &editable) == 1 && editable && source >= 0) {
            snprintf(relative, sizeof(relative), "project-data/mosaic-%03d-dry.wav", source);
            ok = project_files_add(files, relative, 0, 0);
        }
    }
    if (ferror(file)) ok = 0;
    fclose(file);
    return ok;
}

static int project_read_ownership(const char *directory, const char *project,
                                  ProjectFiles *files, char *error, size_t size)
{
    char path[TS_PROJECT_PATH_MAX], line[TS_PROJECT_PATH_MAX + 128], relative[128];
    if (!project_join(path, sizeof(path), directory, "manifest.txt")) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return project_file_error(error, size, "Cannot read ownership manifest", path);
    ProjectFiles legacy = {0};
    size_t pages = 0, end = 0, managed_count = 0;
    int managed = 0, ended = 0, mosaic = 0, ok = 1;
    while (ok && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "managed_files=", 14)) {
            if (managed || strcmp(line, "managed_files=1")) { ok = 0; break; }
            managed = 1;
        } else if (!strncmp(line, "managed=", 8)) {
            unsigned long long hash;
            int offset = 0;
            if (!managed || ended || sscanf(line, "managed=%16llx,%n", &hash, &offset) != 1 ||
                !offset || project_files_find(files, line + offset) ||
                !project_files_add(files, line + offset, (uint64_t)hash, 1)) { ok = 0; break; }
            ++managed_count;
        } else if (!strncmp(line, "managed_end=", 12)) {
            char tail;
            if (!managed || ended || sscanf(line, "managed_end=%zu%c", &end, &tail) != 1) { ok = 0; break; }
            ended = 1;
        } else if (sscanf(line, "page_count=%zu", &pages) == 1) {
            if (!pages || pages > TS_PROJECT_PAGE_LIMIT) { ok = 0; break; }
            for (size_t i = 1; ok && i < pages; ++i) {
                snprintf(relative, sizeof(relative), "project-data/page-%03zu.tsr", i + 1);
                ok = project_files_add(&legacy, relative, 0, 0);
            }
        } else if (!strcmp(line, "record_bank=1")) {
            ok = project_files_add(&legacy, "project-data/record-bank.tsr", 0, 0);
        } else if (!strcmp(line, "sister_state=1")) {
            ok = project_files_add(&legacy, "sister-state.ini", 0, 0);
        } else if (!strcmp(line, "sister_tracker=1")) {
            ok = project_files_add(&legacy, "project-data/sister-tracker.tst", 0, 0);
        } else if (!strcmp(line, "mosaic=1")) mosaic = 1;
        else if (!strncmp(line, "sample=", 7)) {
            char *first = strchr(line, ','), *second = first ? strchr(first + 1, ',') : NULL;
            char *third = second ? strchr(second + 1, ',') : NULL;
            if (!third) { ok = 0; break; }
            *third = '\0';
            if (strncmp(second + 1, "samples/", 8)) { ok = 0; break; }
            ok = project_files_add(&legacy, second + 1, 0, 0);
        }
    }
    if (ferror(file)) ok = 0;
    fclose(file);
    if (managed && (!ended || managed_count != end)) ok = 0;
    if (ok && !managed) {
        if (!pages) ok = 0;
        if (ok && mosaic) ok = project_files_add(&legacy, "project-data/mosaic.tsm", 0, 0) &&
                                  project_legacy_mosaic(&legacy, directory);
        if (ok) { *files = legacy; memset(&legacy, 0, sizeof(legacy)); }
        if (ok) ok = project_files_add(files, project, 0, 0);
    }
    if (ok) ok = project_files_add(files, "manifest.txt", 0, 0);
    project_files_free(&legacy);
    if (!ok) return project_file_error(error, size, "Cannot establish project file ownership", "manifest.txt");
    return 1;
}

typedef struct {
    const char *old, *staging;
    ProjectFiles *owned, *moves;
} ProjectPreservation;

static int project_preserve_visit(const char *relative, void *context, char *error, size_t size)
{
    ProjectPreservation *plan = context;
    if (!project_relative_valid(relative))
        return project_file_error(error, size, "Cannot safely address user entry", relative);
    char old[TS_PROJECT_PATH_MAX], staged[TS_PROJECT_PATH_MAX];
    if (!project_join(old, sizeof(old), plan->old, relative) ||
        !project_join(staged, sizeof(staged), plan->staging, relative))
        return project_file_error(error, size, "Project path too long", relative);
    int kind = project_entry_kind(old), target = project_entry_kind(staged);
    if (kind <= PROJECT_ENTRY_MISSING || target < 0)
        return project_file_error(error, size, "Cannot inspect project entry", relative);
    ProjectFile *owned = project_files_find(plan->owned, relative);
    if (kind == PROJECT_ENTRY_FILE && owned) {
        uint64_t hash;
        if (!owned->fingerprint) return 1;
        if (!project_file_hash(old, &hash))
            return project_file_error(error, size, "Cannot check generated file", relative);
        if (hash == owned->hash) return 1;
        /* An externally edited generated file becomes user material. */
    }
    if (kind == PROJECT_ENTRY_DIRECTORY) {
        if (target != PROJECT_ENTRY_MISSING && target != PROJECT_ENTRY_DIRECTORY)
            return project_file_error(error, size, "Save conflicts with user folder; move it before saving", relative);
        int has_owned = 0;
        size_t n = strlen(relative);
        for (size_t i = 0; i < plan->owned->count; ++i) {
            const char *p = plan->owned->files[i].path;
            if (strlen(p) > n && p[n] == '/' &&
                !strncmp(p, relative, n)) { has_owned = 1; break; }
        }
        if (target == PROJECT_ENTRY_MISSING && !has_owned)
            return project_files_add(plan->moves, relative, 0, 0);
        if (target == PROJECT_ENTRY_MISSING &&
            !make_directory(staged, "preserved project folder", error, size)) return 0;
        return project_children(plan->old, relative, project_preserve_visit, plan, error, size);
    }
    if (target != PROJECT_ENTRY_MISSING)
        return project_file_error(error, size, "Save conflicts with user file; move it before saving", relative);
    if (!project_files_add(plan->moves, relative, 0, 0))
        return project_file_error(error, size, "Cannot plan user file preservation", relative);
    return 1;
}

static int project_move_user(const char *source, const char *target, const char *relative)
{
    char from[TS_PROJECT_PATH_MAX], to[TS_PROJECT_PATH_MAX];
    return project_join(from, sizeof(from), source, relative) &&
           project_join(to, sizeof(to), target, relative) &&
           project_entry_kind(to) == PROJECT_ENTRY_MISSING && rename_directory(from, to);
}
