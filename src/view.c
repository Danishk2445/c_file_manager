#include "view.h"
#include "fileops.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

#define ICON_SIZE_GRID 48
#define ICON_SIZE_LIST 16

/* ---------------- icon resolution ---------------- */

static GdkPixbuf *icon_for_info(GFileInfo *info, int size) {
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GIcon *gicon = g_file_info_get_icon(info);
    if (!gicon) {
        return gtk_icon_theme_load_icon(theme, "text-x-generic", size,
                                        GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }
    GtkIconInfo *ii = gtk_icon_theme_lookup_by_gicon(theme, gicon, size,
                                                     GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!ii) {
        return gtk_icon_theme_load_icon(theme, "text-x-generic", size,
                                        GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    }
    GdkPixbuf *pb = gtk_icon_info_load_icon(ii, NULL);
    g_object_unref(ii);
    return pb;
}

/* ---------------- filter ---------------- */

static gboolean filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer ud) {
    FmWindow *w = ud;
    gboolean is_hidden;
    gtk_tree_model_get(model, iter, COL_IS_HIDDEN, &is_hidden, -1);
    return w->show_hidden || !is_hidden;
}

/* ---------------- selection helper ---------------- */

static char *path_from_filter_path(FmWindow *w, GtkTreePath *fpath) {
    GtkTreePath *cpath = gtk_tree_model_filter_convert_path_to_child_path(
        GTK_TREE_MODEL_FILTER(w->filter), fpath);
    if (!cpath) return NULL;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(w->store), &iter, cpath)) {
        gtk_tree_path_free(cpath);
        return NULL;
    }
    gtk_tree_path_free(cpath);
    char *full = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(w->store), &iter, COL_FULL_PATH, &full, -1);
    return full;
}

GList *fm_view_get_selected_paths(FmWindow *w) {
    GList *result = NULL;
    const char *current = gtk_stack_get_visible_child_name(GTK_STACK(w->view_stack));
    if (g_strcmp0(current, "icon") == 0) {
        GList *items = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(w->icon_view));
        for (GList *l = items; l; l = l->next) {
            char *p = path_from_filter_path(w, l->data);
            if (p) result = g_list_prepend(result, p);
        }
        g_list_free_full(items, (GDestroyNotify)gtk_tree_path_free);
    } else {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
        GtkTreeModel *m = NULL;
        GList *rows = gtk_tree_selection_get_selected_rows(sel, &m);
        for (GList *l = rows; l; l = l->next) {
            char *p = path_from_filter_path(w, l->data);
            if (p) result = g_list_prepend(result, p);
        }
        g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    }
    return g_list_reverse(result);
}

/* ---------------- activation ---------------- */

static void activate_filter_path(FmWindow *w, GtkTreePath *fpath) {
    GtkTreePath *cpath = gtk_tree_model_filter_convert_path_to_child_path(
        GTK_TREE_MODEL_FILTER(w->filter), fpath);
    if (!cpath) return;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(w->store), &it, cpath)) {
        gtk_tree_path_free(cpath);
        return;
    }
    gtk_tree_path_free(cpath);
    char *full = NULL;
    gboolean is_dir = FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(w->store), &it,
                       COL_FULL_PATH, &full,
                       COL_IS_DIR, &is_dir,
                       -1);
    if (!full) return;
    if (is_dir)
        fm_window_navigate(w, full, TRUE);
    else
        fm_fileops_open(w, full);
    g_free(full);
}

static void on_icon_activated(GtkIconView *iv, GtkTreePath *path, gpointer ud) {
    (void)iv;
    activate_filter_path((FmWindow*)ud, path);
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer ud) {
    (void)tv; (void)col;
    activate_filter_path((FmWindow*)ud, path);
}

/* ---------------- right-click context menu ---------------- */

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *ev, gpointer ud) {
    (void)widget;
    FmWindow *w = ud;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == GDK_BUTTON_SECONDARY) {
        GtkWidget *menu = fm_fileops_make_context_menu(w);
        gtk_menu_attach_to_widget(GTK_MENU(menu), w->window, NULL);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)ev);
        return FALSE;
    }
    return FALSE;
}

/* ---------------- build ---------------- */

