#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkWidget *path_entry;
    GtkWidget *statusbar;
    char current_path[PATH_MAX];
    gboolean show_hidden;
} FileManager;

enum {
    COL_ICON,
    COL_NAME,
    COL_SIZE,
    COL_MODIFIED,
    NUM_COLS
};

void refresh_file_list(FileManager *fm);
void update_statusbar(FileManager *fm);

char* format_size(off_t size) {
    static char buf[64];
    if (size < 1024)
        snprintf(buf, sizeof(buf), "%ld B", size);
    else if (size < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", size / 1024.0);
    else if (size < 1024 * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", size / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.1f GB", size / (1024.0 * 1024 * 1024));
    return buf;
}

void navigate_to_path(FileManager *fm, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        char resolved[PATH_MAX];
        if (realpath(path, resolved)) {
            strcpy(fm->current_path, resolved);
            gtk_entry_set_text(GTK_ENTRY(fm->path_entry), fm->current_path);
            refresh_file_list(fm);
        }
    }
}

void on_places_clicked(GtkButton *button, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    const char *path = (const char*)g_object_get_data(G_OBJECT(button), "path");
    if (path) {
        navigate_to_path(fm, path);
    }
}

void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, 
                      GtkTreeViewColumn *column, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    GtkTreeIter iter;
    gchar *filename;
    
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gtk_tree_model_get(model, &iter, COL_NAME, &filename, -1);
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", fm->current_path, filename);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            navigate_to_path(fm, full_path);
        }
        
        g_free(filename);
    }
}

void update_statusbar(FileManager *fm) {
    int total_items = 0;
    int hidden_items = 0;
    
    DIR *dir = opendir(fm->current_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            total_items++;
            if (entry->d_name[0] == '.')
                hidden_items++;
        }
        closedir(dir);
    }
    
    char status[256];
    if (fm->show_hidden) {
        snprintf(status, sizeof(status), "%d items", total_items);
    } else {
        int visible = total_items - hidden_items;
        snprintf(status, sizeof(status), "%d items (%d hidden)", visible, hidden_items);
    }
    
    gtk_statusbar_pop(GTK_STATUSBAR(fm->statusbar), 0);
    gtk_statusbar_push(GTK_STATUSBAR(fm->statusbar), 0, status);
}

void refresh_file_list(FileManager *fm) {
    gtk_list_store_clear(fm->store);
    
    DIR *dir = opendir(fm->current_path);
    if (!dir) {
        perror("opendir");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        // Hide hidden files unless show_hidden is enabled
        if (!fm->show_hidden && entry->d_name[0] == '.')
            continue;
            
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", fm->current_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == -1)
            continue;
        
        GtkTreeIter iter;
        gtk_list_store_append(fm->store, &iter);
        
        const char *icon = S_ISDIR(st.st_mode) ? "folder" : "text-x-generic";
        const char *size = S_ISDIR(st.st_mode) ? "" : format_size(st.st_size);
        
        // Format modification time
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
        
        gtk_list_store_set(fm->store, &iter,
                          COL_ICON, icon,
                          COL_NAME, entry->d_name,
                          COL_SIZE, size,
                          COL_MODIFIED, time_str,
                          -1);
    }
    
    closedir(dir);
    update_statusbar(fm);
}

void on_go_home(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    const char *home = getenv("HOME");
    if (home) {
        navigate_to_path(fm, home);
    }
}

void on_go_up(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    if (strcmp(fm->current_path, "/") != 0) {
        char *last_slash = strrchr(fm->current_path, '/');
        if (last_slash != NULL && last_slash != fm->current_path) {
            *last_slash = '\0';
        } else if (last_slash == fm->current_path) {
            strcpy(fm->current_path, "/");
        }
        gtk_entry_set_text(GTK_ENTRY(fm->path_entry), fm->current_path);
        refresh_file_list(fm);
    }
}

void on_refresh(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    refresh_file_list(fm);
}

void on_toggle_hidden(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    fm->show_hidden = !fm->show_hidden;
    refresh_file_list(fm);
}

