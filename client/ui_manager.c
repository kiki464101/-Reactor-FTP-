/**
 * @file ui_manager.c
 * @brief LVGL UI Manager for FTP client
 *
 * Implements login screen, main file-manager screen, and a progress
 * overlay.  No LVGL widget is touched from outside the UI thread;
 * the network thread delivers data via lv_async_call().
 */

#include "ui_manager.h"
#include "network_task.h"
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SIZE 4096

/* Multi-selection state */
char g_selected_remote[MAX_SELECTED_FILES][256] = {{0}};
int  g_remote_sel_count = 0;
char g_selected_local[MAX_SELECTED_FILES][256]  = {{0}};
int  g_local_sel_count  = 0;

/* ------------------------------------------------------------------ */
/*  Screen objects                                                    */
/* ------------------------------------------------------------------ */
lv_obj_t *login_screen = NULL;
lv_obj_t *main_screen  = NULL;

/* ---- login widgets ---- */
static lv_obj_t *login_ip_ta;
static lv_obj_t *login_port_ta;
static lv_obj_t *login_user_ta;
static lv_obj_t *login_pass_ta;
static lv_obj_t *login_btn;
static lv_obj_t *login_status;
static lv_obj_t *login_title;
static lv_obj_t *login_spinner;

/* ---- main-screen widgets ---- */
static lv_obj_t *main_status_bar;
static lv_obj_t *main_file_list;      /* remote (server) file list */
static lv_obj_t *main_local_list;   /* local file list */

/* ---- selection highlight style ---- */
static lv_style_t style_selected_remote;
static lv_style_t style_selected_local;
static lv_obj_t *main_selected_label;

/* ---- progress overlay (child of active screen) ---- */
static lv_obj_t *prog_panel   = NULL;
static lv_obj_t *prog_label   = NULL;
static lv_obj_t *prog_bar     = NULL;
static lv_obj_t *prog_info    = NULL;
/* on-screen keyboard (shared) */
static lv_obj_t *kb = NULL;

static lv_obj_t *ui_get_window_parent(void)
{
    lv_obj_t *parent = lv_layer_top();
    if (!parent) parent = lv_scr_act();
    return parent;
}

/* ---- multi-transfer batch progress tracking ---- */
#define MAX_PROGRESS_BARS 10

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *label;
    char      filename[256];
    bool      is_upload;
    bool      active;
    bool      done;
} prog_slot_t;

static prog_slot_t prog_slots[MAX_PROGRESS_BARS];
static lv_obj_t   *batch_prog_panel = NULL;
static int          batch_prog_count = 0;

/* current local file browsing path */
static char g_local_cur_path[256] = {0};

/* ================================================================== */
/*  Forward declarations of event handlers                            */
/* ================================================================== */
static void on_login_btn_clicked(lv_event_t *e);
static void on_file_item_clicked(lv_event_t *e);
static void on_local_file_item_clicked(lv_event_t *e);
static void on_refresh_btn_clicked(lv_event_t *e);
static void on_download_btn_clicked(lv_event_t *e);
static void on_upload_btn_clicked(lv_event_t *e);
static void on_disconnect_btn_clicked(lv_event_t *e);
static void on_cancel_btn_clicked(lv_event_t *e);
static void on_close_progress_btn_clicked(lv_event_t *e);
static void on_ta_focused(lv_event_t *e);
static void on_keyboard_event(lv_event_t *e);

