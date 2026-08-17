#ifndef TAPESISTER_CDP_ADAPTER_H
#define TAPESISTER_CDP_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/cdp_recipe.h"
#include "tapesister/sample.h"

enum {
    TS_CDP_PATH_MAX = 1024,
    TS_CDP_DIAGNOSTIC_MAX = 2048
};

typedef enum {
    TS_CDP_RUN_IDLE = 0,
    TS_CDP_RUN_OK,
    TS_CDP_RUN_CANCELLED,
    TS_CDP_RUN_TIMEOUT,
    TS_CDP_RUN_FAILED
} TsCdpRunStatus;

typedef enum {
    TS_CDP_SAFETY_SAFE = 0,
    TS_CDP_SAFETY_HOT,
    TS_CDP_SAFETY_SILENT,
    TS_CDP_SAFETY_INVALID
} TsCdpSafetyStatus;

typedef enum {
    TS_CDP_FAULT_NONE = 0,
    TS_CDP_FAULT_LAUNCH,
    TS_CDP_FAULT_NONZERO_EXIT,
    TS_CDP_FAULT_TEXT_ERROR,
    TS_CDP_FAULT_TIMEOUT,
    TS_CDP_FAULT_CANCEL,
    TS_CDP_FAULT_MISSING_OUTPUT,
    TS_CDP_FAULT_EMPTY_OUTPUT,
    TS_CDP_FAULT_MALFORMED_WAV,
    TS_CDP_FAULT_NONFINITE_OUTPUT,
    TS_CDP_FAULT_UNSUPPORTED_CHANNELS,
    TS_CDP_FAULT_EXCESSIVE_LENGTH,
    TS_CDP_FAULT_CLEANUP
} TsCdpFault;

typedef int (*TsCdpCancelCheck)(void *userdata);

typedef struct {
    char bin_directory[TS_CDP_PATH_MAX];
    char pvoc_path[TS_CDP_PATH_MAX];
    char glisten_path[TS_CDP_PATH_MAX];
    int available;
} TsCdpRuntime;

typedef struct {
    uint64_t job_id;
    uint32_t timeout_ms;
    char temporary_root[TS_CDP_PATH_MAX];
    TsCdpCancelCheck cancel_check;
    void *cancel_userdata;
    TsCdpFault fault;
} TsCdpRunOptions;

typedef struct {
    TsCdpRunStatus status;
    TsCdpSafetyStatus safety;
    TsSample output;
    float peak;
    double dc_offset;
    int clipped_samples;
    int finite;
    int exit_code;
    char failed_executable[TS_CDP_TEXT_MAX];
    char diagnostic[TS_CDP_DIAGNOSTIC_MAX];
    char job_directory[TS_CDP_PATH_MAX];
    int cleanup_failed;
} TsCdpRunResult;

void ts_cdp_runtime_init(TsCdpRuntime *runtime);
int ts_cdp_runtime_discover(TsCdpRuntime *runtime,
                            const char *configured_directory,
                            const char *executable_directory,
                            char *error, size_t error_size);
int ts_cdp_runtime_recipe_available(const TsCdpRuntime *runtime,
                                    const TsCdpRecipe *recipe,
                                    char *error, size_t error_size);
void ts_cdp_run_options_init(TsCdpRunOptions *options);
void ts_cdp_run_result_init(TsCdpRunResult *result);
void ts_cdp_run_result_free(TsCdpRunResult *result);
int ts_cdp_run_recipe(const TsCdpRuntime *runtime,
                      const TsCdpRecipe *recipe,
                      const TsCdpRecipeValues *values,
                      const TsSample *input,
                      const TsCdpRunOptions *options,
                      TsCdpRunResult *result,
                      char *error, size_t error_size);
int ts_cdp_cleanup_job_directory(const char *directory,
                                 char *error, size_t error_size);
const char *ts_cdp_safety_name(TsCdpSafetyStatus safety);

#endif