void fm_view_build(FmWindow *w) {
    w->store = gtk_list_store_new(N_COLS,
                                  GDK_TYPE_PIXBUF,    /* COL_ICON */
                                  G_TYPE_STRING,      /* COL_NAME */
                                  G_TYPE_STRING,      /* COL_SIZE_STR */
                                  G_TYPE_STRING,      /* COL_MODIFIED_STR */
                                  G_TYPE_STRING,      /* COL_MIME */
                                  G_TYPE_STRING,      /* COL_FULL_PATH */
                                  G_TYPE_BOOLEAN,     /* COL_IS_DIR */
                                  G_TYPE_BOOLEAN);    /* COL_IS_HIDDEN */

    w->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(w->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(w->filter),
                                           filter_visible, w, NULL);

    /* Stack of icon view + tree view. */
    w->view_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(w->view_stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);

    /* --- Icon view --- */
    w->icon_view = gtk_icon_view_new_with_model(w->filter);
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(w->icon_view), COL_ICON);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(w->icon_view), COL_NAME);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(w->icon_view), 96);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(w->icon_view),
                                     GTK_SELECTION_MULTIPLE);
    g_signal_connect(w->icon_view, "item-activated",
                     G_CALLBACK(on_icon_activated), w);
    g_signal_connect(w->icon_view, "button-press-event",
                     G_CALLBACK(on_button_press), w);

    GtkWidget *icon_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(icon_scroll), w->icon_view);
    gtk_stack_add_named(GTK_STACK(w->view_stack), icon_scroll, "icon");

    /* --- Tree view --- */
    w->tree_view = gtk_tree_view_new_with_model(w->filter);
    {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    }
    g_signal_connect(w->tree_view, "row-activated",
                     G_CALLBACK(on_row_activated), w);
    g_signal_connect(w->tree_view, "button-press-event",
                     G_CALLBACK(on_button_press), w);

    /* Name column = pixbuf + text. */
    {
        GtkTreeViewColumn *col = gtk_tree_view_column_new();
        gtk_tree_view_column_set_title(col, "Name");
        gtk_tree_view_column_set_expand(col, TRUE);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_sort_column_id(col, COL_NAME);

        GtkCellRenderer *pix = gtk_cell_renderer_pixbuf_new();
        gtk_tree_view_column_pack_start(col, pix, FALSE);
        gtk_tree_view_column_add_attribute(col, pix, "pixbuf", COL_ICON);

        GtkCellRenderer *txt = gtk_cell_renderer_text_new();
        gtk_tree_view_column_pack_start(col, txt, TRUE);
        gtk_tree_view_column_add_attribute(col, txt, "text", COL_NAME);

        gtk_tree_view_append_column(GTK_TREE_VIEW(w->tree_view), col);
    }
    {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            "Size", r, "text", COL_SIZE_STR, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(w->tree_view), c);
    }
    {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            "Modified", r, "text", COL_MODIFIED_STR, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(w->tree_view), c);
    }
    {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            "Type", r, "text", COL_MIME, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(w->tree_view), c);
    }

    GtkWidget *tree_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(tree_scroll), w->tree_view);
    gtk_stack_add_named(GTK_STACK(w->view_stack), tree_scroll, "list");

    /* Default visible child = icon view. */
    gtk_stack_set_visible_child_name(GTK_STACK(w->view_stack), "icon");

    /* Make the stack itself receive right-clicks when the views don't
     * (e.g. empty directory). */
    gtk_widget_add_events(w->view_stack, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(w->view_stack, "button-press-event",
                     G_CALLBACK(on_button_press), w);
}

/* ---------------- load ---------------- */

void fm_view_load_dir(FmWindow *w, const char *path) {
    gtk_list_store_clear(w->store);

    GFile *dir = g_file_new_for_path(path);
    GError *err = NULL;
    GFileEnumerator *en = g_file_enumerate_children(
        dir,
        "standard::name,standard::display-name,standard::size,"
        "standard::type,standard::icon,standard::is-hidden,"
        "standard::content-type,time::modified",
        G_FILE_QUERY_INFO_NONE, NULL, &err);
    if (!en) {
        char *msg = g_strdup_printf("Cannot read %s: %s",
                                    path, err ? err->message : "unknown");
        fm_window_set_status(w, msg);
        g_free(msg);
        g_clear_error(&err);
        g_object_unref(dir);
        return;
    }

    int n_items = 0;
    int icon_size = ICON_SIZE_GRID;
    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        const char *name = g_file_info_get_name(info);
        const char *disp = g_file_info_get_display_name(info);
        if (!disp) disp = name;

        GFile *child = g_file_get_child(dir, name);
        char *full = g_file_get_path(child);
        g_object_unref(child);

        GFileType type = g_file_info_get_file_type(info);
        gboolean is_dir = (type == G_FILE_TYPE_DIRECTORY);
        gboolean is_hidden = g_file_info_get_is_hidden(info);

        char *size_str = NULL;
        if (is_dir) {
            size_str = g_strdup("—");
        } else {
            goffset sz = g_file_info_get_size(info);
            size_str = g_format_size((guint64)sz);
        }

        char *mod_str = NULL;
        GDateTime *dt = g_file_info_get_modification_date_time(info);
        if (dt) {
            mod_str = g_date_time_format(dt, "%Y-%m-%d %H:%M");
            g_date_time_unref(dt);
        } else {
            mod_str = g_strdup("");
        }

        const char *mime = g_file_info_get_content_type(info);
        if (!mime) mime = is_dir ? "inode/directory" : "application/octet-stream";

        GdkPixbuf *pb = icon_for_info(info, icon_size);

        GtkTreeIter iter;
        gtk_list_store_append(w->store, &iter);
        gtk_list_store_set(w->store, &iter,
                           COL_ICON, pb,
                           COL_NAME, disp,
                           COL_SIZE_STR, size_str,
                           COL_MODIFIED_STR, mod_str,
                           COL_MIME, mime,
                           COL_FULL_PATH, full,
                           COL_IS_DIR, is_dir,
                           COL_IS_HIDDEN, is_hidden,
                           -1);

        if (pb) g_object_unref(pb);
        g_free(full);
        g_free(size_str);
        g_free(mod_str);
        g_object_unref(info);
        n_items++;
    }
    g_object_unref(en);
    g_object_unref(dir);

    char *status = g_strdup_printf("%d item%s", n_items, n_items == 1 ? "" : "s");
    fm_window_set_status(w, status);
    g_free(status);
}
