#ifndef FM_WINDOW_H
#define FM_WINDOW_H

#include <gtk/gtk.h>

typedef struct FmWindow FmWindow;

struct FmWindow {
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *back_btn;
    GtkWidget *forward_btn;
    GtkWidget *up_btn;
    GtkWidget *view_toggle;        /* icon vs list */
    GtkWidget *hidden_toggle;
    GtkWidget *pathbar_box;        /* horizontal box of breadcrumb buttons */
    GtkWidget *sidebar;            /* GtkTreeView */
    GtkWidget *view_stack;         /* GtkStack with "icon" and "list" children */
    GtkWidget *icon_view;          /* GtkIconView */
    GtkWidget *tree_view;          /* GtkTreeView */
    GtkWidget *statusbar;
    guint      statusbar_ctx;

    GtkListStore *store;           /* shared model for both views */
    GtkTreeModel *filter;          /* hidden-files filter wrapping store */

    char  *current_path;
    GList *back_stack;             /* list of char* (owned) */
    GList *forward_stack;          /* list of char* (owned) */

    gboolean show_hidden;
};

FmWindow *fm_window_new(void);
void      fm_window_navigate(FmWindow *w, const char *path, gboolean push_history);
void      fm_window_refresh(FmWindow *w);
void      fm_window_set_status(FmWindow *w, const char *text);

#endif
