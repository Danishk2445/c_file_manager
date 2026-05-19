#include "fileops.h"
#include "view.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

/* ---------------- internal clipboard state ----------------
 * Single-window app, so static state is fine. We also mirror onto
 * the GTK clipboard with the standard "x-special/gnome-copied-files"
 * target so cut/copy interoperates with other GNOME file managers. */
static GList   *g_clip_paths = NULL;   /* GList<char*> owned */
static gboolean g_clip_is_cut = FALSE;

static void clip_state_clear(void) {
    g_list_free_full(g_clip_paths, g_free);
    g_clip_paths = NULL;
}

static void clip_state_set(GList *paths, gboolean cut) {
    clip_state_clear();
    for (GList *l = paths; l; l = l->next)
        g_clip_paths = g_list_prepend(g_clip_paths, g_strdup(l->data));
    g_clip_paths = g_list_reverse(g_clip_paths);
    g_clip_is_cut = cut;
}

/* ---------------- helpers ---------------- */

static void show_error(FmWindow *w, const char *prefix, GError *err) {
    char *msg = g_strdup_printf("%s: %s", prefix, err ? err->message : "unknown");
    fm_window_set_status(w, msg);
    g_free(msg);
    g_clear_error(&err);
}

static GtkWidget *prompt_string(FmWindow *w, const char *title,
                                const char *label_text, const char *initial) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(title,
        GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK",     GTK_RESPONSE_OK,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 6);

    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    if (initial) gtk_entry_set_text(GTK_ENTRY(entry), initial);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(dlg), "fm-entry", entry);
    gtk_widget_show_all(content);
    return dlg;
}

/* ---------------- open ---------------- */

void fm_fileops_open(FmWindow *w, const char *path) {
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri) {
        fm_window_set_status(w, "Could not build URI for file");
        return;
    }
    GError *err = NULL;
    if (!gtk_show_uri_on_window(GTK_WINDOW(w->window), uri,
                                GDK_CURRENT_TIME, &err)) {
        show_error(w, "Open failed", err);
    }
    g_free(uri);
}

/* ---------------- clipboard cut/copy/paste ---------------- */

void fm_fileops_copy_to_clipboard(FmWindow *w, gboolean cut) {
    GList *sel = fm_view_get_selected_paths(w);
    if (!sel) {
        fm_window_set_status(w, cut ? "Nothing to cut" : "Nothing to copy");
        return;
    }
    clip_state_set(sel, cut);

    /* Mirror onto the system clipboard in the Nautilus format. */
    GString *payload = g_string_new(cut ? "cut" : "copy");
    for (GList *l = sel; l; l = l->next) {
        char *uri = g_filename_to_uri(l->data, NULL, NULL);
        if (uri) {
            g_string_append_c(payload, '\n');
            g_string_append(payload, uri);
            g_free(uri);
        }
    }
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, payload->str, -1);
    g_string_free(payload, TRUE);

    int n = g_list_length(sel);
    char *msg = g_strdup_printf("%s %d item%s",
                                cut ? "Cut" : "Copied",
                                n, n == 1 ? "" : "s");
    fm_window_set_status(w, msg);
    g_free(msg);
    g_list_free_full(sel, g_free);
}