void on_new_folder(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "New Folder",
        GTK_WINDOW(fm->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_ACCEPT,
        NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Folder name");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && strlen(name) > 0) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", fm->current_path, name);
            if (mkdir(path, 0755) == 0) {
                refresh_file_list(fm);
            }
        }
    }
    
    gtk_widget_destroy(dialog);
}

void on_delete(GtkWidget *widget, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(fm->tree_view));
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *filename;
        gtk_tree_model_get(model, &iter, COL_NAME, &filename, -1);
        
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(fm->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "Delete '%s'?", filename);
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", fm->current_path, filename);
            if (remove(path) == 0) {
                refresh_file_list(fm);
            }
        }
        
        gtk_widget_destroy(dialog);
        g_free(filename);
    }
}

void on_path_activate(GtkEntry *entry, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    const char *path = gtk_entry_get_text(entry);
    navigate_to_path(fm, path);
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    FileManager *fm = (FileManager*)user_data;
    
    // Check for Ctrl+H
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H)) {
        on_toggle_hidden(widget, user_data);
        return TRUE;
    }
    
    return FALSE;
}

GtkWidget* create_place_button(const char *label, const char *icon_name, const char *path) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    
    g_object_set_data_full(G_OBJECT(button), "path", g_strdup(path), g_free);
    
    return button;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    FileManager fm;
    fm.show_hidden = FALSE;
    
    // Get home directory
    const char *home = getenv("HOME");
    strcpy(fm.current_path, home ? home : "/");
    
    // Create main window
    fm.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(fm.window), "File Manager");
    gtk_window_set_default_size(GTK_WINDOW(fm.window), 900, 600);
    g_signal_connect(fm.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(fm.window, "key-press-event", G_CALLBACK(on_key_press), &fm);
    
    // Main container
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(fm.window), vbox);
    
    // Toolbar
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    
    GtkToolItem *back_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(back_btn), "go-previous");
    gtk_widget_set_tooltip_text(GTK_WIDGET(back_btn), "Back");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_go_up), &fm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), back_btn, -1);
    
    GtkToolItem *forward_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(forward_btn), "go-next");
    gtk_widget_set_tooltip_text(GTK_WIDGET(forward_btn), "Forward");
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), forward_btn, -1);
    
    GtkToolItem *up_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(up_btn), "go-up");
    gtk_widget_set_tooltip_text(GTK_WIDGET(up_btn), "Up");
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_go_up), &fm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), up_btn, -1);
    
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
    
    GtkToolItem *new_folder_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(new_folder_btn), "folder-new");
    gtk_widget_set_tooltip_text(GTK_WIDGET(new_folder_btn), "New Folder");
    g_signal_connect(new_folder_btn, "clicked", G_CALLBACK(on_new_folder), &fm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), new_folder_btn, -1);
    
    GtkToolItem *delete_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(delete_btn), "edit-delete");
    gtk_widget_set_tooltip_text(GTK_WIDGET(delete_btn), "Delete");
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete), &fm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), delete_btn, -1);
    
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
    
    GtkToolItem *search_btn = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(search_btn), "edit-find");
    gtk_widget_set_tooltip_text(GTK_WIDGET(search_btn), "Search");
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), search_btn, -1);
    
    // Path bar
    GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), path_box, FALSE, FALSE, 5);
    gtk_widget_set_margin_start(path_box, 5);
    gtk_widget_set_margin_end(path_box, 5);
    
    fm.path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(fm.path_entry), fm.current_path);
    g_signal_connect(fm.path_entry, "activate", G_CALLBACK(on_path_activate), &fm);
    gtk_box_pack_start(GTK_BOX(path_box), fm.path_entry, TRUE, TRUE, 0);
    
    // Main content area with paned layout
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
    
    // Left sidebar (Places)
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_size_request(sidebar, 150, -1);
    
    GtkWidget *places_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(places_label), "<b>Places</b>");
    gtk_widget_set_halign(places_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(places_label, 8);
    gtk_widget_set_margin_top(places_label, 5);
    gtk_widget_set_margin_bottom(places_label, 5);
    gtk_box_pack_start(GTK_BOX(sidebar), places_label, FALSE, FALSE, 0);
    
    // Add place buttons
    char desktop_path[PATH_MAX], documents_path[PATH_MAX], downloads_path[PATH_MAX];
    char pictures_path[PATH_MAX], music_path[PATH_MAX], videos_path[PATH_MAX];
    
    snprintf(desktop_path, sizeof(desktop_path), "%s/Desktop", home);
    snprintf(documents_path, sizeof(documents_path), "%s/Documents", home);
    snprintf(downloads_path, sizeof(downloads_path), "%s/Downloads", home);
    snprintf(pictures_path, sizeof(pictures_path), "%s/Pictures", home);
    snprintf(music_path, sizeof(music_path), "%s/Music", home);
    snprintf(videos_path, sizeof(videos_path), "%s/Videos", home);
    
    GtkWidget *home_btn = create_place_button("Home", "go-home", home);
    g_signal_connect(home_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), home_btn, FALSE, FALSE, 0);
    
    GtkWidget *desktop_btn = create_place_button("Desktop", "user-desktop", desktop_path);
    g_signal_connect(desktop_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), desktop_btn, FALSE, FALSE, 0);
    
    GtkWidget *documents_btn = create_place_button("Documents", "folder-documents", documents_path);
    g_signal_connect(documents_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), documents_btn, FALSE, FALSE, 0);
    
    GtkWidget *downloads_btn = create_place_button("Downloads", "folder-download", downloads_path);
    g_signal_connect(downloads_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), downloads_btn, FALSE, FALSE, 0);
    
    GtkWidget *pictures_btn = create_place_button("Pictures", "folder-pictures", pictures_path);
    g_signal_connect(pictures_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), pictures_btn, FALSE, FALSE, 0);
    
    GtkWidget *music_btn = create_place_button("Music", "folder-music", music_path);
    g_signal_connect(music_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), music_btn, FALSE, FALSE, 0);
    
    GtkWidget *videos_btn = create_place_button("Videos", "folder-videos", videos_path);
    g_signal_connect(videos_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), videos_btn, FALSE, FALSE, 0);
    
    // Add separator
    gtk_box_pack_start(GTK_BOX(sidebar), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    
    GtkWidget *root_btn = create_place_button("Root", "drive-harddisk", "/");
    g_signal_connect(root_btn, "clicked", G_CALLBACK(on_places_clicked), &fm);
    gtk_box_pack_start(GTK_BOX(sidebar), root_btn, FALSE, FALSE, 0);
    
    gtk_paned_add1(GTK_PANED(paned), sidebar);
    
    // Right side - file view
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    
    // Create list store
    fm.store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, 
                                  G_TYPE_STRING, G_TYPE_STRING);
    
    // Create tree view
    fm.tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(fm.store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(fm.tree_view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(fm.tree_view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(fm.tree_view), COL_NAME);
    gtk_container_add(GTK_CONTAINER(scroll), fm.tree_view);
    g_signal_connect(fm.tree_view, "row-activated", G_CALLBACK(on_row_activated), &fm);
    
    // Add columns
    GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(
        "", renderer, "icon-name", COL_ICON, NULL);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(col, 30);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fm.tree_view), col);
    
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
        "Name", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sort_column_id(col, COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fm.tree_view), col);
    
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
        "Size", renderer, "text", COL_SIZE, NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sort_column_id(col, COL_SIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fm.tree_view), col);
    
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
        "Modified", renderer, "text", COL_MODIFIED, NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_sort_column_id(col, COL_MODIFIED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fm.tree_view), col);
    
    gtk_paned_add2(GTK_PANED(paned), scroll);
    gtk_paned_set_position(GTK_PANED(paned), 150);
    
    // Statusbar
    fm.statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), fm.statusbar, FALSE, FALSE, 0);
    
    // Initial file list
    refresh_file_list(&fm);
    
    gtk_widget_show_all(fm.window);
    gtk_main();
    
    return 0;
}
