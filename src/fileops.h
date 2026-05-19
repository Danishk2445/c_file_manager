#ifndef FM_FILEOPS_H
#define FM_FILEOPS_H

#include "window.h"

void fm_fileops_open(FmWindow *w, const char *path);
void fm_fileops_copy_to_clipboard(FmWindow *w, gboolean cut);
void fm_fileops_paste(FmWindow *w);
void fm_fileops_trash_selected(FmWindow *w);
void fm_fileops_rename_selected(FmWindow *w);
void fm_fileops_new_folder(FmWindow *w);

/* Context menu factory for right-click on icon/list views. */
GtkWidget *fm_fileops_make_context_menu(FmWindow *w);

#endif
