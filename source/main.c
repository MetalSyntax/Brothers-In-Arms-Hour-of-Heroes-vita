/**
 * @file main.c
 * @brief Application entry point and runtime bootstrap for the PS Vita iOS loader.
 */
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/io/dirent.h>
#include <psp2/message_dialog.h>
#include <vitaGL.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <stdio.h>
#include <string.h>

#include "darwin.h"
#include "gles.h"
#include "hle.h"
#include "input.h"
#include "log.h"
#include "macho.h"
#include "objc.h"
#include "patch.h"

int _newlib_heap_size_user = 192 * 1024 * 1024;
// Guest main thread requires substantial stack headroom (4 MB)
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// Rotating log file manager (brothersinarmshourofheroes_001.log .. brothersinarmshourofheroes_999.log)
static int next_log_number(void) {
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(LOGS_PATH, 0777);
    int last = 0, n;
    uint64_t newest = 0;
    SceUID d = sceIoDopen(LOGS_PATH);
    SceIoDirent e;
    while (d >= 0 && sceIoDread(d, &e) > 0) {
        const SceDateTime *t = &e.d_stat.st_mtime;
        uint64_t key = (uint64_t)t->year << 48 | (uint64_t)t->month << 40 | (uint64_t)t->day << 32 |
                       (uint64_t)t->hour << 24 | t->minute << 16 | t->second << 8 | t->microsecond / 10000;
        if (sscanf(e.d_name, "brothersinarmshourofheroes_%3d.log", &n) == 1 && n >= 1 && n <= 999 && key >= newest) {
            newest = key;
            last = n;
        }
    }
    if (d >= 0)
        sceIoDclose(d);
    return (last % 999) + 1;
}

// Boot checker: a missing file is reported on screen (and in the log) with its exact path.
static void boot_error(const char *fmt, const char *path) {
    static char text[512];
    snprintf(text, sizeof(text), fmt, path);
    l_error("[BOOT] %s", text);
    SceMsgDialogUserMessageParam msg;
    memset(&msg, 0, sizeof(msg));
    msg.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    msg.msg = (const SceChar8 *)text;
    SceMsgDialogParam p;
    sceMsgDialogParamInit(&p);
    p.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    p.userMsgParam = &msg;
    if (sceMsgDialogInit(&p) >= 0) {
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_TRUE);
        }
        sceMsgDialogTerm();
    }
    sceKernelExitProcess(0);
}

static int file_exists(const char *path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

int main(int argc, char *argv[]) {
    // Standard PS Vita overclock for optimal emulation frame pacing
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Prepare save data and logs directories
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(DOCS_PATH, 0777);
    sceIoMkdir(DOCS_PATH "save", 0777);
    sceIoMkdir(DATA_PATH "game_app/save", 0777);
    char log_path[128];
    snprintf(log_path, sizeof(log_path), LOGS_PATH "brothersinarmshourofheroes_%03d.log", next_log_number());
    log_open(log_path);
    l_info("=== iOS Mach-O Loader for PS Vita Initializing ===");

    // Verify ShaRKBR33D runtime availability
    if (sceIoGetstat("ur0:/data/libshacccg.suprx", &(SceIoStat){0}) < 0 &&
        sceIoGetstat("ur0:/data/external/libshacccg.suprx", &(SceIoStat){0}) < 0) {
        fatal_error("Missing libshacccg.suprx (install it via ShaRKBR33D on console).");
    }

    // Initialize AppUtil and system common dialogs
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    SceCommonDialogConfigParam dlg;
    sceCommonDialogConfigParamInit(&dlg);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&dlg.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&dlg.enterButtonAssign);
    sceCommonDialogSetConfigParam(&dlg);

    // Initialize subsystems
    gles_init();
    darwin_init();
    input_init();

    if (!file_exists(BUNDLE_PATH EXEC_NAME))
        boot_error("Game executable not found:\n%s\n\nCopy the decrypted IPA's BIA.app contents there (tools/stage_data.sh).",
                   BUNDLE_PATH EXEC_NAME);
    if (!file_exists(DATA_PATH "rebase.bin"))
        boot_error("Relocation table not found:\n%s\n\nRun tools/stage_data.sh and copy ux0_data/ again.",
                   DATA_PATH "rebase.bin");
    if (!file_exists(BUNDLE_PATH "m_menu.wav"))
        l_warn("[BOOT] %sm_menu.wav missing: music needs tools/convert_audio.sh (game runs without it)", BUNDLE_PATH);

    // Load guest Mach-O executable
    // Note: Executable is loaded from inside bundle game_app/
    // or custom filename configured in patch.c / CMakeLists
    l_info("Loading primary Mach-O executable...");
    if (macho_load(BUNDLE_PATH EXEC_NAME, DATA_PATH "rebase.bin") < 0)
        boot_error("Failed to load the Mach-O executable:\n%s", BUNDLE_PATH EXEC_NAME);

    // Initialize game-specific hooks and memory patches
    patch_init();
    input_bind();

    // Execute C/C++ static constructors (__mod_init_func)
    macho_run_inits();

    // Locate entry point
    uintptr_t entry = macho_sym("_main");
    if (!entry)
        entry = macho_sym("_SDL_main");
    if (!entry)
        entry = macho_sym("_startClet");

    if (entry) {
        l_info("Jumping to guest entry point at 0x%08X (Mach-O offset 0x%08X)",
               (unsigned)entry, (unsigned)(entry - MACHO_SLIDE));
        call_guest((void *)entry, 0, 0, 0, 0);
    } else {
        l_warn("Neither '_main' nor '_startClet' found; entering default event loop...");
    }

    // Main loop for input sampling and touch dispatching (render presentation is driven by ThreadRoutine / presentRenderbuffer)
    for (;;) {
        input_poll();
        ime_poll();
        sceKernelDelayThread(8000); // ~120 Hz input sampling
    }

    return 0;
}
