#define _XOPEN_SOURCE 700

#include "tapesister/cdp_adapter.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define TS_ACCESS _access
#define TS_EXE_SUFFIX ".exe"
#define TS_PATH_SEPARATOR '\\'
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define TS_ACCESS access
#define TS_EXE_SUFFIX ""
#define TS_PATH_SEPARATOR '/'
#endif

typedef struct {
    uint16_t format;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    uint32_t sample_rate;
    uint32_t data_size;
    long data_offset;
    size_t frames;
    float raw_peak;
    int finite;
} TsCdpWavInfo;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static int path_join(char *output, size_t output_size,
                     const char *left, const char *right)
{
    size_t length;
    int written;
    if (output == NULL || output_size == 0u || left == NULL || right == NULL ||
        right[0] == '\0') return 0;
    length = strlen(left);
    written = snprintf(output, output_size, "%s%s%s", left,
                       length > 0u && left[length - 1u] != '/' &&
                       left[length - 1u] != '\\' ?
                       (TS_PATH_SEPARATOR == '/' ? "/" : "\\") : "", right);
    return written >= 0 && (size_t)written < output_size;
}

static int file_exists(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat state;
    return stat(path, &state) == 0 && S_ISREG(state.st_mode) && access(path, X_OK) == 0;
#endif
}

static int regular_output_file(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
#else
    struct stat state;
    return lstat(path, &state) == 0 && S_ISREG(state.st_mode);
#endif
}

static int directory_exists(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat state;
    return stat(path, &state) == 0 && S_ISDIR(state.st_mode);
#endif
}

static int make_directory(const char *path)
{
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0700) == 0 || errno == EEXIST;
#endif
}

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void pause_10ms(void)
{
#ifdef _WIN32
    Sleep(10);
#else
    struct timespec delay = {0, 10000000L};
    (void)nanosleep(&delay, NULL);
#endif
}

void ts_cdp_runtime_init(TsCdpRuntime *runtime)
{
    if (runtime != NULL) memset(runtime, 0, sizeof(*runtime));
}

