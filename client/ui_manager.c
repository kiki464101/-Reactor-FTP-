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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static lv_obj_t *main_file_list;
static lv_obj_t *main_upload_ta;
static lv_obj_t *main_selected_label;   /* shows which file is selected */

/* ---- progress overlay (child of active screen) ---- */
static lv_obj_t *prog_panel   = NULL;
static lv_obj_t *prog_label   = NULL;
static lv_obj_t *prog_bar     = NULL;
static lv_obj_t *prog_info    = NULL;

/* ---- transfer-in-progress flag (prevent double-click) ---- */
static bool g_transferring = false;

/* ---- last selected filename ---- */
static char g_selected_file[256] = {0};

/* ================================================================== */
/*  Forward declarations of event handlers                            */
/* ================================================================== */
static void on_login_btn_clicked(lv_event_t *e);
static void on_file_item_clicked(lv_event_t *e);
static void on_refresh_btn_clicked(lv_event_t *e);
static void on_download_btn_clicked(lv_event_t *e);
static void on_upload_btn_clicked(lv_event_t *e);
static void on_disconnect_btn_clicked(lv_event_t *e);
static void on_cancel_btn_clicked(lv_event_t *e);

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
    lv_obj_align(login_title, LV_ALIGN_TOP_MID, 0, 30);

    lv_coord_t cx;  /* screen centre x */
    lv_coord_t sy;  /* current y offset */
    lv_obj_t *cont = lv_obj_create(login_screen);
    lv_obj_set_size(cont, 340, 360);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* ---- IP ---- */
    lv_obj_t *lbl1 = lv_label_create(cont);
    lv_label_set_text(lbl1, "Server IP");
    lv_obj_set_style_text_color(lbl1, lv_color_hex(0xcccccc), 0);

    login_ip_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_ip_ta, TA_W, TA_H);
    lv_textarea_set_placeholder_text(login_ip_ta, "192.168.1.100");
    lv_textarea_set_text(login_ip_ta, "192.168.1.100");

    /* ---- Port ---- */
    lv_obj_t *lbl2 = lv_label_create(cont);
    lv_label_set_text(lbl2, "Port");
    lv_obj_set_style_text_color(lbl2, lv_color_hex(0xcccccc), 0);

    login_port_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_port_ta, TA_W, TA_H);
    lv_textarea_set_placeholder_text(login_port_ta, "8888");
    lv_textarea_set_text(login_port_ta, "8888");

    /* ---- Username ---- */
    lv_obj_t *lbl3 = lv_label_create(cont);
    lv_label_set_text(lbl3, "Username");
    lv_obj_set_style_text_color(lbl3, lv_color_hex(0xcccccc), 0);

    login_user_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_user_ta, TA_W, TA_H);
    lv_textarea_set_placeholder_text(login_user_ta, "admin");
    lv_textarea_set_text(login_user_ta, "admin");

    /* ---- Password ---- */
    lv_obj_t *lbl4 = lv_label_create(cont);
    lv_label_set_text(lbl4, "Password");
    lv_obj_set_style_text_color(lbl4, lv_color_hex(0xcccccc), 0);

    login_pass_ta = lv_textarea_create(cont);
    lv_obj_set_size(login_pass_ta, TA_W, TA_H);
    lv_textarea_set_placeholder_text(login_pass_ta, "password");
    lv_textarea_set_text(login_pass_ta, "123456");
    lv_textarea_set_password_mode(login_pass_ta, true);

    /* small spacer */
    lv_obj_t *sp = lv_obj_create(cont);
    lv_obj_set_size(sp, 1, 8);
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

    /* ======== path label ======== */
    lv_obj_t *path_lbl = lv_label_create(main_screen);
    lv_obj_set_style_text_color(path_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(path_lbl, LV_ALIGN_TOP_LEFT, 10, 32);
    lv_label_set_text(path_lbl, "Directory: /remote_share/");

    /* ======== file list ======== */
    lv_coord_t list_h = 280;
    lv_obj_t *list_cont = lv_obj_create(main_screen);
    lv_obj_set_size(list_cont, LV_PCT(96), list_h);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_border_color(list_cont, lv_color_hex(0x334466), 0);
    lv_obj_set_style_radius(list_cont, 6, 0);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);

    main_file_list = lv_list_create(list_cont);
    lv_obj_set_size(main_file_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_file_list, 0, 0);
    lv_list_add_text(main_file_list, "Click \"Refresh\" to list files");

    /* ======== selected file display ======== */
    main_selected_label = lv_label_create(main_screen);
    lv_obj_align(main_selected_label, LV_ALIGN_BOTTOM_LEFT, 10, -48);
    lv_obj_set_style_text_color(main_selected_label, lv_color_hex(0x88ccff), 0);
    lv_label_set_text(main_selected_label, "Selected: (none)");

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

    create_btn(btn_row, "Refresh",  100, 34, on_refresh_btn_clicked);
    create_btn(btn_row, "Download", 100, 34, on_download_btn_clicked);
    create_btn(btn_row, "Disconnect", 110, 34, on_disconnect_btn_clicked);

    /* ======== upload row ======== */
    lv_obj_t *upload_row = lv_obj_create(main_screen);
    lv_obj_set_size(upload_row, LV_PCT(100), 44);
    lv_obj_align(upload_row, LV_ALIGN_BOTTOM_LEFT, 0, -46);
    lv_obj_set_style_bg_color(upload_row, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_border_width(upload_row, 0, 0);
    lv_obj_set_flex_flow(upload_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(upload_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ul_lbl = lv_label_create(upload_row);
    lv_label_set_text(ul_lbl, "Upload:");
    lv_obj_set_style_text_color(ul_lbl, lv_color_hex(0xaaaaaa), 0);

    main_upload_ta = lv_textarea_create(upload_row);
    lv_obj_set_size(main_upload_ta, 180, 32);
    lv_textarea_set_placeholder_text(main_upload_ta, "local_file.txt");
    lv_obj_set_style_pad_left(main_upload_ta, 4, 0);

    create_btn(upload_row, "Upload", 90, 32, on_upload_btn_clicked);

    /* load the screen (don't switch automatically â€?ui_switch_to_main does it) */
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
    g_transferring = false;
}

void ui_switch_to_main(void)
{
    if (!main_screen) ui_main_init();

    /* update status bar */
    char sb[128];
    snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
             lv_textarea_get_text(login_user_ta), g_session_info);
    if (main_status_bar) lv_label_set_text(main_status_bar, sb);

    /* request initial file list */
    lv_screen_load(main_screen);
    network_cmd_ls();
}

/* ================================================================== */
/*  Progress overlay                                                  */
/* ================================================================== */
void ui_show_progress(const char *filename, bool is_upload)
{
    lv_obj_t *parent = lv_layer_top();
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
    lv_obj_set_style_anim_time(prog_bar, 200, 0);

    prog_info = lv_label_create(prog_panel);
    lv_label_set_text(prog_info, "0 B / 0 B");
    lv_obj_set_style_text_color(prog_info, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(prog_info, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_button_create(prog_panel);
    lv_obj_set_size(cancel_btn, 70, 26);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
    lv_obj_t *cbl = lv_label_create(cancel_btn);
    lv_label_set_text(cbl, "Cancel");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(cancel_btn, on_cancel_btn_clicked, LV_EVENT_CLICKED,
                         NULL);

    g_transferring = true;
}

void ui_hide_progress(void)
{
    if (prog_panel) {
        lv_obj_del(prog_panel);
        prog_panel = NULL;
    }
    prog_label = NULL;
    prog_bar   = NULL;
    prog_info  = NULL;
    g_transferring = false;
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
/*  Event handlers â€?main screen buttons                              */
/* ================================================================== */

static void on_file_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn || !main_file_list) return;

    const char *text = lv_list_get_button_text(main_file_list, btn);
    if (text) {
        strncpy(g_selected_file, text, sizeof(g_selected_file) - 1);
        char buf[280];
        snprintf(buf, sizeof(buf), "Selected: %s", text);
        lv_label_set_text(main_selected_label, buf);
    }
}

static void on_refresh_btn_clicked(lv_event_t *e)
{
    (void)e;
    ui_set_status("Refreshing...");
    network_cmd_ls();
}

static void on_download_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_transferring) {
        ui_show_error("Transfer already in progress");
        return;
    }
    if (strlen(g_selected_file) == 0) {
        ui_show_error("No file selected");
        return;
    }
    ui_set_status("Downloading...");
    network_cmd_get(g_selected_file);
}