void fm_fileops_paste(FmWindow *w) {
    if (!g_clip_paths) {
        fm_window_set_status(w, "Clipboard is empty");
        return;
    }
    if (!w->current_path) return;

    int ok_count = 0, fail_count = 0;
    GFile *dest_dir = g_file_new_for_path(w->current_path);

    for (GList *l = g_clip_paths; l; l = l->next) {
        const char *src_path = l->data;
        GFile *src = g_file_new_for_path(src_path);
        char *base = g_file_get_basename(src);
        GFile *dest = g_file_get_child(dest_dir, base);
        g_free(base);

        GError *err = NULL;
        gboolean ok;
        if (g_clip_is_cut)
            ok = g_file_move(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        else
            ok = g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        if (ok) ok_count++; else { fail_count++; g_clear_error(&err); }

        g_object_unref(src);
        g_object_unref(dest);
    }
    g_object_unref(dest_dir);

    if (g_clip_is_cut) clip_state_clear();

    char *msg = g_strdup_printf("Pasted %d item%s%s%s",
                                ok_count, ok_count == 1 ? "" : "s",
                                fail_count ? ", " : "",
                                fail_count ? "some failed" : "");
    fm_window_set_status(w, msg);
    g_free(msg);

    fm_window_refresh(w);
}

/* ---------------- trash ---------------- */

void fm_fileops_trash_selected(FmWindow *w) {
    GList *sel = fm_view_get_selected_paths(w);
    if (!sel) {
        fm_window_set_status(w, "Nothing selected");
        return;
    }
    int ok = 0, fail = 0;
    for (GList *l = sel; l; l = l->next) {
        GFile *f = g_file_new_for_path(l->data);
        GError *err = NULL;
        if (g_file_trash(f, NULL, &err)) ok++;
        else { fail++; g_clear_error(&err); }
        g_object_unref(f);
    }
    g_list_free_full(sel, g_free);

    char *msg = g_strdup_printf("Moved %d to Trash%s%s",
                                ok, fail ? ", " : "",
                                fail ? "some failed" : "");
    fm_window_set_status(w, msg);
    g_free(msg);
    fm_window_refresh(w);
}

/* ---------------- rename ---------------- */

void fm_fileops_rename_selected(FmWindow *w) {
    GList *sel = fm_view_get_selected_paths(w);
    if (!sel) {
        fm_window_set_status(w, "Nothing selected");
        return;
    }
    char *target = g_strdup(sel->data);
    g_list_free_full(sel, g_free);

    char *base = g_path_get_basename(target);
    GtkWidget *dlg = prompt_string(w, "Rename", "New name:", base);
    g_free(base);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GtkEntry *entry = g_object_get_data(G_OBJECT(dlg), "fm-entry");
        const char *new_name = gtk_entry_get_text(entry);
        if (new_name && *new_name) {
            GFile *src = g_file_new_for_path(target);
            GError *err = NULL;
            GFile *res = g_file_set_display_name(src, new_name, NULL, &err);
            if (res) g_object_unref(res);
            else show_error(w, "Rename failed", err);
            g_object_unref(src);
        }
    }
    gtk_widget_destroy(dlg);
    g_free(target);
    fm_window_refresh(w);
}

/* ---------------- new folder ---------------- */

void fm_fileops_new_folder(FmWindow *w) {
    if (!w->current_path) return;
    GtkWidget *dlg = prompt_string(w, "New Folder", "Folder name:", "untitled folder");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GtkEntry *entry = g_object_get_data(G_OBJECT(dlg), "fm-entry");
        const char *name = gtk_entry_get_text(entry);
        if (name && *name) {
            char *full = g_build_filename(w->current_path, name, NULL);
            GFile *f = g_file_new_for_path(full);
            GError *err = NULL;
            if (!g_file_make_directory(f, NULL, &err))
                show_error(w, "Create folder failed", err);
            g_object_unref(f);
            g_free(full);
        }
    }
    gtk_widget_destroy(dlg);
    fm_window_refresh(w);
}

/* ---------------- context menu ---------------- */

static void mi_open(GtkMenuItem *mi, gpointer ud) {
    (void)mi;
    FmWindow *w = ud;
    GList *sel = fm_view_get_selected_paths(w);
    if (sel) {
        fm_fileops_open(w, sel->data);
        g_list_free_full(sel, g_free);
    }
}
static void mi_copy(GtkMenuItem *mi, gpointer ud)   { (void)mi; fm_fileops_copy_to_clipboard(ud, FALSE); }
static void mi_cut(GtkMenuItem *mi, gpointer ud)    { (void)mi; fm_fileops_copy_to_clipboard(ud, TRUE);  }
static void mi_paste(GtkMenuItem *mi, gpointer ud)  { (void)mi; fm_fileops_paste(ud); }
static void mi_trash(GtkMenuItem *mi, gpointer ud)  { (void)mi; fm_fileops_trash_selected(ud); }
static void mi_rename(GtkMenuItem *mi, gpointer ud) { (void)mi; fm_fileops_rename_selected(ud); }
static void mi_new(GtkMenuItem *mi, gpointer ud)    { (void)mi; fm_fileops_new_folder(ud); }
static void mi_refresh(GtkMenuItem *mi, gpointer ud){ (void)mi; fm_window_refresh(ud); }

static void add_item(GtkWidget *menu, const char *label,
                     GCallback cb, FmWindow *w) {
    GtkWidget *mi = gtk_menu_item_new_with_label(label);
    g_signal_connect(mi, "activate", cb, w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
}

GtkWidget *fm_fileops_make_context_menu(FmWindow *w) {
    GtkWidget *menu = gtk_menu_new();
    add_item(menu, "Open",        G_CALLBACK(mi_open),    w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    add_item(menu, "Cut",         G_CALLBACK(mi_cut),     w);
    add_item(menu, "Copy",        G_CALLBACK(mi_copy),    w);
    add_item(menu, "Paste",       G_CALLBACK(mi_paste),   w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    add_item(menu, "Rename…",     G_CALLBACK(mi_rename),  w);
    add_item(menu, "Move to Trash", G_CALLBACK(mi_trash), w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    add_item(menu, "New Folder…", G_CALLBACK(mi_new),     w);
    add_item(menu, "Refresh",     G_CALLBACK(mi_refresh), w);
    return menu;
}
