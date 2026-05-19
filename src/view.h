#ifndef FM_VIEW_H
#define FM_VIEW_H

#include <gtk/gtk.h>
#include "window.h"

enum {
    COL_ICON = 0,        /* GdkPixbuf */
    COL_NAME,            /* gchararray display name */
    COL_SIZE_STR,        /* gchararray */
    COL_MODIFIED_STR,    /* gchararray */
    COL_MIME,            /* gchararray */
    COL_FULL_PATH,       /* gchararray */
    COL_IS_DIR,          /* gboolean */
    COL_IS_HIDDEN,       /* gboolean */
    N_COLS
};

void fm_view_build(FmWindow *w);
void fm_view_load_dir(FmWindow *w, const char *path);
GList *fm_view_get_selected_paths(FmWindow *w);

#endif
