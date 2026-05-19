#include <gtk/gtk.h>
#include "window.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* Force Adwaita icon theme so the look matches Nautilus even on
     * desktops that default to Breeze, Papirus, etc. */
    g_object_set(gtk_settings_get_default(),
                 "gtk-icon-theme-name", "Adwaita",
                 NULL);

    fm_window_new();
    gtk_main();
    return 0;
}
