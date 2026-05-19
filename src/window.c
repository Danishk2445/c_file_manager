#include "window.h"
#include "view.h"
#include "sidebar.h"
#include "pathbar.h"
#include "fileops.h"

#include <glib.h>
#include <string.h>

/* ---------------- navigation helpers ---------------- */

static void clear_path_list(GList **lp) {
    g_list_free_full(*lp, g_free);
    *lp = NULL;
}

static void update_nav_sensitivity(FmWindow *w) {
    gtk_widget_set_sensitive(w->back_btn, w->back_stack != NULL);
    gtk_widget_set_sensitive(w->forward_btn, w->forward_stack != NULL);

    gboolean can_up = FALSE;
    if (w->current_path && strcmp(w->current_path, "/") != 0)
        can_up = TRUE;
    gtk_widget_set_sensitive(w->up_btn, can_up);
}

void fm_window_set_status(FmWindow *w, const char *text) {
    gtk_statusbar_pop(GTK_STATUSBAR(w->statusbar), w->statusbar_ctx);
    gtk_statusbar_push(GTK_STATUSBAR(w->statusbar), w->statusbar_ctx, text);
}

void fm_window_refresh(FmWindow *w) {
    if (w->current_path)
        fm_view_load_dir(w, w->current_path);
}

void fm_window_navigate(FmWindow *w, const char *path, gboolean push_history) {
    if (!path) return;
    if (w->current_path && strcmp(w->current_path, path) == 0) return;

    if (push_history && w->current_path) {
        w->back_stack = g_list_prepend(w->back_stack, g_strdup(w->current_path));
        clear_path_list(&w->forward_stack);
    }

    g_free(w->current_path);
    w->current_path = g_strdup(path);

    /* Window title: the basename of the directory (or "/"). */
    char *base = g_path_get_basename(path);
    gtk_header_bar_set_title(GTK_HEADER_BAR(w->header),
                             (strcmp(base, "/") == 0) ? "/" : base);
    g_free(base);

    fm_pathbar_update(w);
    fm_view_load_dir(w, path);
    update_nav_sensitivity(w);
}

/* ---------------- toolbar callbacks ---------------- */

static void on_back(GtkButton *b, FmWindow *w) {
    (void)b;
    if (!w->back_stack) return;
    char *target = w->back_stack->data;
    w->back_stack = g_list_delete_link(w->back_stack, w->back_stack);
    if (w->current_path)
        w->forward_stack = g_list_prepend(w->forward_stack, g_strdup(w->current_path));
    fm_window_navigate(w, target, FALSE);
    g_free(target);
}

static void on_forward(GtkButton *b, FmWindow *w) {
    (void)b;
    if (!w->forward_stack) return;
    char *target = w->forward_stack->data;
    w->forward_stack = g_list_delete_link(w->forward_stack, w->forward_stack);
    if (w->current_path)
        w->back_stack = g_list_prepend(w->back_stack, g_strdup(w->current_path));
    fm_window_navigate(w, target, FALSE);
    g_free(target);
}

static void on_up(GtkButton *b, FmWindow *w) {
    (void)b;
    if (!w->current_path) return;
    char *parent = g_path_get_dirname(w->current_path);
    fm_window_navigate(w, parent, TRUE);
    g_free(parent);
}

static void on_view_toggle(GtkToggleButton *t, FmWindow *w) {
    /* Untoggled = icon view (default), toggled = list view. */
    if (gtk_toggle_button_get_active(t))
        gtk_stack_set_visible_child_name(GTK_STACK(w->view_stack), "list");
    else
        gtk_stack_set_visible_child_name(GTK_STACK(w->view_stack), "icon");
}

static void on_hidden_toggle(GtkToggleButton *t, FmWindow *w) {
    w->show_hidden = gtk_toggle_button_get_active(t);
    if (w->filter)
        gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(w->filter));
}

/* ---------------- key accelerators ---------------- */

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *ev, gpointer ud) {
    (void)widget;
    FmWindow *w = ud;
    guint k = ev->keyval;
    GdkModifierType mods = ev->state & gtk_accelerator_get_default_mod_mask();

    if (mods == GDK_CONTROL_MASK) {
        if (k == GDK_KEY_h || k == GDK_KEY_H) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->hidden_toggle),
                                         !w->show_hidden);
            return TRUE;
        }
        if (k == GDK_KEY_c || k == GDK_KEY_C) { fm_fileops_copy_to_clipboard(w, FALSE); return TRUE; }
        if (k == GDK_KEY_x || k == GDK_KEY_X) { fm_fileops_copy_to_clipboard(w, TRUE);  return TRUE; }
        if (k == GDK_KEY_v || k == GDK_KEY_V) { fm_fileops_paste(w);                    return TRUE; }
        if (k == GDK_KEY_n || k == GDK_KEY_N) { fm_fileops_new_folder(w);               return TRUE; }
        if (k == GDK_KEY_r || k == GDK_KEY_R) { fm_window_refresh(w);                   return TRUE; }
    }
    if (mods == GDK_MOD1_MASK) { /* Alt */
        if (k == GDK_KEY_Left)  { on_back(NULL, w);    return TRUE; }
        if (k == GDK_KEY_Right) { on_forward(NULL, w); return TRUE; }
        if (k == GDK_KEY_Up)    { on_up(NULL, w);      return TRUE; }
    }
    if (mods == 0) {
        if (k == GDK_KEY_F2)     { fm_fileops_rename_selected(w); return TRUE; }
        if (k == GDK_KEY_Delete) { fm_fileops_trash_selected(w);  return TRUE; }
        if (k == GDK_KEY_F5)     { fm_window_refresh(w);          return TRUE; }
    }
    return FALSE;
}