static int runtime_from_directory(TsCdpRuntime *runtime, const char *directory,
                                  char *error, size_t error_size)
{
    char canonical[TS_CDP_PATH_MAX];
    char pvoc[TS_CDP_PATH_MAX];
    char glisten[TS_CDP_PATH_MAX];
    char name[64];
    if (directory == NULL || directory[0] == '\0' || !directory_exists(directory))
        return 0;
#ifdef _WIN32
    if (GetFullPathNameA(directory, (DWORD)sizeof(canonical), canonical, NULL) == 0u)
        return 0;
#else
    {
        char *resolved = realpath(directory, NULL);
        if (resolved == NULL || strlen(resolved) >= sizeof(canonical)) {
            free(resolved);
            return 0;
        }
        memcpy(canonical, resolved, strlen(resolved) + 1u);
        free(resolved);
    }
#endif
    snprintf(name, sizeof(name), "pvoc%s", TS_EXE_SUFFIX);
    if (!path_join(pvoc, sizeof(pvoc), canonical, name)) return 0;
    snprintf(name, sizeof(name), "glisten%s", TS_EXE_SUFFIX);
    if (!path_join(glisten, sizeof(glisten), canonical, name)) return 0;
    if (!file_exists(pvoc) || !file_exists(glisten)) {
        char message[256];
        snprintf(message, sizeof(message),
                 "CDP RUNTIME NEEDS PVOC%s AND GLISTEN%s", TS_EXE_SUFFIX,
                 TS_EXE_SUFFIX);
        set_error(error, error_size, message);
        return 0;
    }
    snprintf(runtime->bin_directory, sizeof(runtime->bin_directory), "%s", canonical);
    snprintf(runtime->pvoc_path, sizeof(runtime->pvoc_path), "%s", pvoc);
    snprintf(runtime->glisten_path, sizeof(runtime->glisten_path), "%s", glisten);
    runtime->available = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_cdp_runtime_discover(TsCdpRuntime *runtime,
                            const char *configured_directory,
                            const char *executable_directory,
                            char *error, size_t error_size)
{
    char candidate[TS_CDP_PATH_MAX];
    char last_error[256] = "CDP RUNTIME NOT FOUND";
    if (runtime == NULL) {
        set_error(error, error_size, "CDP runtime destination is missing");
        return 0;
    }
    ts_cdp_runtime_init(runtime);
    if (configured_directory != NULL && configured_directory[0] != '\0') {
        if (runtime_from_directory(runtime, configured_directory,
                                   last_error, sizeof(last_error))) {
            set_error(error, error_size, "");
            return 1;
        }
        set_error(error, error_size, last_error);
        return 0;
    }
    if (executable_directory != NULL && executable_directory[0] != '\0') {
        if (path_join(candidate, sizeof(candidate), executable_directory, "cdp/bin") &&
            runtime_from_directory(runtime, candidate, last_error, sizeof(last_error)))
            return 1;
        if (path_join(candidate, sizeof(candidate), executable_directory, "cdp") &&
            runtime_from_directory(runtime, candidate, last_error, sizeof(last_error)))
            return 1;
    }
    if (runtime_from_directory(runtime, "cdp/bin", last_error, sizeof(last_error)))
        return 1;
    set_error(error, error_size,
              "CDP RUNTIME NOT FOUND - SET CDP BIN IN CONFIG");
    return 0;
}

void ts_cdp_run_options_init(TsCdpRunOptions *options)
{
    if (options == NULL) return;
    memset(options, 0, sizeof(*options));
    options->timeout_ms = 120000u;
}

void ts_cdp_run_result_init(TsCdpRunResult *result)
{
    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
    result->status = TS_CDP_RUN_IDLE;
    result->safety = TS_CDP_SAFETY_INVALID;
    result->finite = 1;
    ts_sample_init(&result->output);
}

void ts_cdp_run_result_free(TsCdpRunResult *result)
{
    if (result == NULL) return;
    ts_sample_free(&result->output);
    ts_cdp_run_result_init(result);
}

static int default_temporary_root(char *path, size_t path_size)
{
#ifdef _WIN32
    char base[TS_CDP_PATH_MAX];
    DWORD length = GetTempPathA((DWORD)sizeof(base), base);
    if (length == 0u || length >= sizeof(base)) return 0;
    return path_join(path, path_size, base, "tapesister-cdp");
#else
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') base = "/tmp";
    return path_join(path, path_size, base, "tapesister-cdp");
#endif
}

static int create_job_directory(const TsCdpRunOptions *options,
                                char *directory, size_t directory_size,
                                char *error, size_t error_size)
{
    char root[TS_CDP_PATH_MAX];
    uint64_t identity = options != NULL && options->job_id != 0u ?
                        options->job_id : now_ms();
    if (options != NULL && options->temporary_root[0] != '\0')
        snprintf(root, sizeof(root), "%s", options->temporary_root);
    else if (!default_temporary_root(root, sizeof(root))) {
        set_error(error, error_size, "Could not resolve temporary CDP root");
        return 0;
    }
    if (!make_directory(root)) {
        set_error(error, error_size, "Could not create temporary CDP root");
        return 0;
    }
    for (unsigned attempt = 0; attempt < 128u; ++attempt) {
        char leaf[96];
#ifdef _WIN32
        unsigned long process = GetCurrentProcessId();
#else
        unsigned long process = (unsigned long)getpid();
#endif
        snprintf(leaf, sizeof(leaf), "job-%llu-%lu-%u",
                 (unsigned long long)identity, process, attempt);
        if (!path_join(directory, directory_size, root, leaf)) break;
#ifdef _WIN32
        if (_mkdir(directory) == 0) return 1;
#else
        if (mkdir(directory, 0700) == 0) return 1;
#endif
        if (errno != EEXIST) break;
    }
    set_error(error, error_size, "Could not create isolated CDP job directory");
    return 0;
}

static int read_log(const char *path, char *destination, size_t destination_size)
{
    FILE *file;
    size_t used;
    if (destination == NULL || destination_size == 0u) return 0;
    used = strlen(destination);
    if (used + 1u >= destination_size) return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (used > 0u && used + 3u < destination_size) {
        memcpy(destination + used, " | ", 3u);
        used += 3u;
        destination[used] = '\0';
    }
    used += fread(destination + used, 1, destination_size - used - 1u, file);
    destination[used] = '\0';
    fclose(file);
    return 1;
}

static int contains_text_error(const char *text)
{
    if (text == NULL) return 0;
    for (; strlen(text) >= 6u; ++text)
        if (toupper((unsigned char)text[0]) == 'E' &&
            toupper((unsigned char)text[1]) == 'R' &&
            toupper((unsigned char)text[2]) == 'R' &&
            toupper((unsigned char)text[3]) == 'O' &&
            toupper((unsigned char)text[4]) == 'R' && text[5] == ':') return 1;
    return 0;
}

#ifdef _WIN32
static int quote_windows_argument(const char *argument, char *output,
                                  size_t output_size, size_t *used)
{
    int needs_quotes = argument[0] == '\0' || strpbrk(argument, " \t\"") != NULL;
    size_t backslashes = 0u;
    if (*used > 0u) {
        if (*used + 1u >= output_size) return 0;
        output[(*used)++] = ' ';
    }
    if (needs_quotes) output[(*used)++] = '"';
    for (; *argument != '\0'; ++argument) {
        if (*argument == '\\') { ++backslashes; continue; }
        if (*argument == '"') {
            while (backslashes-- > 0u) {
                if (*used + 2u >= output_size) return 0;
                output[(*used)++] = '\\'; output[(*used)++] = '\\';
            }
            if (*used + 2u >= output_size) return 0;
            output[(*used)++] = '\\'; output[(*used)++] = '"';
            backslashes = 0u;
            continue;
        }
        while (backslashes-- > 0u) {
            if (*used + 1u >= output_size) return 0;
            output[(*used)++] = '\\';
        }
        if (*used + 1u >= output_size) return 0;
        output[(*used)++] = *argument;
        backslashes = 0u;
    }
    if (needs_quotes) {
        while (backslashes-- > 0u) {
            if (*used + 2u >= output_size) return 0;
            output[(*used)++] = '\\'; output[(*used)++] = '\\';
        }
        if (*used + 1u >= output_size) return 0;
        output[(*used)++] = '"';
    } else while (backslashes-- > 0u) {
        if (*used + 1u >= output_size) return 0;
        output[(*used)++] = '\\';
    }
    output[*used] = '\0';
    return 1;
}
#endif

static int execute_command(const char *executable, const TsCdpCommand *command,
                           const char *job_directory, unsigned stage,
                           const TsCdpRunOptions *options,
                           TsCdpRunResult *result,
                           char *error, size_t error_size)
{
    char stdout_path[TS_CDP_PATH_MAX];
    char stderr_path[TS_CDP_PATH_MAX];
    char filename[64];
    uint64_t started = now_ms();
    uint32_t timeout = options != NULL && options->timeout_ms > 0u ?
                       options->timeout_ms : 120000u;
    snprintf(filename, sizeof(filename), "stage-%u.stdout.log", stage + 1u);
    if (!path_join(stdout_path, sizeof(stdout_path), job_directory, filename)) return 0;
    snprintf(filename, sizeof(filename), "stage-%u.stderr.log", stage + 1u);
    if (!path_join(stderr_path, sizeof(stderr_path), job_directory, filename)) return 0;
    if (options != NULL && options->fault == TS_CDP_FAULT_LAUNCH) {
        set_error(error, error_size, "Injected CDP process launch failure");
        return 0;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_NONZERO_EXIT) {
        result->exit_code = 9;
        set_error(error, error_size, "Injected CDP nonzero exit");
        return 0;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_TEXT_ERROR) {
        snprintf(result->diagnostic, sizeof(result->diagnostic),
                 "ERROR: injected misleading success");
        set_error(error, error_size, "CDP reported ERROR in its diagnostic output");
        return 0;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_TIMEOUT) {
        result->status = TS_CDP_RUN_TIMEOUT;
        set_error(error, error_size, "Injected CDP timeout");
        return 0;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_CANCEL) {
        result->status = TS_CDP_RUN_CANCELLED;
        set_error(error, error_size, "Injected CDP cancellation");
        return 0;
    }
#ifdef _WIN32
    {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
        HANDLE stdout_file;
        HANDLE stderr_file;
        HANDLE job = NULL;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits;
        char command_line[4096] = "";
        size_t used = 0u;
        DWORD exit_code = 1u;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        stdout_file = CreateFileA(stdout_path, GENERIC_WRITE, FILE_SHARE_READ,
                                  &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        stderr_file = CreateFileA(stderr_path, GENERIC_WRITE, FILE_SHARE_READ,
                                  &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (stdout_file == INVALID_HANDLE_VALUE || stderr_file == INVALID_HANDLE_VALUE) {
            if (stdout_file != INVALID_HANDLE_VALUE) CloseHandle(stdout_file);
            if (stderr_file != INVALID_HANDLE_VALUE) CloseHandle(stderr_file);
            set_error(error, error_size, "Could not create CDP diagnostic logs");
            return 0;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = stdout_file;
        startup.hStdError = stderr_file;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        if (!quote_windows_argument(executable, command_line, sizeof(command_line), &used))
            goto windows_failed;
        for (int i = 0; i < command->argc; ++i)
            if (!quote_windows_argument(command->arguments[i], command_line,
                                        sizeof(command_line), &used)) goto windows_failed;
        memset(&job_limits, 0, sizeof(job_limits));
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        job = CreateJobObjectA(NULL, NULL);
        if (job == NULL ||
            !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &job_limits, sizeof(job_limits)))
            goto windows_failed;
        if (!CreateProcessA(executable, command_line, NULL, NULL, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, job_directory,
                            &startup, &process)) goto windows_failed;
        CloseHandle(stdout_file); CloseHandle(stderr_file);
        stdout_file = stderr_file = INVALID_HANDLE_VALUE;
        if (!AssignProcessToJobObject(job, process.hProcess) ||
            ResumeThread(process.hThread) == (DWORD)-1) {
            TerminateJobObject(job, 126u);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            memset(&process, 0, sizeof(process));
            goto windows_failed;
        }
        for (;;) {
            DWORD waiting = WaitForSingleObject(process.hProcess, 10u);
            if (waiting == WAIT_OBJECT_0) break;
            if (options != NULL && options->cancel_check != NULL &&
                options->cancel_check(options->cancel_userdata)) {
                TerminateJobObject(job, 2u);
                result->status = TS_CDP_RUN_CANCELLED;
                break;
            }
            if (now_ms() - started >= timeout) {
                TerminateJobObject(job, 3u);
                result->status = TS_CDP_RUN_TIMEOUT;
                break;
            }
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        GetExitCodeProcess(process.hProcess, &exit_code);
        result->exit_code = (int)exit_code;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        CloseHandle(job);
        job = NULL;
        if (result->status == TS_CDP_RUN_CANCELLED ||
            result->status == TS_CDP_RUN_TIMEOUT) {
            read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
            read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
            set_error(error, error_size,
                      result->status == TS_CDP_RUN_CANCELLED ?
                      "CDP render cancelled" : "CDP render timed out");
            return 0;
        }
        if (exit_code != 0u) {
            read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
            read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
            set_error(error, error_size, "CDP process returned a nonzero exit code");
            return 0;
        }
        goto windows_finished;
windows_failed:
        if (job != NULL) CloseHandle(job);
        if (stdout_file != INVALID_HANDLE_VALUE) CloseHandle(stdout_file);
        if (stderr_file != INVALID_HANDLE_VALUE) CloseHandle(stderr_file);
        set_error(error, error_size, "Could not launch CDP process");
        return 0;
windows_finished:;
    }
#else
    {
        pid_t child = fork();
        int status = 0;
        if (child < 0) {
            set_error(error, error_size, "Could not launch CDP process");
            return 0;
        }
        if (child == 0) {
            int output = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            int errors = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            char *arguments[TS_CDP_MAX_COMMAND_ARGS + 2];
            (void)setpgid(0, 0);
            if (output < 0 || errors < 0 || chdir(job_directory) != 0) _exit(126);
            (void)dup2(output, STDOUT_FILENO);
            (void)dup2(errors, STDERR_FILENO);
            close(output); close(errors);
            arguments[0] = (char *)executable;
            for (int i = 0; i < command->argc; ++i)
                arguments[i + 1] = (char *)command->arguments[i];
            arguments[command->argc + 1] = NULL;
            execv(executable, arguments);
            dprintf(STDERR_FILENO, "ERROR: process launch failed: %s\n", strerror(errno));
            _exit(127);
        }
        (void)setpgid(child, child);
        for (;;) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break;
            if (waited < 0) {
                set_error(error, error_size, "Could not reap CDP process");
                return 0;
            }
            if (options != NULL && options->cancel_check != NULL &&
                options->cancel_check(options->cancel_userdata)) {
                (void)kill(-child, SIGTERM);
                pause_10ms();
                if (waitpid(child, &status, WNOHANG) == 0) (void)kill(-child, SIGKILL);
                (void)waitpid(child, &status, 0);
                result->status = TS_CDP_RUN_CANCELLED;
                read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
                read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
                set_error(error, error_size, "CDP render cancelled");
                return 0;
            }
            if (now_ms() - started >= timeout) {
                (void)kill(-child, SIGTERM);
                pause_10ms();
                if (waitpid(child, &status, WNOHANG) == 0) (void)kill(-child, SIGKILL);
                (void)waitpid(child, &status, 0);
                result->status = TS_CDP_RUN_TIMEOUT;
                read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
                read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
                set_error(error, error_size, "CDP render timed out");
                return 0;
            }
            pause_10ms();
        }
        result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
            read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
            set_error(error, error_size, "CDP process returned a nonzero exit code");
            return 0;
        }
    }
#endif
    read_log(stdout_path, result->diagnostic, sizeof(result->diagnostic));
    read_log(stderr_path, result->diagnostic, sizeof(result->diagnostic));
    if (contains_text_error(result->diagnostic)) {
        set_error(error, error_size, "CDP reported ERROR in its diagnostic output");
        return 0;
    }
    return 1;
}

static uint16_t read_le16(const unsigned char *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static float float_le32(const unsigned char *data)
{
    uint32_t bits = read_le32(data);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int probe_wav(const char *path, TsCdpWavInfo *info,
                     char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    unsigned char header[12];
    unsigned char format_data[40];
    memset(info, 0, sizeof(*info));
    info->data_offset = -1;
    info->finite = 1;
    if (file == NULL) { set_error(error, error_size, "CDP output WAV is missing"); return 0; }
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(file); set_error(error, error_size, "CDP output is not a valid WAV"); return 0;
    }
    while (!feof(file)) {
        unsigned char chunk[8];
        uint32_t size;
        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) break;
        size = read_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            size_t keep = size < sizeof(format_data) ? size : sizeof(format_data);
            if (keep < 16u || fread(format_data, 1, keep, file) != keep) break;
            info->format = read_le16(format_data);
            info->channels = read_le16(format_data + 2);
            info->sample_rate = read_le32(format_data + 4);
            info->block_align = read_le16(format_data + 12);
            info->bits = read_le16(format_data + 14);
            if (size > keep) (void)fseek(file, (long)(size - keep), SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            info->data_offset = ftell(file);
            info->data_size = size;
            (void)fseek(file, (long)size, SEEK_CUR);
        } else (void)fseek(file, (long)size, SEEK_CUR);
        if ((size & 1u) != 0u) (void)fseek(file, 1, SEEK_CUR);
    }
    if ((info->format != 1u && info->format != 3u) || info->channels != 1u ||
        info->sample_rate < 1000u || info->block_align == 0u ||
        info->data_offset < 0 || info->data_size < info->block_align ||
        (info->format == 3u && info->bits != 32u)) {
        fclose(file); set_error(error, error_size,
                               "CDP output must be a nonempty mono PCM/float WAV"); return 0;
    }
    info->frames = info->data_size / info->block_align;
    if (info->frames == 0u || info->frames > 100000000u) {
        fclose(file); set_error(error, error_size, "CDP output length is invalid"); return 0;
    }
    if (info->format == 3u) {
        unsigned char frame[128];
        if (info->block_align > sizeof(frame) ||
            fseek(file, info->data_offset, SEEK_SET) != 0) {
            fclose(file); set_error(error, error_size, "CDP float WAV layout is invalid"); return 0;
        }
        for (size_t i = 0; i < info->frames; ++i) {
            float value;
            if (fread(frame, 1, info->block_align, file) != info->block_align) {
                fclose(file); set_error(error, error_size, "CDP WAV ended early"); return 0;
            }
            value = float_le32(frame);
            if (!isfinite(value)) info->finite = 0;
            if (isfinite(value) && fabsf(value) > info->raw_peak)
                info->raw_peak = fabsf(value);
        }
    }
    fclose(file);
    if (!info->finite) { set_error(error, error_size, "CDP output contains NaN or infinity"); return 0; }
    set_error(error, error_size, "");
    return 1;
}

static void analyze_output(TsCdpRunResult *result, float raw_peak)
{
    double sum = 0.0;
    int clipped = 0;
    float peak = raw_peak;
    result->finite = 1;
    for (size_t i = 0; i < result->output.frames; ++i) {
        float value = result->output.data[i];
        if (!isfinite(value)) { result->finite = 0; continue; }
        if (fabsf(value) > peak) peak = fabsf(value);
        if (fabsf(value) >= 0.9999f) ++clipped;
        sum += value;
    }
    result->peak = peak;
    result->dc_offset = result->output.frames > 0u ? sum / result->output.frames : 0.0;
    result->clipped_samples = clipped;
    if (!result->finite) result->safety = TS_CDP_SAFETY_INVALID;
    else if (peak < 0.00001f) result->safety = TS_CDP_SAFETY_SILENT;
    else if (peak > 1.0f || clipped > 0 || fabs(result->dc_offset) > 0.1)
        result->safety = TS_CDP_SAFETY_HOT;
    else result->safety = TS_CDP_SAFETY_SAFE;
}

int ts_cdp_cleanup_job_directory(const char *directory,
                                 char *error, size_t error_size)
{
    if (directory == NULL || directory[0] == '\0') return 1;
#ifdef _WIN32
    {
        WIN32_FIND_DATAA entry;
        HANDLE search;
        char pattern[TS_CDP_PATH_MAX];
        if (!path_join(pattern, sizeof(pattern), directory, "*")) return 0;
        search = FindFirstFileA(pattern, &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                char path[TS_CDP_PATH_MAX];
                if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0)
                    continue;
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    !path_join(path, sizeof(path), directory, entry.cFileName) ||
                    !DeleteFileA(path)) { FindClose(search); goto failed; }
            } while (FindNextFileA(search, &entry));
            FindClose(search);
        }
        if (!RemoveDirectoryA(directory)) goto failed;
    }