/* ================================================================== */
/*  Helper: create a simple button with text on a given parent        */
/* ================================================================== */
static lv_obj_t *create_btn(lv_obj_t *parent, const char *text,
                             lv_coord_t w, lv_coord_t h,
                             lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ================================================================== */
/*  Login screen                                                      */
/* ================================================================== */
#define BTN_W  160
#define BTN_H   40
#define TA_W   280
#define TA_H    36
#define LBL_H   22

void ui_login_init(void)
{
    if (login_screen) {
        lv_obj_clean(login_screen);
        lv_screen_load(login_screen);
        return;
    }

    login_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(login_screen, lv_color_hex(0x1a1a2e), 0);

    /* ---- title ---- */
    login_title = lv_label_create(login_screen);
    lv_label_set_text(login_title,
        "Embedded Remote File Manager\nLVGL FTP Client");
    lv_obj_set_style_text_align(login_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(login_title, lv_color_hex(0x00d2ff), 0);
    lv_obj_set_style_text_font(login_title, &lv_font_montserrat_20, 0);
    lv_obj_align(login_title, LV_ALIGN_TOP_MID, 0, 20);

    /* ---- form container ---- */
    lv_obj_t *cont = lv_obj_create(login_screen);
    lv_obj_set_size(cont, 380, 420);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    /* ---- IP ---- */
    lv_obj_t *lbl1 = lv_label_create(cont);
    lv_label_set_text(lbl1, "Server IP");
    lv_obj_set_style_text_color(lbl1, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_margin_top(lbl1, 6, 0);

    login_ip_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_ip_ta, TA_W, TA_H);
    lv_textarea_set_one_line(login_ip_ta, true);
    lv_textarea_set_cursor_click_pos(login_ip_ta, true);
    lv_textarea_set_max_length(login_ip_ta, 32);
    lv_textarea_set_placeholder_text(login_ip_ta, "127.0.0.1");
    lv_textarea_set_text(login_ip_ta, "127.0.0.1");
    lv_obj_add_event_cb(login_ip_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(login_ip_ta, on_ta_focused, LV_EVENT_CLICKED, NULL);

    /* ---- Port ---- */
    lv_obj_t *lbl2 = lv_label_create(cont);
    lv_label_set_text(lbl2, "Port");
    lv_obj_set_style_text_color(lbl2, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_margin_top(lbl2, 6, 0);

    login_port_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_port_ta, TA_W, TA_H);
    lv_textarea_set_one_line(login_port_ta, true);
    lv_textarea_set_cursor_click_pos(login_port_ta, true);
    lv_textarea_set_max_length(login_port_ta, 8);
    lv_textarea_set_placeholder_text(login_port_ta, "8888");
    lv_textarea_set_text(login_port_ta, "8888");
    lv_obj_add_event_cb(login_port_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(login_port_ta, on_ta_focused, LV_EVENT_CLICKED, NULL);

    /* ---- Username ---- */
    lv_obj_t *lbl3 = lv_label_create(cont);
    lv_label_set_text(lbl3, "Username");
    lv_obj_set_style_text_color(lbl3, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_margin_top(lbl3, 6, 0);

    login_user_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_user_ta, TA_W, TA_H);
    lv_textarea_set_one_line(login_user_ta, true);
    lv_textarea_set_cursor_click_pos(login_user_ta, true);
    lv_textarea_set_max_length(login_user_ta, 32);
    lv_textarea_set_placeholder_text(login_user_ta, "admin");
    lv_textarea_set_text(login_user_ta, "admin");
    lv_obj_add_event_cb(login_user_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(login_user_ta, on_ta_focused, LV_EVENT_CLICKED, NULL);

    /* ---- Password ---- */
    lv_obj_t *lbl4 = lv_label_create(cont);
    lv_label_set_text(lbl4, "Password");
    lv_obj_set_style_text_color(lbl4, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_margin_top(lbl4, 6, 0);

    login_pass_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_pass_ta, TA_W, TA_H);
    lv_textarea_set_one_line(login_pass_ta, true);
    lv_textarea_set_cursor_click_pos(login_pass_ta, true);
    lv_textarea_set_max_length(login_pass_ta, 32);
    lv_textarea_set_password_mode(login_pass_ta, true);
    lv_textarea_set_placeholder_text(login_pass_ta, "password");
    lv_textarea_set_text(login_pass_ta, "123456");
    lv_obj_add_event_cb(login_pass_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(login_pass_ta, on_ta_focused, LV_EVENT_CLICKED, NULL);

    /* ---- spacer ---- */
    lv_obj_t *sp = lv_obj_create(cont);
    lv_obj_set_size(sp, 1, 10);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    /* ---- Connect button ---- */
    login_btn = lv_button_create(cont);
    lv_obj_set_size(login_btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(login_btn, lv_color_hex(0x007bff), 0);
    lv_obj_set_style_radius(login_btn, 6, 0);
    lv_obj_t *btn_lbl = lv_label_create(login_btn);
    lv_label_set_text(btn_lbl, "Connect / Login");
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(login_btn, on_login_btn_clicked, LV_EVENT_CLICKED,
                         NULL);

    /* ---- Status ---- */
    login_status = lv_label_create(cont);
    lv_label_set_text(login_status, "Ready");
    lv_obj_set_style_text_color(login_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_margin_top(login_status, 6, 0);

    lv_screen_load(login_screen);
}

/* ================================================================== */
/*  Login button handler                                              */
/* ================================================================== */
static void on_login_btn_clicked(lv_event_t *e)
{
    (void)e;
    const char *ip   = lv_textarea_get_text(login_ip_ta);
    const char *port = lv_textarea_get_text(login_port_ta);
    const char *user = lv_textarea_get_text(login_user_ta);
    const char *pass = lv_textarea_get_text(login_pass_ta);

    if (strlen(ip) < 7 || strlen(port) < 1) {
        lv_label_set_text(login_status, "Please enter IP and Port");
        return;
    }

    lv_label_set_text(login_status, "Connecting...");
    lv_obj_set_style_text_color(login_status, lv_color_hex(0xffaa00), 0);

    /* disable button to prevent double-click */
    lv_obj_add_state(login_btn, LV_STATE_DISABLED);

    if (!network_start_connect(ip, port, user, pass)) {
        lv_label_set_text(login_status, "Failed to start connection thread");
        lv_obj_clear_state(login_btn, LV_STATE_DISABLED);
    }
}

/* ================================================================== */
/*  Main screen (file manager)                                        */
/* ================================================================== */
void ui_main_init(void)
{
    if (main_screen) {
        lv_obj_clean(main_screen);
        lv_screen_load(main_screen);
        return;
    }

    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x16213e), 0);

    /* ======== status bar ======== */
    main_status_bar = lv_label_create(main_screen);
    lv_obj_set_size(main_status_bar, LV_PCT(100), 28);
    lv_obj_set_style_bg_color(main_status_bar, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_bg_opa(main_status_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(main_status_bar, 10, 0);
    lv_obj_set_style_text_color(main_status_bar, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(main_status_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(main_status_bar, "Connecting...");

    /* init selection styles */
    lv_style_init(&style_selected_remote);
    lv_style_set_bg_color(&style_selected_remote, lv_color_hex(0x228B22));
    lv_style_set_bg_opa(&style_selected_remote, LV_OPA_COVER);
    lv_style_init(&style_selected_local);
    lv_style_set_bg_color(&style_selected_local, lv_color_hex(0x228B22));
    lv_style_set_bg_opa(&style_selected_local, LV_OPA_COVER);

    /* ======== path label ======== */
    lv_obj_t *path_lbl = lv_label_create(main_screen);
    lv_obj_set_style_text_color(path_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(path_lbl, LV_ALIGN_TOP_LEFT, 10, 32);
    lv_label_set_text(path_lbl, "Directory: /remote_share/");
    lv_obj_add_flag(path_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ======== remote file list (top half) ======== */
    lv_obj_t *remote_header = lv_label_create(main_screen);
    lv_label_set_text(remote_header, "Remote Files (server):");
    lv_obj_set_style_text_color(remote_header, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(remote_header, LV_ALIGN_TOP_LEFT, 10, 32);

    lv_obj_t *remote_cont = lv_obj_create(main_screen);
    lv_obj_set_size(remote_cont, LV_PCT(96), 200);
    lv_obj_align(remote_cont, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_color(remote_cont, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_border_color(remote_cont, lv_color_hex(0x334466), 0);
    lv_obj_set_style_radius(remote_cont, 6, 0);
    lv_obj_set_scrollbar_mode(remote_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(remote_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(remote_cont, LV_DIR_VER);

    main_file_list = lv_list_create(remote_cont);
    lv_obj_set_size(main_file_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_file_list, 0, 0);
    lv_obj_set_scrollbar_mode(main_file_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(main_file_list, LV_DIR_VER);
    lv_list_add_text(main_file_list, "Click Refresh");

    /* ======== local file list (bottom half, same size) ======== */
    lv_obj_t *local_header = lv_label_create(main_screen);
    lv_label_set_text(local_header, "Local Files (client):");
    lv_obj_set_style_text_color(local_header, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(local_header, LV_ALIGN_TOP_LEFT, 10, 258);

    lv_obj_t *local_cont = lv_obj_create(main_screen);
    lv_obj_set_size(local_cont, LV_PCT(96), 200);
    lv_obj_align(local_cont, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_set_style_bg_color(local_cont, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_border_color(local_cont, lv_color_hex(0x334466), 0);
    lv_obj_set_style_radius(local_cont, 6, 0);
    lv_obj_set_scrollbar_mode(local_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(local_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(local_cont, LV_DIR_VER);

    main_local_list = lv_list_create(local_cont);
    lv_obj_set_size(main_local_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_local_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_local_list, 0, 0);
    lv_obj_set_scrollbar_mode(main_local_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(main_local_list, LV_DIR_VER);
    lv_list_add_text(main_local_list, "Click Refresh");
    /* ======== selected file display ======== */
    main_selected_label = lv_label_create(main_screen);
    lv_obj_align(main_selected_label, LV_ALIGN_BOTTOM_LEFT, 10, -48);
    lv_obj_set_style_text_color(main_selected_label, lv_color_hex(0x88ccff), 0);
    lv_label_set_text(main_selected_label, "Remote: 0 | Local: 0");

    /* ======== action buttons row ======== */
    lv_obj_t *btn_row = lv_obj_create(main_screen);
    lv_obj_set_size(btn_row, LV_PCT(100), 46);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_row, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_left(btn_row, 4, 0);
    lv_obj_set_style_pad_right(btn_row, 4, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_btn(btn_row, "Refresh",    90, 34, on_refresh_btn_clicked);
    create_btn(btn_row, "Download",   90, 34, on_download_btn_clicked);
    create_btn(btn_row, "Upload",     90, 34, on_upload_btn_clicked);
    create_btn(btn_row, "Disconnect", 100, 34, on_disconnect_btn_clicked);

    /* load the screen (don't switch automatically — ui_switch_to_main does it) */
}

/* ================================================================== */
/*  Screen switching                                                  */
/* ================================================================== */
void ui_switch_to_login(void)
{
    if (!login_screen) ui_login_init();
    lv_screen_load(login_screen);
    if (login_status) {
        lv_label_set_text(login_status, "Disconnected");
        lv_obj_set_style_text_color(login_status, lv_color_hex(0xff6666), 0);
    }
    if (login_btn) lv_obj_clear_state(login_btn, LV_STATE_DISABLED);
}

void ui_switch_to_main(void)
{
    if (!main_screen) ui_main_init();

    /* update status bar */
    char sb[256];
    snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
             g_login_user[0] ? g_login_user : "admin",
             g_session_info[0] ? g_session_info : "N/A");
    if (main_status_bar) lv_label_set_text(main_status_bar, sb);

    /* request initial file list and scan local */
    lv_screen_load(main_screen);
    network_cmd_ls();
    ui_refresh_local_files();
}

/* ================================================================== */
/*  Progress overlay                                                  */
/* ================================================================== */

void ui_show_progress_batch(void)
{
    lv_obj_t *parent = ui_get_window_parent();
    if (!parent) return;

    /* don't duplicate */
    if (batch_prog_panel || prog_panel) return;

    memset(prog_slots, 0, sizeof(prog_slots));
    batch_prog_count = 0;

    batch_prog_panel = lv_obj_create(parent);
    lv_obj_set_size(batch_prog_panel, 340, 160);
    lv_obj_center(batch_prog_panel);
    lv_obj_set_style_bg_color(batch_prog_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(batch_prog_panel, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_border_width(batch_prog_panel, 2, 0);
    lv_obj_set_style_radius(batch_prog_panel, 10, 0);
    lv_obj_set_style_pad_all(batch_prog_panel, 12, 0);

    /* title: filename + percentage */
    prog_slots[0].label = lv_label_create(batch_prog_panel);
    lv_label_set_text(prog_slots[0].label, "Preparing transfer...");
    lv_obj_set_style_text_color(prog_slots[0].label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(prog_slots[0].label, &lv_font_montserrat_16, 0);
    lv_obj_align(prog_slots[0].label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* RED progress bar - tall and clearly visible */
    prog_slots[0].bar = lv_bar_create(batch_prog_panel);
    lv_obj_set_size(prog_slots[0].bar, 300, 30);
    lv_obj_align(prog_slots[0].bar, LV_ALIGN_CENTER, 0, 4);
    lv_bar_set_range(prog_slots[0].bar, 0, 100);
    lv_bar_set_value(prog_slots[0].bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(prog_slots[0].bar, lv_color_hex(0x331111), 0);
    lv_obj_set_style_bg_color(prog_slots[0].bar, lv_color_hex(0xCC0000), LV_PART_INDICATOR);
    lv_obj_set_style_radius(prog_slots[0].bar, 6, 0);
    lv_obj_set_style_anim_time(prog_slots[0].bar, 100, 0);

    /* info: file size */
    prog_slots[1].label = lv_label_create(batch_prog_panel);
    lv_label_set_text(prog_slots[1].label, "0 B / 0 B");
    lv_obj_set_style_text_color(prog_slots[1].label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(prog_slots[1].label, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Close button */
    lv_obj_t *close_btn = lv_button_create(batch_prog_panel);
    lv_obj_set_size(close_btn, 70, 26);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_obj_t *cbl = lv_label_create(close_btn);
    lv_label_set_text(cbl, "Close");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(close_btn, on_close_progress_btn_clicked, LV_EVENT_CLICKED, NULL);

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_button_create(batch_prog_panel);
    lv_obj_set_size(cancel_btn, 80, 26);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    lv_obj_t *cbl2 = lv_label_create(cancel_btn);
    lv_label_set_text(cbl2, "Cancel");
    lv_obj_center(cbl2);
    lv_obj_add_event_cb(cancel_btn, on_cancel_btn_clicked, LV_EVENT_CLICKED, NULL);

    batch_prog_count = 0;
}

void ui_update_transfer_progress(const char *filename, int percent,
                                  int current_bytes, int total_bytes,
                                  bool is_upload)
{
    if (!batch_prog_panel) return;

    /* slot 0: title label */
    if (prog_slots[0].label) {
        char title[300];
        snprintf(title, sizeof(title), "%s: %s  %d%%",
                 is_upload ? "Uploading" : "Downloading", filename, percent);
        lv_label_set_text(prog_slots[0].label, title);
    }

    /* slot 0: bar */
    if (prog_slots[0].bar) {
        lv_bar_set_value(prog_slots[0].bar, percent, LV_ANIM_ON);
    }

    /* slot 1: info label */
    if (prog_slots[1].label) {
        char buf[128];
        if (total_bytes >= 1048576)
            snprintf(buf, sizeof(buf), "%.1f / %.1f MB",
                     current_bytes / 1048576.0f, total_bytes / 1048576.0f);
        else if (total_bytes >= 1024)
            snprintf(buf, sizeof(buf), "%.1f / %.1f KB",
                     current_bytes / 1024.0f, total_bytes / 1024.0f);
        else
            snprintf(buf, sizeof(buf), "%d / %d B", current_bytes, total_bytes);
        lv_label_set_text(prog_slots[1].label, buf);
    }
}

void ui_on_transfer_done(const char *filename, bool success, bool is_upload)
{
    if (batch_prog_panel && prog_slots[0].label) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[%s] %s - %s",
                 is_upload ? "UP" : "DL", filename,
                 success ? "DONE" : "FAILED");
        lv_label_set_text(prog_slots[0].label, buf);
        lv_bar_set_value(prog_slots[0].bar, success ? 100 : 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(prog_slots[0].bar,
            success ? lv_color_hex(0x00CC00) : lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
}

void ui_show_progress(const char *filename, bool is_upload)
{
    lv_obj_t *parent = ui_get_window_parent();
    if (!parent) return;

    /* clean up existing overlay */
    if (prog_panel) { lv_obj_del(prog_panel); prog_panel = NULL; }

    prog_panel = lv_obj_create(parent);
    lv_obj_set_size(prog_panel, 320, 150);
    lv_obj_center(prog_panel);
    lv_obj_set_style_bg_color(prog_panel, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_color(prog_panel, lv_color_hex(0x4488cc), 0);
    lv_obj_set_style_border_width(prog_panel, 2, 0);
    lv_obj_set_style_radius(prog_panel, 10, 0);
    lv_obj_set_style_pad_all(prog_panel, 12, 0);

    prog_label = lv_label_create(prog_panel);
    char title[300];
    snprintf(title, sizeof(title), "%s: %s",
             is_upload ? "Uploading" : "Downloading", filename);
    lv_label_set_text(prog_label, title);
    lv_obj_set_style_text_color(prog_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(prog_label, LV_ALIGN_TOP_LEFT, 0, 0);

    prog_bar = lv_bar_create(prog_panel);
    lv_obj_set_size(prog_bar, 280, 20);
    lv_obj_align(prog_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(prog_bar, 0, 100);
    lv_bar_set_value(prog_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(prog_bar, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(prog_bar, lv_color_hex(0xCC0000), LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(prog_bar, 200, 0);

    prog_info = lv_label_create(prog_panel);
    lv_label_set_text(prog_info, "0 B / 0 B");
    lv_obj_set_style_text_color(prog_info, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(prog_info, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Close button (just hides, transfer continues) */
    lv_obj_t *close_btn = lv_button_create(prog_panel);
    lv_obj_set_size(close_btn, 70, 26);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_t *cbl_close = lv_label_create(close_btn);
    lv_label_set_text(cbl_close, "Close");
    lv_obj_center(cbl_close);
    lv_obj_add_event_cb(close_btn, on_close_progress_btn_clicked, LV_EVENT_CLICKED, NULL);

    /* Cancel button (stops transfer + hides popup) */
    lv_obj_t *cancel_btn = lv_button_create(prog_panel);
    lv_obj_set_size(cancel_btn, 70, 26);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
    lv_obj_t *cbl = lv_label_create(cancel_btn);
    lv_label_set_text(cbl, "Cancel");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(cancel_btn, on_cancel_btn_clicked, LV_EVENT_CLICKED, NULL);

}

void ui_hide_progress(void)
{
    if (batch_prog_panel) {
        lv_obj_del(batch_prog_panel);
        batch_prog_panel = NULL;
        memset(prog_slots, 0, sizeof(prog_slots));
        batch_prog_count = 0;
    }
    if (prog_panel) {
        lv_obj_del(prog_panel);
        prog_panel = NULL;
    }
    prog_label = NULL;
    prog_bar   = NULL;
    prog_info  = NULL;
}

void ui_update_progress(int percent, int current_bytes, int total_bytes,
                        const char *filename, bool is_upload)
{
    /* update bar */
    if (prog_bar) lv_bar_set_value(prog_bar, percent, LV_ANIM_ON);

    /* update info text */
    if (prog_info) {
        char buf[128];
        if (total_bytes >= 1048576)
            snprintf(buf, sizeof(buf), "%.1f MB / %.1f MB",
                     current_bytes / 1048576.0f, total_bytes / 1048576.0f);
        else if (total_bytes >= 1024)
            snprintf(buf, sizeof(buf), "%.1f KB / %.1f KB",
                     current_bytes / 1024.0f, total_bytes / 1024.0f);
        else
            snprintf(buf, sizeof(buf), "%d B / %d B",
                     current_bytes, total_bytes);
        lv_label_set_text(prog_info, buf);
    }

    if (prog_label && filename) {
        char title[300];
        snprintf(title, sizeof(title), "%s: %s  %d%%",
                 is_upload ? "Uploading" : "Downloading", filename, percent);
        lv_label_set_text(prog_label, title);
    }
}

/* ================================================================== */
/*  Status & error helpers                                            */
/* ================================================================== */

void ui_set_status(const char *msg)
{
    if (main_status_bar) {
        lv_label_set_text(main_status_bar, msg);
        lv_obj_set_style_text_color(main_status_bar, lv_color_hex(0x88ff88), 0);
    }
}

void ui_show_error(const char *msg)
{
    if (main_status_bar) {
        lv_label_set_text(main_status_bar, msg);
        lv_obj_set_style_text_color(main_status_bar, lv_color_hex(0xff6666), 0);
    } else if (login_status) {
        lv_label_set_text(login_status, msg);
        lv_obj_set_style_text_color(login_status, lv_color_hex(0xff6666), 0);
    }

    /* re-enable login button if on login screen */
    if (login_btn && lv_scr_act() == login_screen) {
        lv_obj_clear_state(login_btn, LV_STATE_DISABLED);
    }
}

/* ================================================================== */
/*  Event handlers ?main screen buttons                              */
/* ================================================================== */

static void on_file_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn || !main_file_list) return;

    const char *text = lv_list_get_button_text(main_file_list, btn);
    if (!text) return;

    /* check if already selected (toggle off) */
    for (int i = 0; i < g_remote_sel_count; i++) {
        if (strcmp(g_selected_remote[i], text) == 0) {
            lv_obj_remove_style(btn, &style_selected_remote, 0);
            for (int j = i; j < g_remote_sel_count - 1; j++) {
                strncpy(g_selected_remote[j], g_selected_remote[j + 1],
                        sizeof(g_selected_remote[j]) - 1);
            }
            g_remote_sel_count--;
            goto update_label;
        }
    }

    /* toggle on */
    if (g_remote_sel_count >= MAX_SELECTED_FILES) return;

    lv_obj_add_style(btn, &style_selected_remote, 0);
    strncpy(g_selected_remote[g_remote_sel_count], text,
            sizeof(g_selected_remote[g_remote_sel_count]) - 1);
    g_remote_sel_count++;

update_label:
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d | Local: %d",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
}

static void on_refresh_btn_clicked(lv_event_t *e)
{
    (void)e;
    ui_set_status("Refreshing...");
    g_local_cur_path[0] = '\0';  /* reset to top-level */
    network_cmd_ls();
    ui_refresh_local_files();
    /* immediately restore status bar */
    if (g_login_ok) {
        char sb[256];
        snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
                 g_login_user[0] ? g_login_user : "admin",
                 g_session_info[0] ? g_session_info : "N/A");
        ui_set_status(sb);
    }
}

static void on_download_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_remote_sel_count == 0) {
        ui_show_error("No file selected");
        return;
    }

    const char *files[MAX_SELECTED_FILES];
    for (int i = 0; i < g_remote_sel_count; i++)
        files[i] = g_selected_remote[i];

    ui_set_status("Downloading...");
    ui_show_progress_batch(); /* show popup synchronously on UI thread */
    if (!network_cmd_get_multi(files, g_remote_sel_count))
        ui_show_error("Failed to start download");
}

static void on_upload_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_local_sel_count == 0) {
        ui_show_error("No file selected");
        return;
    }

    const char *files[MAX_SELECTED_FILES];
    for (int i = 0; i < g_local_sel_count; i++)
        files[i] = g_selected_local[i];

    ui_set_status("Uploading...");
    ui_show_progress_batch(); /* show popup synchronously on UI thread */
    if (!network_cmd_put_multi(files, g_local_sel_count))
        ui_show_error("No valid files to upload");
}

static void on_disconnect_btn_clicked(lv_event_t *e)
{
    (void)e;
    ui_set_status("Disconnecting...");
    network_disconnect();
}



/* ================================================================== */
/*  Error popup handler                                               */
/* ================================================================== */

static lv_obj_t *err_popup = NULL;
static lv_obj_t *err_label = NULL;

static void on_close_error_popup_btn_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *popup = lv_obj_get_parent(btn);
    lv_obj_del(popup);
    err_popup = NULL;
    err_label = NULL;
}

void ui_show_error_popup(const char *msg)
{
    lv_obj_t *parent = ui_get_window_parent();
    if (!parent) return;

    /* If a popup already exists and is still valid, just update its text */
    if (err_popup && lv_obj_is_valid(err_popup)) {
        if (err_label) lv_label_set_text(err_label, msg);
        return;
    }

    err_popup = lv_obj_create(parent);
    lv_obj_set_size(err_popup, 280, 150);
    lv_obj_center(err_popup);
    lv_obj_set_style_bg_color(err_popup, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_color(err_popup, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_border_width(err_popup, 2, 0);
    lv_obj_set_style_radius(err_popup, 8, 0);
    lv_obj_set_style_pad_all(err_popup, 15, 0);

    /* use flex column so label and button stack without overlapping */
    lv_obj_set_flex_flow(err_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(err_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    err_label = lv_label_create(err_popup);
    lv_label_set_text(err_label, msg);
    lv_obj_set_style_text_color(err_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(err_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(err_label, LV_LABEL_LONG_WRAP);  /* allow text to wrap */
    lv_obj_set_width(err_label, 240);                        /* limit width to force wrapping */

    lv_obj_t *close_btn = lv_button_create(err_popup);
    lv_obj_set_size(close_btn, 80, 28);
    lv_obj_t *cbl = lv_label_create(close_btn);
    lv_label_set_text(cbl, "Close");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(close_btn, on_close_error_popup_btn_clicked, LV_EVENT_CLICKED, NULL);
}
static void on_close_progress_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (batch_prog_panel) {
        lv_obj_del(batch_prog_panel);
        batch_prog_panel = NULL;
        memset(prog_slots, 0, sizeof(prog_slots));
        batch_prog_count = 0;
    }
    if (prog_panel) {
        lv_obj_del(prog_panel);
        prog_panel = NULL;
        prog_label = NULL;
        prog_bar   = NULL;
        prog_info  = NULL;
    }
}

static void on_cancel_btn_clicked(lv_event_t *e)
{
    (void)e;
    network_cancel_transfer();
    ui_hide_progress();
    ui_show_error("Transfer cancelled");
}

/* Show an on-screen keyboard when a textarea gains focus or is clicked. */
static void on_ta_focused(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (!ta) return;

    if (!kb) {
        /* create keyboard as a child of the top layer so it appears above screens */
        lv_obj_t *parent = lv_layer_top();
        kb = lv_keyboard_create(parent);
        lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    /* attach keyboard to this textarea and show it */
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

/* Keyboard events: hide keyboard on cancel/ready and detach textarea */
static void on_keyboard_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        if (kb) {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ================================================================== */
/*  Async callback ?update file list from network thread             */
/* ================================================================== */
void ui_update_file_list_cb(void *data)
{
    char *filelist = (char *)data;
    if (!main_file_list) { free(filelist); return; }

    /* clear old items */
    lv_obj_clean(main_file_list);

    if (!filelist || strlen(filelist) == 0) {
        lv_list_add_text(main_file_list, "(empty directory)");
        free(filelist);
        return;
    }

    /* parse newline-separated entries and add buttons */
    char *save;
    char *token = strtok_r(filelist, "\n", &save);
    while (token) {
        /* trim trailing whitespace / carriage return */
        size_t tlen = strlen(token);
        while (tlen > 0 && (token[tlen-1] == '\r' || token[tlen-1] == ' '))
            token[--tlen] = '\0';
        if (tlen == 0) { token = strtok_r(NULL, "\n", &save); continue; }

        /* skip "." and ".." entries from server directory listing */
        if (strcmp(token, ".") == 0 || strcmp(token, "..") == 0) {
            token = strtok_r(NULL, "\n", &save);
            continue;
        }

        lv_obj_t *btn = lv_list_add_button(main_file_list, NULL, token);
        lv_obj_add_event_cb(btn, on_file_item_clicked, LV_EVENT_CLICKED,
                             NULL);
        token = strtok_r(NULL, "\n", &save);
    }

    free(filelist);
    g_remote_sel_count = 0;
    memset(g_selected_remote, 0, sizeof(g_selected_remote));
}

/* ================================================================== */
/*  Cleanup (optional)                                                */
/* ================================================================== */


/* ================================================================== */
/*  Local file helpers                                                 */
/* ================================================================== */

static char *scan_local_directory(const char *subpath)
{
    char *buf = (char *)malloc(SIZE);
    if (!buf) return NULL;
    int off = 0;
    buf[0] = '\0';

    char full[520];
    if (subpath && subpath[0])
        snprintf(full, sizeof(full), "./client/%s", subpath);
    else
        snprintf(full, sizeof(full), "./client");

    DIR *dir = opendir(full);
    if (!dir) {
        free(buf);
        return NULL;
    }

    /* ".." entry if inside a subdirectory */
    if (subpath && subpath[0])
        off += snprintf(buf + off, (size_t)(SIZE - off - 1), "..\n");

    struct dirent *d;
    while ((d = readdir(dir)) != NULL) {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;

        char item_path[560];
        snprintf(item_path, sizeof(item_path), "%s/%s", full, d->d_name);
        struct stat st;
        if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            /* directory: show with "/" suffix */
            off += snprintf(buf + off, (size_t)(SIZE - off - 1),
                            "%s/\n", d->d_name);
        } else {
            /* file: show normally */
            off += snprintf(buf + off, (size_t)(SIZE - off - 1),
                            "%s\n", d->d_name);
        }
    }
    closedir(dir);
    return buf;
}

void ui_refresh_local_files(void)
{
    char *filelist = scan_local_directory(g_local_cur_path);
    lv_async_call(ui_update_local_file_list_cb, filelist); /* NULL is handled inside cb */
}

void ui_update_local_file_list_cb(void *data)
{
    char *filelist = (char *)data;
    if (!main_local_list) { free(filelist); return; }

    lv_obj_clean(main_local_list);

    /* handle NULL (directory open failed) — show "Unable file", then return to parent */
    if (!filelist) {
        lv_list_add_text(main_local_list, "(unable to open)");
        ui_show_error_popup("Unable file");
        /* return to parent directory */
        char *slash = strrchr(g_local_cur_path, '/');
        if (slash) *slash = '\0';
        else g_local_cur_path[0] = '\0';
        ui_refresh_local_files();
        return;
    }

    /* show current path in a header text */
    {
        char header[320];
        snprintf(header, sizeof(header), "Local: ./client/%s",
                 g_local_cur_path[0] ? g_local_cur_path : "");
        lv_list_add_text(main_local_list, header);
    }

    if (!filelist || strlen(filelist) == 0) {
        lv_list_add_text(main_local_list, "(empty directory)");
        free(filelist);
        return;
    }

    char *save;
    char *token = strtok_r(filelist, "\n", &save);
    while (token) {
        size_t tlen = strlen(token);
        while (tlen > 0 && (token[tlen-1] == '\r' || token[tlen-1] == ' '))
            token[--tlen] = '\0';
        if (tlen == 0) { token = strtok_r(NULL, "\n", &save); continue; }

        lv_obj_t *btn = lv_list_add_button(main_local_list, NULL, token);
        lv_obj_add_event_cb(btn, on_local_file_item_clicked, LV_EVENT_CLICKED, NULL);
        token = strtok_r(NULL, "\n", &save);
    }
    free(filelist);
    g_local_sel_count = 0;
    memset(g_selected_local, 0, sizeof(g_selected_local));
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d | Local: %d",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
}

static void on_local_file_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn || !main_local_list) return;

    const char *text = lv_list_get_button_text(main_local_list, btn);
    if (!text) return;

    size_t tlen = strlen(text);

    /* ".." entry: navigate to parent directory */
    if (strcmp(text, "..") == 0) {
        char *slash = strrchr(g_local_cur_path, '/');
        if (slash) *slash = '\0';
        else g_local_cur_path[0] = '\0';
        ui_refresh_local_files();
        return;
    }

    /* directory (ends with "/"): navigate into it */
    if (tlen > 0 && text[tlen - 1] == '/') {
        char dirname[256];
        strncpy(dirname, text, sizeof(dirname) - 1);
        dirname[sizeof(dirname) - 1] = '\0';
        if (tlen < sizeof(dirname)) dirname[tlen - 1] = '\0'; /* strip trailing / */

        if (g_local_cur_path[0])
            snprintf(g_local_cur_path, sizeof(g_local_cur_path),
                     "%s/%s", g_local_cur_path, dirname);
        else
            strncpy(g_local_cur_path, dirname, sizeof(g_local_cur_path) - 1);

        ui_refresh_local_files();
        return;
    }

    /* regular file: toggle selection */

    /* Build the full relative path for comparison/storage so that
     * subdirectory context is preserved (e.g. "load/haha.txt"). */
    char full_path[256];
    if (g_local_cur_path[0])
        snprintf(full_path, sizeof(full_path), "%s/%s", g_local_cur_path, text);
    else
        strncpy(full_path, text, sizeof(full_path) - 1);

    for (int i = 0; i < g_local_sel_count; i++) {
        if (strcmp(g_selected_local[i], full_path) == 0) {
            lv_obj_remove_style(btn, &style_selected_local, 0);
            for (int j = i; j < g_local_sel_count - 1; j++) {
                strncpy(g_selected_local[j], g_selected_local[j + 1],
                        sizeof(g_selected_local[j]) - 1);
            }
            g_local_sel_count--;
            goto update_label;
        }
    }

    /* toggle on */
    if (g_local_sel_count >= MAX_SELECTED_FILES) return;

    lv_obj_add_style(btn, &style_selected_local, 0);
    strncpy(g_selected_local[g_local_sel_count], full_path,
            sizeof(g_selected_local[g_local_sel_count]) - 1);
    g_local_sel_count++;

update_label:
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d | Local: %d",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
}

/* ================================================================== */
/*  Status restore after transfer                                     */
/* ================================================================== */

static void restore_status_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    if (g_login_ok) {
        char sb[256];
        snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
                 g_login_user[0] ? g_login_user : "admin",
                 g_session_info[0] ? g_session_info : "N/A");
        ui_set_status(sb);
    }
}

void ui_restore_status_after_delay(void)
{
    lv_timer_t *t = lv_timer_create(restore_status_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void ui_cleanup(void)
{
    if (login_screen) { lv_obj_del(login_screen); login_screen = NULL; }
    if (main_screen)  { lv_obj_del(main_screen);  main_screen  = NULL; }
    ui_hide_progress();
}
