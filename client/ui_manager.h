/**
 * @file ui_manager.h
 * @brief LVGL UI Manager for FTP client
 *
 * Manages three screens: login, main (file list with multi-select),
 * and a progress overlay.  All LVGL widget operations run on the UI
 * thread; the network thread pushes data through lv_async_call().
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "../lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */
#define MAX_SELECTED_FILES  128

/* ------------------------------------------------------------------ */
/*  Screen references                                                 */
/* ------------------------------------------------------------------ */
extern lv_obj_t *login_screen;
extern lv_obj_t *main_screen;

/* ------------------------------------------------------------------ */
/*  Multi-selection state                                             */
/* ------------------------------------------------------------------ */
extern char g_selected_remote[MAX_SELECTED_FILES][256];
extern int  g_remote_sel_count;
extern char g_selected_local[MAX_SELECTED_FILES][256];
extern int  g_local_sel_count;

/* ------------------------------------------------------------------ */
/*  Initialisation (call once from main())                            */
/* ------------------------------------------------------------------ */
void ui_login_init(void);
void ui_main_init(void);

/* ------------------------------------------------------------------ */
/*  Screen switching                                                  */
/* ------------------------------------------------------------------ */
void ui_switch_to_login(void);
void ui_switch_to_main(void);

/* ------------------------------------------------------------------ */
/*  Progress overlay                                                  */
/* ------------------------------------------------------------------ */
void ui_show_progress(const char *filename, bool is_upload);
void ui_show_progress_batch(void);
void ui_hide_progress(void);
void ui_update_progress(int percent, int current_bytes, int total_bytes,
                        const char *filename, bool is_upload);
void ui_update_transfer_progress(const char *filename, int percent,
                                  int current_bytes, int total_bytes,
                                  bool is_upload);
void ui_on_transfer_done(const char *filename, bool success, bool is_upload);

/* ------------------------------------------------------------------ */
/*  Status & error                                                    */
/* ------------------------------------------------------------------ */
void ui_set_status(const char *msg);
void ui_show_error(const char *msg);

/* ------------------------------------------------------------------ */
/*  Async callbacks (called from network thread via lv_async_call)    */
/* ------------------------------------------------------------------ */
void ui_update_file_list_cb(void *data);
void ui_update_local_file_list_cb(void *data);
void ui_refresh_local_files(void);

/* Query whether a file name is currently displayed in a list widget. */
bool ui_remote_list_has_file(const char *name);
bool ui_local_list_has_file(const char *name);
void ui_restore_status_after_delay(void);
void ui_show_error_popup(const char *msg);

#endif /* UI_MANAGER_H */
