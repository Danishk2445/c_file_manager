#include "pathbar.h"
#include "fileops.h"

#include <glib.h>
#include <string.h>

/* Modern Nautilus shows the location as a single rounded pill: an icon
 * (home/folder/computer) + the current folder name, with a "⋮" overflow
 * button glued to its right. We build that with two buttons in a "linked"
 * box so the theme renders them as one continuous pill. */

#define PATHBAR_MIN_WIDTH 200

static void on_main_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    FmWindow *w = ud;
    /* Cheap stand-in behaviour: jump up one level, mirroring what the
     * crumb-click used to do when you clicked the parent crumb. */
    if (!w->current_path || g_strcmp0(w->current_path, "/") == 0) return;
    char *parent = g_path_get_dirname(w->current_path);
    fm_window_navigate(w, parent, TRUE);
    g_free(parent);
}

/* ---------------- overflow popover ---------------- */

static void close_popover(GtkWidget *child) {
    GtkWidget *pop = gtk_widget_get_ancestor(child, GTK_TYPE_POPOVER);
    if (pop) gtk_widget_hide(pop);
}

static void act_new_folder(GtkButton *b, gpointer ud) {
    close_popover(GTK_WIDGET(b));
    fm_fileops_new_folder((FmWindow*)ud);
}
static void act_paste(GtkButton *b, gpointer ud) {
    close_popover(GTK_WIDGET(b));
    fm_fileops_paste((FmWindow*)ud);
}
static void act_toggle_hidden(GtkButton *b, gpointer ud) {
    close_popover(GTK_WIDGET(b));
    FmWindow *w = ud;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->hidden_toggle),
                                 !w->show_hidden);
}
static void act_reload(GtkButton *b, gpointer ud) {
    close_popover(GTK_WIDGET(b));
    fm_window_refresh((FmWindow*)ud);
}

static GtkWidget *menu_item(const char *label, GCallback cb, FmWindow *w) {
    /* GtkModelButton renders as a flush menu item under Adwaita. */
    GtkWidget *b = g_object_new(GTK_TYPE_MODEL_BUTTON, "text", label, NULL);
    g_signal_connect(b, "clicked", cb, w);
    return b;
}

static GtkWidget *build_popover(GtkWidget *relative_to, FmWindow *w) {
    GtkWidget *pop  = gtk_popover_new(relative_to);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start (vbox, 6);
    gtk_widget_set_margin_end   (vbox, 6);
    gtk_widget_set_margin_top   (vbox, 6);
    gtk_widget_set_margin_bottom(vbox, 6);

    gtk_box_pack_start(GTK_BOX(vbox),
        menu_item("New Folder",        G_CALLBACK(act_new_folder),    w), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        menu_item("Paste",             G_CALLBACK(act_paste),         w), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(vbox),
        menu_item("Show Hidden Files", G_CALLBACK(act_toggle_hidden), w), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        menu_item("Reload",            G_CALLBACK(act_reload),        w), FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(pop), vbox);
    gtk_widget_show_all(vbox);
    return pop;
}

/* ---------------- pathbar widget ---------------- */

GtkWidget *fm_pathbar_new(FmWindow *w) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "linked");

    /* Main pill: icon + label inside one clickable button. */
    GtkWidget *main_btn = gtk_button_new();
    gtk_widget_set_focus_on_click(main_btn, FALSE);
    gtk_widget_set_size_request(main_btn, PATHBAR_MIN_WIDTH, -1);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *icon  = gtk_image_new_from_icon_name("user-home-symbolic",
                                                    GTK_ICON_SIZE_BUTTON);
    GtkWidget *label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(inner), icon,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inner), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(main_btn), inner);

    g_object_set_data(G_OBJECT(box), "fm-pb-icon",  icon);
    g_object_set_data(G_OBJECT(box), "fm-pb-label", label);

    g_signal_connect(main_btn, "clicked", G_CALLBACK(on_main_clicked), w);
    gtk_box_pack_start(GTK_BOX(box), main_btn, FALSE, FALSE, 0);

    /* Overflow menu: dots on the right side of the pill. */
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(menu_btn),
        gtk_image_new_from_icon_name("view-more-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_focus_on_click(menu_btn, FALSE);
    gtk_widget_set_tooltip_text(menu_btn, "Location menu");

    GtkWidget *pop = build_popover(menu_btn, w);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_btn), pop);

    gtk_box_pack_start(GTK_BOX(box), menu_btn, FALSE, FALSE, 0);

    return box;
}

void fm_pathbar_update(FmWindow *w) {
    GtkWidget *icon  = g_object_get_data(G_OBJECT(w->pathbar_box), "fm-pb-icon");
    GtkWidget *label = g_object_get_data(G_OBJECT(w->pathbar_box), "fm-pb-label");
    if (!icon || !label || !w->current_path) {
        gtk_widget_show_all(w->pathbar_box);
        return;
    }

    const char *home = g_get_home_dir();
    const char *icon_name;
    char       *display_name;

    if (home && g_strcmp0(w->current_path, home) == 0) {
        icon_name    = "user-home-symbolic";
        display_name = g_strdup("Home");
    } else if (g_strcmp0(w->current_path, "/") == 0) {
        icon_name    = "drive-harddisk-symbolic";
        display_name = g_strdup("Computer");
    } else {
        icon_name    = "folder-symbolic";
        display_name = g_path_get_basename(w->current_path);
    }

    gtk_image_set_from_icon_name(GTK_IMAGE(icon), icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_label_set_text(GTK_LABEL(label), display_name);
    g_free(display_name);

    gtk_widget_show_all(w->pathbar_box);
}
