#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

static FILE *open_diagnostic_log(void)
{
    const char *path = getenv("TAPESISTER_DIAGNOSTIC_LOG");
#ifdef _WIN32
    if (path == NULL || path[0] == '\0') path = "tapesister-diagnostic.log";
#else
    if (path == NULL || path[0] == '\0') return NULL;
#endif
    return fopen(path, "ab");
}

static void write_diagnostic_line(const char *phase)
{
    FILE *file = open_diagnostic_log();
    if (file == NULL) return;
#ifdef _WIN32
    fprintf(file,
            "%s pid=%lu TsInstrument=%zu TsFramebuffer=%zu TsUiState=%zu\n",
            phase, (unsigned long)GetCurrentProcessId(),
            sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState));
#else
    fprintf(file, "%s TsInstrument=%zu TsFramebuffer=%zu TsUiState=%zu\n",
            phase, sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState));
#endif
    fflush(file);
    fclose(file);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void tapesister_process_constructor(void)
{
    /* This executes before main() creates its stack frame. On Windows it gives us
       a durable breadcrumb even if entry into main later exhausts the thread stack. */
    write_diagnostic_line("PROCESS_CONSTRUCTOR");
}

__attribute__((destructor))
static void tapesister_process_destructor(void)
{
    write_diagnostic_line("PROCESS_DESTRUCTOR");
}
#endif
