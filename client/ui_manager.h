/**
 * @file ui_manager.h
 * @brief LVGL UI Manager for FTP client
 *
 * Manages three screens:  login, main (file list), and a progress
 * overlay.  All LVGL widget operations run on the UI thread; the
 * network thread pushes data through lv_async_call().
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "../lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Screen references                                                 */
/* ------------------------------------------------------------------ */
extern lv_obj_t *login_screen;
extern lv_obj_t *main_screen;

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
void ui_hide_progress(void);
void ui_update_progress(int percent, int current_bytes, int total_bytes,
                        const char *filename, bool is_upload);

/* ------------------------------------------------------------------ */
/*  Status & error                                                    */
/* ------------------------------------------------------------------ */
void ui_set_status(const char *msg);
void ui_show_error(const char *msg);

/* ------------------------------------------------------------------ */
/*  Async callbacks (called from network thread via lv_async_call)    */
/* ------------------------------------------------------------------ */
void ui_update_file_list_cb(void *data);   /* data = malloc'd char* */

#endif /* UI_MANAGER_H */