/* ---------------- construction ---------------- */

/* CSS tweaks: round the CSD corners and inset / round the sidebar
 * selection so the look matches modern Nautilus instead of inheriting
 * whatever sharp-edged styling the system theme provides. */
static void install_css(void) {
    static const char *css =
        "decoration,"
        "window.background {"
        "  border-radius: 12px;"
        "}"
        ".sidebar:selected,"
        "treeview.sidebar:selected {"
        "  border-radius: 6px;"
        "}";
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
}

static GtkWidget *make_icon_button(const char *icon_name, const char *tooltip) {
    GtkWidget *btn = gtk_button_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn, tooltip);
    return btn;
}

static void on_destroy(GtkWidget *win, gpointer ud) {
    (void)win;
    FmWindow *w = ud;
    g_free(w->current_path);
    clear_path_list(&w->back_stack);
    clear_path_list(&w->forward_stack);
    g_free(w);
    gtk_main_quit();
}

FmWindow *fm_window_new(void) {
    install_css();

    FmWindow *w = g_new0(FmWindow, 1);

    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(w->window), 980, 640);
    gtk_window_set_icon_name(GTK_WINDOW(w->window), "system-file-manager");

    /* HeaderBar — gives the modern Nautilus titlebar look. */
    w->header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(w->header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(w->header), "Files");
    gtk_window_set_titlebar(GTK_WINDOW(w->window), w->header);

    /* Left side of headerbar: back / forward / up grouped, then pathbar. */
    GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(nav_box), "linked");

    w->back_btn    = make_icon_button("go-previous-symbolic", "Back (Alt+Left)");
    w->forward_btn = make_icon_button("go-next-symbolic",     "Forward (Alt+Right)");
    w->up_btn      = make_icon_button("go-up-symbolic",       "Up (Alt+Up)");
    gtk_box_pack_start(GTK_BOX(nav_box), w->back_btn,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), w->forward_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_box), w->up_btn,      FALSE, FALSE, 0);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(w->header), nav_box);

    /* Pathbar in the middle of the headerbar. */
    w->pathbar_box = fm_pathbar_new(w);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(w->header), w->pathbar_box);

    /* Right side: hidden-files toggle, view-mode toggle. */
    w->hidden_toggle = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(w->hidden_toggle),
        gtk_image_new_from_icon_name("view-reveal-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(w->hidden_toggle, "Show hidden files (Ctrl+H)");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(w->header), w->hidden_toggle);

    w->view_toggle = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(w->view_toggle),
        gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(w->view_toggle, "Toggle list/icon view");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(w->header), w->view_toggle);

    /* Main vertical box: paned + statusbar. */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(w->window), vbox);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 200);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    /* Sidebar on the left. */
    w->sidebar = fm_sidebar_new(w);
    GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sidebar_scroll), w->sidebar);
    gtk_paned_pack1(GTK_PANED(paned), sidebar_scroll, FALSE, FALSE);

    /* View stack on the right. */
    fm_view_build(w);
    gtk_paned_pack2(GTK_PANED(paned), w->view_stack, TRUE, FALSE);

    /* Statusbar. */
    w->statusbar = gtk_statusbar_new();
    w->statusbar_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(w->statusbar), "fm");
    gtk_box_pack_start(GTK_BOX(vbox), w->statusbar, FALSE, FALSE, 0);

    /* Signals. */
    g_signal_connect(w->back_btn,      "clicked",  G_CALLBACK(on_back),         w);
    g_signal_connect(w->forward_btn,   "clicked",  G_CALLBACK(on_forward),      w);
    g_signal_connect(w->up_btn,        "clicked",  G_CALLBACK(on_up),           w);
    g_signal_connect(w->view_toggle,   "toggled",  G_CALLBACK(on_view_toggle),  w);
    g_signal_connect(w->hidden_toggle, "toggled",  G_CALLBACK(on_hidden_toggle),w);
    g_signal_connect(w->window,        "destroy",  G_CALLBACK(on_destroy),      w);
    g_signal_connect(w->window,        "key-press-event", G_CALLBACK(on_key_press), w);

    /* Initial navigation. */
    const char *home = g_get_home_dir();
    fm_window_navigate(w, home, FALSE);
    update_nav_sensitivity(w);

    gtk_widget_show_all(w->window);
    return w;
}
