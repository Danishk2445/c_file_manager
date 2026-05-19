#include "sidebar.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

enum {
    SB_ICON = 0,    /* icon-name string */
    SB_LABEL,       /* visible label */
    SB_PATH,        /* path to navigate to (NULL = separator) */
    SB_MOUNT,       /* GMount* (or NULL); owned reference */
    SB_N
};

static gboolean row_is_separator(GtkTreeModel *m, GtkTreeIter *it, gpointer ud) {
    (void)ud;
    char *path = NULL;
    gtk_tree_model_get(m, it, SB_PATH, &path, -1);
    gboolean sep = (path == NULL);
    g_free(path);
    return sep;
}

static void append_entry(GtkListStore *s, const char *icon, const char *label,
                         const char *path) {
    GtkTreeIter it;
    gtk_list_store_append(s, &it);
    gtk_list_store_set(s, &it,
                       SB_ICON, icon,
                       SB_LABEL, label,
                       SB_PATH, path,
                       SB_MOUNT, NULL,
                       -1);
}

static void append_separator(GtkListStore *s) {
    GtkTreeIter it;
    gtk_list_store_append(s, &it);
    gtk_list_store_set(s, &it,
                       SB_ICON, NULL,
                       SB_LABEL, NULL,
                       SB_PATH, NULL,
                       SB_MOUNT, NULL,
                       -1);
}

static void append_special_dir(GtkListStore *s, GUserDirectory which,
                               const char *icon, const char *label) {
    const char *p = g_get_user_special_dir(which);
    if (p && g_file_test(p, G_FILE_TEST_IS_DIR))
        append_entry(s, icon, label, p);
}

static void append_mount(GtkListStore *s, GMount *mount) {
    GFile *root = g_mount_get_root(mount);
    char *path = g_file_get_path(root);
    g_object_unref(root);
    if (!path) return; /* non-local mount we can't navigate via path */

    char *name = g_mount_get_name(mount);
    GIcon *icon = g_mount_get_icon(mount);
    char *icon_name = NULL;
    if (G_IS_THEMED_ICON(icon)) {
        const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        if (names && names[0]) icon_name = g_strdup(names[0]);
    }
    if (!icon_name) icon_name = g_strdup("drive-removable-media");

    GtkTreeIter it;
    gtk_list_store_append(s, &it);
    gtk_list_store_set(s, &it,
                       SB_ICON, icon_name,
                       SB_LABEL, name,
                       SB_PATH, path,
                       SB_MOUNT, g_object_ref(mount),
                       -1);
    g_free(name);
    g_free(icon_name);
    g_free(path);
    g_object_unref(icon);
}

static void clear_mounts(GtkListStore *store) {
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &it);
    while (valid) {
        GMount *m = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &it, SB_MOUNT, &m, -1);
        if (m) {
            g_object_unref(m);
            valid = gtk_list_store_remove(store, &it);
        } else {
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &it);
        }
    }
}

static void rebuild_mounts(GtkListStore *store, GVolumeMonitor *vm) {
    clear_mounts(store);
    GList *mounts = g_volume_monitor_get_mounts(vm);
    gboolean any = mounts != NULL;
    if (any) append_separator(store);
    for (GList *l = mounts; l; l = l->next) {
        append_mount(store, l->data);
        g_object_unref(l->data);
    }
    g_list_free(mounts);
}

static void on_mounts_changed(GVolumeMonitor *vm, GMount *m, gpointer ud) {
    (void)m;
    rebuild_mounts(GTK_LIST_STORE(ud), vm);
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer ud) {
    (void)col;
    FmWindow *w = ud;
    GtkTreeModel *m = gtk_tree_view_get_model(tv);
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(m, &it, path)) return;
    char *p = NULL;
    gtk_tree_model_get(m, &it, SB_PATH, &p, -1);
    if (p) {
        fm_window_navigate(w, p, TRUE);
        g_free(p);
    }
}

GtkWidget *fm_sidebar_new(FmWindow *w) {
    GtkListStore *store = gtk_list_store_new(SB_N,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_OBJECT);

    /* Standard places. */
    append_entry(store, "drive-harddisk-symbolic", "Computer", "/");
    append_entry(store, "user-home-symbolic", "Home", g_get_home_dir());
    append_special_dir(store, G_USER_DIRECTORY_DESKTOP,   "user-desktop-symbolic",   "Desktop");
    append_special_dir(store, G_USER_DIRECTORY_DOCUMENTS, "folder-documents-symbolic","Documents");
    append_special_dir(store, G_USER_DIRECTORY_DOWNLOAD,  "folder-download-symbolic", "Downloads");
    append_special_dir(store, G_USER_DIRECTORY_MUSIC,     "folder-music-symbolic",    "Music");
    append_special_dir(store, G_USER_DIRECTORY_PICTURES,  "folder-pictures-symbolic", "Pictures");
    append_special_dir(store, G_USER_DIRECTORY_VIDEOS,    "folder-videos-symbolic",   "Videos");

    {
        char *trash_dir = g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
        if (g_file_test(trash_dir, G_FILE_TEST_IS_DIR))
            append_entry(store, "user-trash-symbolic", "Trash", trash_dir);
        g_free(trash_dir);
    }

    /* Mounts. */
    GVolumeMonitor *vm = g_volume_monitor_get();
    rebuild_mounts(store, vm);
    g_signal_connect(vm, "mount-added",   G_CALLBACK(on_mounts_changed), store);
    g_signal_connect(vm, "mount-removed", G_CALLBACK(on_mounts_changed), store);
    /* vm ref is intentionally kept alive for the lifetime of the app. */

    GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tv), TRUE);
    gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW(tv),
                                         row_is_separator, NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(tv),
                                GTK_STYLE_CLASS_SIDEBAR);

    {
        GtkTreeViewColumn *col = gtk_tree_view_column_new();

        /* Inset the icon from the left edge and give rows a bit of vertical
         * breathing room — Nautilus' sidebar looks like this, not flush-left. */
        GtkCellRenderer *pix = gtk_cell_renderer_pixbuf_new();
        gtk_cell_renderer_set_padding(pix, 8, 2);
        gtk_tree_view_column_pack_start(col, pix, FALSE);
        gtk_tree_view_column_add_attribute(col, pix, "icon-name", SB_ICON);

        GtkCellRenderer *txt = gtk_cell_renderer_text_new();
        gtk_cell_renderer_set_padding(txt, 4, 2);
        gtk_tree_view_column_pack_start(col, txt, TRUE);
        gtk_tree_view_column_add_attribute(col, txt, "text", SB_LABEL);

        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    g_signal_connect(tv, "row-activated", G_CALLBACK(on_row_activated), w);
    return tv;
}