#else
    {
        DIR *opened = opendir(directory);
        struct dirent *entry;
        if (opened == NULL) goto failed;
        while ((entry = readdir(opened)) != NULL) {
            char path[TS_CDP_PATH_MAX];
            struct stat state;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            if (!path_join(path, sizeof(path), directory, entry->d_name) ||
                lstat(path, &state) != 0 || !S_ISREG(state.st_mode) || unlink(path) != 0) {
                closedir(opened); goto failed;
            }
        }
        closedir(opened);
        if (rmdir(directory) != 0) goto failed;
    }
#endif
    set_error(error, error_size, "");
    return 1;
failed:
    set_error(error, error_size, "Could not completely clean the CDP job directory");
    return 0;
}

int ts_cdp_run_recipe(const TsCdpRuntime *runtime,
                      const TsCdpRecipe *recipe,
                      const TsCdpRecipeValues *values,
                      const TsSample *input,
                      const TsCdpRunOptions *options,
                      TsCdpRunResult *result,
                      char *error, size_t error_size)
{
    TsCdpCommand commands[3];
    TsCdpWavInfo wav;
    char input_path[TS_CDP_PATH_MAX];
    char output_path[TS_CDP_PATH_MAX];
    char intermediate_path[TS_CDP_PATH_MAX];
    char cleanup_error[160];
    int ok = 0;
    if (result == NULL) { set_error(error, error_size, "CDP result destination is missing"); return 0; }
    ts_cdp_run_result_free(result);
    if (runtime == NULL || !runtime->available || !file_exists(runtime->pvoc_path) ||
        !file_exists(runtime->glisten_path)) {
        result->status = TS_CDP_RUN_FAILED;
        set_error(error, error_size, "CDP RUNTIME NEEDS PVOC AND GLISTEN");
        return 0;
    }
    if (input == NULL || input->data == NULL || input->frames == 0u ||
        !ts_cdp_recipe_validate(recipe, error, error_size) ||
        !ts_cdp_recipe_input_valid(recipe, input->frames, input->sample_rate,
                                   error, error_size) ||
        !ts_cdp_glisten_build_commands(recipe, values, commands,
                                       error, error_size)) {
        result->status = TS_CDP_RUN_FAILED;
        return 0;
    }
    if (!create_job_directory(options, result->job_directory,
                              sizeof(result->job_directory), error, error_size)) {
        result->status = TS_CDP_RUN_FAILED;
        return 0;
    }
    if (!path_join(input_path, sizeof(input_path), result->job_directory, "input.wav") ||
        !path_join(output_path, sizeof(output_path), result->job_directory, "output.wav") ||
        !ts_sample_save_wav16(input, input_path, error, error_size)) goto finished;
    for (unsigned stage = 0; stage < 3u; ++stage) {
        const char *executable = strcmp(commands[stage].executable, "pvoc") == 0 ?
                                 runtime->pvoc_path : runtime->glisten_path;
        snprintf(result->failed_executable, sizeof(result->failed_executable), "%s",
                 commands[stage].executable);
        if (!execute_command(executable, &commands[stage], result->job_directory,
                             stage, options, result, error, error_size)) goto finished;
        if (stage < 2u) {
            const char *name = stage == 0u ? "input.ana" : "glisten.ana";
            if (!path_join(intermediate_path, sizeof(intermediate_path),
                           result->job_directory, name) ||
                !regular_output_file(intermediate_path)) {
                set_error(error, error_size,
                          "CDP stage did not create its required analysis file");
                goto finished;
            }
        }
    }
    result->failed_executable[0] = '\0';
    if (options != NULL && options->fault == TS_CDP_FAULT_MISSING_OUTPUT)
        (void)remove(output_path);
    else if (options != NULL && options->fault == TS_CDP_FAULT_EMPTY_OUTPUT) {
        FILE *empty = fopen(output_path, "wb");
        if (empty != NULL) fclose(empty);
    } else if (options != NULL && options->fault == TS_CDP_FAULT_MALFORMED_WAV) {
        FILE *bad = fopen(output_path, "wb");
        if (bad != NULL) { fwrite("BAD", 1, 3, bad); fclose(bad); }
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_NONFINITE_OUTPUT) {
        set_error(error, error_size, "Injected CDP output contains NaN or infinity");
        goto finished;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_UNSUPPORTED_CHANNELS) {
        set_error(error, error_size, "Injected CDP output has unsupported channels");
        goto finished;
    }
    if (options != NULL && options->fault == TS_CDP_FAULT_EXCESSIVE_LENGTH) {
        set_error(error, error_size, "Injected CDP output length is excessive");
        goto finished;
    }
    if (!regular_output_file(output_path)) {
        set_error(error, error_size,
                  "CDP output is missing, not regular, or is a redirected path");
        goto finished;
    }
    if (!probe_wav(output_path, &wav, error, error_size)) goto finished;
    if (wav.sample_rate != input->sample_rate) {
        set_error(error, error_size, "CDP output sample rate changed unexpectedly");
        goto finished;
    }
    if (!ts_sample_load_wav(&result->output, output_path, error, error_size)) goto finished;
    analyze_output(result, wav.raw_peak);
    if (!result->finite || result->output.frames == 0u ||
        result->output.frames > TS_CANVAS_MAX_FRAMES ||
        (input->frames <= SIZE_MAX / 64u &&
         result->output.frames > input->frames * 64u)) {
        set_error(error, error_size, "CDP output failed safety validation");
        goto finished;
    }
    result->status = TS_CDP_RUN_OK;
    ok = 1;
    set_error(error, error_size, "");
finished:
    if (!ok && result->status != TS_CDP_RUN_CANCELLED &&
        result->status != TS_CDP_RUN_TIMEOUT) result->status = TS_CDP_RUN_FAILED;
    if (options != NULL && options->fault == TS_CDP_FAULT_CLEANUP) {
        result->cleanup_failed = 1;
    } else if (!ts_cdp_cleanup_job_directory(result->job_directory,
                                             cleanup_error, sizeof(cleanup_error))) {
        result->cleanup_failed = 1;
        if (!ok && error != NULL && error_size > 0u && error[0] == '\0')
            set_error(error, error_size, cleanup_error);
    } else result->job_directory[0] = '\0';
    return ok;
}

const char *ts_cdp_safety_name(TsCdpSafetyStatus safety)
{
    switch (safety) {
    case TS_CDP_SAFETY_SAFE: return "SAFE";
    case TS_CDP_SAFETY_HOT: return "HOT";
    case TS_CDP_SAFETY_SILENT: return "SILENT";
    default: return "INVALID";
    }
}