static void on_upload_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_transferring) {
        ui_show_error("Transfer already in progress");
        return;
    }
    const char *fname = lv_textarea_get_text(main_upload_ta);
    if (!fname || strlen(fname) == 0) {
        ui_show_error("Enter a filename to upload");
        return;
    }
    ui_set_status("Uploading...");
    network_cmd_put(fname);
}

static void on_disconnect_btn_clicked(lv_event_t *e)
{
    (void)e;
    ui_set_status("Disconnecting...");
    network_disconnect();
}

static void on_cancel_btn_clicked(lv_event_t *e)
{
    (void)e;
    network_cancel_transfer();
}

/* ================================================================== */
/*  Async callback â€?update file list from network thread             */
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

        lv_obj_t *btn = lv_list_add_button(main_file_list, NULL, token);
        lv_obj_add_event_cb(btn, on_file_item_clicked, LV_EVENT_CLICKED,
                             NULL);
        token = strtok_r(NULL, "\n", &save);
    }

    free(filelist);
    g_selected_file[0] = '\0';
}

/* ================================================================== */
/*  Cleanup (optional)                                                */
/* ================================================================== */
void ui_cleanup(void)
{
    if (login_screen) { lv_obj_del(login_screen); login_screen = NULL; }
    if (main_screen)  { lv_obj_del(main_screen);  main_screen  = NULL; }
    ui_hide_progress();
}
