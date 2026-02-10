/**
 * etsuko.h - Main part of the application that handles global initialization and cleanup
 */

#ifndef ETSUKO_ETSUKO_H
#define ETSUKO_ETSUKO_H

// Initializes stuff that is used through all the operating modes and should not get re-initialized more than once
void global_init(void);
// Frees the stuff initialized by init. This should only be called once per run (same with init)
void global_finish(void);

// The mode the application should operate on
typedef enum Config_OpMode_t {
    APP_MODE_NONE = 0,
    // Shows a single song with album art on one side and the scrolling lyrics on the other
    APP_MODE_KARAOKE,
    // Shows a menu where you can select a song to open the karaoke mode with
    APP_MODE_MENU,
} Config_OpMode_t;

typedef enum AppStatus_t {
    APP_STATUS_OK = 0,
    APP_STATUS_FAILURE,
    APP_STATUS_LOADING,
} AppStatus_t;

struct Ui_t;

// Switch the operating mode for the application. This includes finishing the execution and freeing up the resources of the current
// running mode, if there's any
void global_mode_switch(Config_OpMode_t mode);
// Load process of the current switched-to mode, which returns a status code telling the progress
AppStatus_t global_load();
// The main loop for the current operating mode
AppStatus_t global_loop();
// Returns the current operating mode
Config_OpMode_t global_active_mode();
// Whether the current selected mode has finished loading
bool global_mode_finished_loading();
// Actually runs the stuff because if you switch modes mid frame, bad things can happen
void global_update();
// Setup the version string on the top right
void etsuko_setup_version(struct Ui_t *ui);
// Change the address bar on the web build
void etsuko_navigate(const char *relative_address);

#endif // ETSUKO_ETSUKO_H
