/*

 g3data2 : A program for grabbing data from scanned graphs
 Copyright (C) 2011 Jonas Frantz

 This file is part of g3data2.

 g3data2 is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 g3data2 is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 Authors email : jonas@frantz.fi

 */

#include <gtk/gtk.h>								/* Include gtk library */
#include <stdio.h>									/* Include stdio library */
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>									/* Include stdlib library */
#include <string.h>									/* Include string library */
#include <math.h>
#include <glib/gstdio.h>
#include <libgen.h>
#include "main.h"									/* Include predefined variables */
#include "datastore.h"
#include "strings.h"								/* Include strings */
#include "vardefs.h"

#ifdef NOSPACING
#define SECT_SEP 0
#define GROUP_SEP 0
#define ELEM_SEP 0
#define FRAME_INDENT 0
#define WINDOW_BORDER 0
#else
#define SECT_SEP 12
#define GROUP_SEP 12
#define ELEM_SEP 6
#define FRAME_INDENT 18
#define WINDOW_BORDER 12
#endif

// This is the name we will attach the data structure to the container with
static const char *DATA_STORE_NAME = "tabdatastruct";
static const gdouble MAIN_IMAGE_MIN_ZOOM = 0.05;
static const gdouble MAIN_IMAGE_MAX_ZOOM = 16.0;
static const gdouble MAIN_IMAGE_MAX_CANVAS_DIMENSION = 30000.0;
static const gdouble MAIN_IMAGE_ZOOM_STEP = 1.25;
static const gdouble ZOOM_AREA_VIEW_MULTIPLIER = 2.0;
static const gdouble MAIN_IMAGE_CANVAS_MIN_PAD = 512.0;
static const gint MAX_RECENT_FILES = 6;
static const gint START_TILE_THUMB_W = 160;
static const gint START_TILE_THUMB_H = 120;
static const gint START_TILE_COLUMNS = 3;
static const gint START_TILE_MIN_ITEM_W = 150;
static const gint START_TILE_MAX_ITEM_W = 220;
static const char *RECENT_GROUP = "RecentFiles";
static const char *RECENT_PATH_KEY_FMT = "path_%d";
static const char *RECENT_DATE_KEY_FMT = "date_%d";
static const char *EXPORT_PREF_GROUP = "Export";
static const char *EXPORT_ORDERING_KEY = "ordering";
static const char *EXPORT_ERRORS_KEY = "include_errors";

static const char *DROPPED_URI_DELIMITER = "\r\n";

#ifdef G3DATA2_DEBUG
#define G3DBG(...) g_printerr("[g3data2][debug] " __VA_ARGS__)
#else
#define G3DBG(...) ((void) 0)
#endif

static void setButtonSensitivity(struct TabData *tabData);
static void triggerUpdateDrawArea(GtkWidget *area);
static void refreshProcessingInformation(struct TabData *tabData);
static void refreshSeriesWidgets(struct TabData *tabData);
static void persistCalibration(struct TabData *tabData);
static void deleteSelectedPoint(GtkWidget *widget, gpointer data);
static gboolean calibrationIsComplete(const struct TabData *tabData);
static void updateExportMenuSensitivity(struct TabData *tabData);
static void updateEditMenuSensitivity(struct TabData *tabData);
static void exportFromMenu(GtkWidget *widget, gpointer data);
static void exportOrderingChanged(GtkCheckMenuItem *widget, gpointer data);
static void exportErrorsChanged(GtkCheckMenuItem *widget, gpointer data);
static struct TabData *getCurrentTabData(void);
void setNumberOfPointsEntryValue(GtkWidget *np_entry, gint np);
gint setupNewTab(char *filename, gdouble Scale, gdouble maxX,
		gdouble maxY, gboolean UsePreSetCoords, gdouble *TempCoords,
		gboolean *Uselogxy, gboolean *UseError);

// Declaration of gtk variables
GtkWidget *window;
GtkWidget *mainnotebook;
GtkWidget *close_menu_item;
GtkWidget *file_menu_widget;
GtkWidget *start_page_widget;
GtkWidget *start_icon_view_widget;
GtkWidget *export_current_menu_item;
GtkWidget *export_all_menu_item;
GtkWidget *remove_last_menu_item;
GtkWidget *clear_series_menu_item;
GtkWidget *delete_selected_menu_item;

struct RecentFileEntry {
	gchar *path;
	gchar *date;
};

GPtrArray *recent_files;
GPtrArray *recent_menu_items;

// Declaration of global variables
gboolean MovePointMode = FALSE;
gboolean HideLog = FALSE, HideZoomArea = FALSE;
static gint exportOrdering = 0;
static gboolean exportUseErrors = FALSE;

// Declaration of extern functions
extern void drawMarker(cairo_t *cr, gint x, gint y, gint type);
extern void drawSeriesMarker(cairo_t *cr, gdouble x, gdouble y, guint32 rgba,
		gboolean active, gboolean hovered, gboolean selected);
extern struct PointValue calculatePointValue(gdouble Xpos, gdouble Ypos,
		struct TabData *tabData);
static Datastore *appDatastore = NULL;

typedef enum {
	EXPORT_TO_STDOUT = 0,
	EXPORT_TO_FILE,
	EXPORT_TO_CLIPBOARD,
	EXPORT_TARGET_COUNT
} ExportTarget;

static void debugDumpViewportState(const char *tag, struct TabData *tabData) {
#ifdef G3DATA2_DEBUG
	GtkAdjustment *hadj, *vadj;
	gdouble hVal, hLower, hUpper, hPage;
	gdouble vVal, vLower, vUpper, vPage;
	gint vpW, vpH, daW, daH;

	if (tabData == NULL || tabData->ViewPort == NULL) {
		G3DBG("%s: tab/viewport not ready\n", tag);
		return;
	}

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	hVal = gtk_adjustment_get_value(hadj);
	hLower = gtk_adjustment_get_lower(hadj);
	hUpper = gtk_adjustment_get_upper(hadj);
	hPage = gtk_adjustment_get_page_size(hadj);
	vVal = gtk_adjustment_get_value(vadj);
	vLower = gtk_adjustment_get_lower(vadj);
	vUpper = gtk_adjustment_get_upper(vadj);
	vPage = gtk_adjustment_get_page_size(vadj);
	vpW = gtk_widget_get_allocated_width(tabData->ViewPort);
	vpH = gtk_widget_get_allocated_height(tabData->ViewPort);
	daW = tabData->drawing_area != NULL ?
			gtk_widget_get_allocated_width(tabData->drawing_area) : -1;
	daH = tabData->drawing_area != NULL ?
			gtk_widget_get_allocated_height(tabData->drawing_area) : -1;

	G3DBG(
			"%s: vp_alloc=%dx%d da_alloc=%dx%d hadj[v=%.2f lo=%.2f up=%.2f page=%.2f] vadj[v=%.2f lo=%.2f up=%.2f page=%.2f] zoom=%.6f origin=(%.2f,%.2f) canvas=(%.2f,%.2f) image=%dx%d zoom_to_fit=%d\n",
			tag, vpW, vpH, daW, daH, hVal, hLower, hUpper, hPage, vVal, vLower, vUpper,
			vPage, tabData->viewZoom, tabData->viewOrigin[0], tabData->viewOrigin[1],
			tabData->viewCanvasSize[0], tabData->viewCanvasSize[1], tabData->XSize,
			tabData->YSize, tabData->zoomedToFit);
#else
	(void) tag;
	(void) tabData;
#endif
}

/****************************************************************/
/* This function closes the window when the application is 	*/
/* killed.							*/
/****************************************************************/
gint closeApplicationHandler(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gtk_main_quit(); /* Quit gtk */
	return FALSE;
}

static void freeRecentFileEntry(gpointer data) {
	struct RecentFileEntry *entry;
	entry = (struct RecentFileEntry *) data;
	if (entry == NULL)
		return;
	g_free(entry->path);
	g_free(entry->date);
	g_free(entry);
}

static gchar *getRecentFilesPath(void) {
	return g_build_filename(g_get_user_config_dir(), "g3data3",
			"recent-files.ini", NULL);
}

static gchar *getExportPreferencesPath(void) {
	return g_build_filename(g_get_user_config_dir(), "g3data2",
			"preferences.ini", NULL);
}

static void loadExportPreferences(void) {
	GKeyFile *key_file;
	GError *error;
	gchar *path;
	gint ordering;

	key_file = g_key_file_new();
	path = getExportPreferencesPath();
	error = NULL;
	if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
		g_clear_error(&error);
		g_free(path);
		g_key_file_unref(key_file);
		return;
	}

	error = NULL;
	ordering = g_key_file_get_integer(key_file, EXPORT_PREF_GROUP,
			EXPORT_ORDERING_KEY, &error);
	if (error == NULL && ordering >= 0 && ordering < ORDERBNUM)
		exportOrdering = ordering;
	g_clear_error(&error);

	error = NULL;
	exportUseErrors = g_key_file_get_boolean(key_file, EXPORT_PREF_GROUP,
			EXPORT_ERRORS_KEY, &error);
	if (error != NULL) {
		exportUseErrors = FALSE;
		g_clear_error(&error);
	}

	g_free(path);
	g_key_file_unref(key_file);
}

static void saveExportPreferences(void) {
	GKeyFile *key_file;
	GError *error;
	gchar *path, *dirpath, *contents;
	gsize length;

	key_file = g_key_file_new();
	g_key_file_set_integer(key_file, EXPORT_PREF_GROUP, EXPORT_ORDERING_KEY,
			exportOrdering);
	g_key_file_set_boolean(key_file, EXPORT_PREF_GROUP, EXPORT_ERRORS_KEY,
			exportUseErrors);
	contents = g_key_file_to_data(key_file, &length, NULL);
	path = getExportPreferencesPath();
	dirpath = g_path_get_dirname(path);
	g_mkdir_with_parents(dirpath, 0755);
	error = NULL;
	if (!g_file_set_contents(path, contents, length, &error)) {
		g_warning("Could not save export preferences: %s", error->message);
		g_error_free(error);
	}

	g_free(dirpath);
	g_free(path);
	g_free(contents);
	g_key_file_unref(key_file);
}

static gchar *getNowIsoTimestamp(void) {
	GDateTime *dt;
	gchar *stamp;
	dt = g_date_time_new_now_local();
	stamp = g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S%z");
	g_date_time_unref(dt);
	return stamp;
}

static gint getMenuItemIndex(GtkWidget *menu, GtkWidget *item) {
	GList *children, *iter;
	gint idx;

	children = gtk_container_get_children(GTK_CONTAINER(menu));
	idx = 0;
	for (iter = children; iter != NULL; iter = iter->next, idx++) {
		if (iter->data == item) {
			g_list_free(children);
			return idx;
		}
	}
	g_list_free(children);
	return -1;
}

static void clearRecentFileMenuItems(void) {
	guint i;

	if (recent_menu_items == NULL || file_menu_widget == NULL)
		return;
	for (i = 0; i < recent_menu_items->len; i++) {
		GtkWidget *item;
		item = (GtkWidget *) g_ptr_array_index(recent_menu_items, i);
		gtk_container_remove(GTK_CONTAINER(file_menu_widget), item);
	}
	g_ptr_array_set_size(recent_menu_items, 0);
}

static gint countDataTabs(void) {
	gint i, count;
	count = 0;
	if (mainnotebook == NULL)
		return 0;
	for (i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(mainnotebook)); i++) {
		GtkWidget *page;
		page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(mainnotebook), i);
		if (g_object_get_data(G_OBJECT(page), DATA_STORE_NAME) != NULL)
			count++;
	}
	return count;
}

static void hideStartPage(void) {
	GtkWidget *parent;
	if (start_page_widget == NULL)
		return;
	parent = gtk_widget_get_parent(start_page_widget);
	if (parent != NULL)
		gtk_container_remove(GTK_CONTAINER(parent), start_page_widget);
	start_page_widget = NULL;
	start_icon_view_widget = NULL;
	if (mainnotebook != NULL)
		gtk_widget_show(mainnotebook);
}

static void openRecentPath(const gchar *path) {
	if (path == NULL || *path == '\0')
		return;
	setupNewTab((char *) path, 1.0, -1, -1, FALSE, NULL, NULL, NULL);
}

static void updateStartIconViewLayout(GtkWidget *icon_view, gint host_width) {
	gint available_w, columns, item_w;
	const gint spacing = 10;
	const gint margin = 6;

	if (icon_view == NULL || host_width <= 1)
		return;

	available_w = host_width - 2 * margin;
	if (available_w <= START_TILE_MIN_ITEM_W) {
		columns = 1;
	} else {
		columns = available_w / START_TILE_MIN_ITEM_W;
		if (columns < 1)
			columns = 1;
		if (columns > START_TILE_COLUMNS)
			columns = START_TILE_COLUMNS;
	}

	item_w = (available_w - (columns - 1) * spacing) / columns;
	if (item_w < START_TILE_MIN_ITEM_W)
		item_w = START_TILE_MIN_ITEM_W;
	if (item_w > START_TILE_MAX_ITEM_W)
		item_w = START_TILE_MAX_ITEM_W;

	gtk_icon_view_set_columns(GTK_ICON_VIEW(icon_view), columns);
	gtk_icon_view_set_item_width(GTK_ICON_VIEW(icon_view), item_w);
	gtk_widget_queue_resize(icon_view);
	gtk_widget_queue_draw(icon_view);
}

static void startIconViewSizeAllocateEvent(GtkWidget *widget,
		GtkAllocation *allocation, gpointer data) {
	(void) data;
	updateStartIconViewLayout(widget, allocation->width);
}

static void startIconHostSizeAllocateEvent(GtkWidget *widget,
		GtkAllocation *allocation, gpointer data) {
	GtkWidget *icon_view;
	GtkAdjustment *hadj, *vadj;
	(void) widget;
	icon_view = GTK_WIDGET(data);
	hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(widget));
	vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(widget));
	if (hadj != NULL)
		gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
	if (vadj != NULL)
		gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
	updateStartIconViewLayout(icon_view, allocation->width);
	gtk_widget_queue_draw(widget);
	gtk_widget_queue_draw(icon_view);
}

enum StartIconColumns {
	START_ICON_COL_PIXBUF = 0, START_ICON_COL_NAME, START_ICON_COL_PATH,
	START_ICON_COL_TOOLTIP, START_ICON_COL_COUNT
};

static void startIconItemActivated(GtkIconView *icon_view, GtkTreePath *path,
		gpointer data) {
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *full_path;
	(void) data;

	model = gtk_icon_view_get_model(icon_view);
	if (model == NULL || !gtk_tree_model_get_iter(model, &iter, path))
		return;

	gtk_tree_model_get(model, &iter, START_ICON_COL_PATH, &full_path, -1);
	openRecentPath(full_path);
	g_free(full_path);
}

static void showStartPageIfNeeded(void) {
	GtkWidget *outer, *scrolled, *icon_view, *parent;
	GtkListStore *store;
	GtkIconTheme *theme;
	gint i;

	if (mainnotebook == NULL)
		return;
	if (countDataTabs() > 0) {
		hideStartPage();
		gtk_widget_show(mainnotebook);
		return;
	}
	hideStartPage();
	parent = gtk_widget_get_parent(mainnotebook);
	if (parent == NULL)
		return;

	outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(outer), 0);
	gtk_widget_set_hexpand(outer, TRUE);
	gtk_widget_set_vexpand(outer, TRUE);
	gtk_widget_set_halign(outer, GTK_ALIGN_FILL);
	gtk_widget_set_valign(outer, GTK_ALIGN_FILL);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, TRUE);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_widget_set_halign(scrolled, GTK_ALIGN_FILL);
	gtk_widget_set_valign(scrolled, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);

	store = gtk_list_store_new(START_ICON_COL_COUNT, GDK_TYPE_PIXBUF, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING);
	theme = gtk_icon_theme_get_default();

	for (i = 0; recent_files != NULL && i < (gint) recent_files->len; i++) {
		struct RecentFileEntry *entry;
		GdkPixbuf *pixbuf;
		GtkTreeIter iter;
		gchar *base;
		GError *pix_err;

		entry = (struct RecentFileEntry *) g_ptr_array_index(recent_files, i);
		pix_err = NULL;
		pixbuf = gdk_pixbuf_new_from_file_at_scale(entry->path, START_TILE_THUMB_W,
				START_TILE_THUMB_H, TRUE, &pix_err);
		if (pixbuf == NULL) {
			if (pix_err != NULL)
				g_error_free(pix_err);
			pixbuf = gtk_icon_theme_load_icon(theme, "image-missing", 64, 0, NULL);
		}

		base = g_path_get_basename(entry->path);
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, START_ICON_COL_PIXBUF, pixbuf,
				START_ICON_COL_NAME, base, START_ICON_COL_PATH, entry->path,
				START_ICON_COL_TOOLTIP, entry->path, -1);
		if (pixbuf != NULL)
			g_object_unref(pixbuf);
		g_free(base);
	}

	icon_view = gtk_icon_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(icon_view), START_ICON_COL_PIXBUF);
	gtk_icon_view_set_text_column(GTK_ICON_VIEW(icon_view), START_ICON_COL_NAME);
	gtk_icon_view_set_tooltip_column(GTK_ICON_VIEW(icon_view),
			START_ICON_COL_TOOLTIP);
	gtk_icon_view_set_activate_on_single_click(GTK_ICON_VIEW(icon_view), TRUE);
	gtk_icon_view_set_columns(GTK_ICON_VIEW(icon_view), START_TILE_COLUMNS);
	gtk_icon_view_set_margin(GTK_ICON_VIEW(icon_view), 6);
	gtk_icon_view_set_row_spacing(GTK_ICON_VIEW(icon_view), 10);
	gtk_icon_view_set_column_spacing(GTK_ICON_VIEW(icon_view), 10);
	gtk_widget_set_hexpand(icon_view, TRUE);
	gtk_widget_set_vexpand(icon_view, TRUE);
	gtk_widget_set_halign(icon_view, GTK_ALIGN_FILL);
	gtk_widget_set_valign(icon_view, GTK_ALIGN_START);
	g_signal_connect(G_OBJECT(icon_view), "size-allocate",
			G_CALLBACK(startIconViewSizeAllocateEvent), NULL);
	g_signal_connect(G_OBJECT(scrolled), "size-allocate",
			G_CALLBACK(startIconHostSizeAllocateEvent), icon_view);
	g_signal_connect(G_OBJECT(icon_view), "item-activated",
			G_CALLBACK(startIconItemActivated), NULL);
	g_object_unref(store);

	gtk_container_add(GTK_CONTAINER(scrolled), icon_view);

	if (recent_files == NULL || recent_files->len == 0)
		gtk_widget_set_sensitive(icon_view, FALSE);

	start_page_widget = outer;
	start_icon_view_widget = icon_view;
	gtk_box_pack_start(GTK_BOX(parent), start_page_widget, TRUE, TRUE, 0);
	gtk_widget_show_all(start_page_widget);
	gtk_widget_hide(mainnotebook);
	gtk_widget_set_sensitive(close_menu_item, FALSE);
	updateEditMenuSensitivity(NULL);
	gtk_window_set_title(GTK_WINDOW(window), Window_Title_NoneOpen);
}

static gint windowConfigureEvent(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gint host_w;
	(void) widget;
	(void) event;
	(void) data;

	if (countDataTabs() == 0)
		showStartPageIfNeeded();

	if (start_page_widget == NULL || start_icon_view_widget == NULL)
		return FALSE;

	/* Keep notebook hidden whenever the standalone start page is active. */
	gtk_widget_hide(mainnotebook);
	gtk_widget_show_all(start_page_widget);

	host_w = gtk_widget_get_allocated_width(start_page_widget);
	updateStartIconViewLayout(start_icon_view_widget, host_w);
	gtk_widget_queue_draw(start_page_widget);
	gtk_widget_queue_draw(start_icon_view_widget);
	return FALSE;
}

static void saveRecentFiles(void) {
	GKeyFile *kf;
	gchar *path, *dirpath;
	gchar *out;
	gsize out_len;
	guint i;
	gchar keybuf[32];

	if (recent_files == NULL)
		return;

	kf = g_key_file_new();
	for (i = 0; i < recent_files->len && i < (guint) MAX_RECENT_FILES; i++) {
		struct RecentFileEntry *entry;
		entry = (struct RecentFileEntry *) g_ptr_array_index(recent_files, i);
		snprintf(keybuf, sizeof(keybuf), RECENT_PATH_KEY_FMT, (int) i);
		g_key_file_set_string(kf, RECENT_GROUP, keybuf, entry->path);
		snprintf(keybuf, sizeof(keybuf), RECENT_DATE_KEY_FMT, (int) i);
		g_key_file_set_string(kf, RECENT_GROUP, keybuf, entry->date);
	}

	out = g_key_file_to_data(kf, &out_len, NULL);
	path = getRecentFilesPath();
	dirpath = g_path_get_dirname(path);
	g_mkdir_with_parents(dirpath, 0755);
	g_file_set_contents(path, out, out_len, NULL);

	g_free(dirpath);
	g_free(path);
	g_free(out);
	g_key_file_unref(kf);
}

void recentFileActivate(GtkWidget *widget, gpointer data) {
	const gchar *path;
	(void) data;
	path = (const gchar *) g_object_get_data(G_OBJECT(widget), "recent-path");
	openRecentPath(path);
}

static void rebuildRecentFileMenu(void) {
	gint insert_at;
	guint i;

	if (file_menu_widget == NULL || close_menu_item == NULL || recent_files == NULL)
		return;

	clearRecentFileMenuItems();
	insert_at = getMenuItemIndex(file_menu_widget, close_menu_item);
	if (insert_at < 0)
		return;

	if (recent_files->len == 0) {
		GtkWidget *empty_item;
		empty_item = gtk_menu_item_new_with_label("(No recent files)");
		gtk_widget_set_sensitive(empty_item, FALSE);
		gtk_menu_shell_insert(GTK_MENU_SHELL(file_menu_widget), empty_item,
				insert_at);
		g_ptr_array_add(recent_menu_items, empty_item);
		showStartPageIfNeeded();
		return;
	}

	for (i = 0; i < recent_files->len; i++) {
		struct RecentFileEntry *entry;
		GtkWidget *item;
		gchar *base;
		gchar *label;

		entry = (struct RecentFileEntry *) g_ptr_array_index(recent_files, i);
		base = g_path_get_basename(entry->path);
		label = g_strdup_printf("%s  (%s)", base, entry->date);
		item = gtk_menu_item_new_with_label(label);
		g_object_set_data_full(G_OBJECT(item), "recent-path",
				g_strdup(entry->path), g_free);
		gtk_widget_set_tooltip_text(item, entry->path);
		g_signal_connect(G_OBJECT(item), "activate",
				G_CALLBACK(recentFileActivate), NULL);
		gtk_menu_shell_insert(GTK_MENU_SHELL(file_menu_widget), item, insert_at + i);
		g_ptr_array_add(recent_menu_items, item);
		g_free(label);
		g_free(base);
	}
	showStartPageIfNeeded();
}

static void loadRecentFiles(void) {
	GKeyFile *kf;
	gchar *path;
	guint i;
	gchar keybuf[32];
	GError *err;

	if (recent_files == NULL)
		recent_files = g_ptr_array_new_with_free_func(freeRecentFileEntry);
	else
		g_ptr_array_set_size(recent_files, 0);

	kf = g_key_file_new();
	path = getRecentFilesPath();
	err = NULL;
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		if (err != NULL)
			g_error_free(err);
		g_free(path);
		g_key_file_unref(kf);
		return;
	}

	for (i = 0; i < (guint) MAX_RECENT_FILES; i++) {
		gchar *p, *d;
		struct RecentFileEntry *entry;

		snprintf(keybuf, sizeof(keybuf), RECENT_PATH_KEY_FMT, (int) i);
		p = g_key_file_get_string(kf, RECENT_GROUP, keybuf, NULL);
		if (p == NULL || *p == '\0') {
			g_free(p);
			continue;
		}
		snprintf(keybuf, sizeof(keybuf), RECENT_DATE_KEY_FMT, (int) i);
		d = g_key_file_get_string(kf, RECENT_GROUP, keybuf, NULL);
		if (d == NULL || *d == '\0') {
			g_free(d);
			d = g_strdup("");
		}
		entry = g_new0(struct RecentFileEntry, 1);
		entry->path = p;
		entry->date = d;
		g_ptr_array_add(recent_files, entry);
	}

	g_free(path);
	g_key_file_unref(kf);
}

static void addRecentFile(const gchar *filename) {
	gchar *canon;
	gchar *now;
	guint i;
	struct RecentFileEntry *entry;

	if (filename == NULL || *filename == '\0')
		return;
	if (recent_files == NULL)
		recent_files = g_ptr_array_new_with_free_func(freeRecentFileEntry);

	canon = g_canonicalize_filename(filename, NULL);
	for (i = 0; i < recent_files->len; i++) {
		struct RecentFileEntry *cur;
		cur = (struct RecentFileEntry *) g_ptr_array_index(recent_files, i);
		if (g_strcmp0(cur->path, canon) == 0) {
			g_ptr_array_remove_index(recent_files, i);
			break;
		}
	}

	now = getNowIsoTimestamp();
	entry = g_new0(struct RecentFileEntry, 1);
	entry->path = canon;
	entry->date = now;
	g_ptr_array_insert(recent_files, 0, entry);

	while (recent_files->len > (guint) MAX_RECENT_FILES)
		g_ptr_array_remove_index(recent_files, recent_files->len - 1);

	saveRecentFiles();
	rebuildRecentFileMenu();
}

gboolean updateZoomArea(GtkWidget *widget, cairo_t *cr, gpointer data) {
	cairo_t *first_cr;
	cairo_surface_t *first;
	struct TabData *tabData;
	gdouble zoomAreaScale;

	tabData = (struct TabData *) data;

	if (tabData->mousePointerCoords[0] >= 0 && tabData->mousePointerCoords[1] >= 0) {
		zoomAreaScale = tabData->viewZoom * ZOOM_AREA_VIEW_MULTIPLIER;
		if (zoomAreaScale <= 0.0)
			zoomAreaScale = ZOOM_AREA_VIEW_MULTIPLIER;

		first = cairo_surface_create_similar(cairo_get_target(cr),
				CAIRO_CONTENT_COLOR, ZOOMPIXSIZE, ZOOMPIXSIZE);

		first_cr = cairo_create(first);
		cairo_scale(first_cr, zoomAreaScale, zoomAreaScale);
		cairo_set_source_surface(
				first_cr,
				tabData->image,
				-tabData->mousePointerCoords[0] * tabData->imageScale
						+ ZOOMPIXSIZE / (2 * zoomAreaScale),
				-tabData->mousePointerCoords[1] * tabData->imageScale
						+ ZOOMPIXSIZE / (2 * zoomAreaScale));
		cairo_paint(first_cr);
		cairo_scale(first_cr, 1.0 / zoomAreaScale, 1.0 / zoomAreaScale);

		drawMarker(first_cr, ZOOMPIXSIZE / 2, ZOOMPIXSIZE / 2, 2);

		cairo_set_source_surface(cr, first, 0, 0);
		cairo_paint(cr);

		cairo_surface_destroy(first);

		cairo_destroy(first_cr);
	}

	return TRUE;
}

static gdouble maximumZoomForImage(const struct TabData *tabData) {
	gdouble largest_dimension, canvas_limited_zoom;

	if (tabData == NULL)
		return MAIN_IMAGE_MAX_ZOOM;
	largest_dimension = MAX(tabData->XSize, tabData->YSize);
	if (largest_dimension <= 0.0)
		return MAIN_IMAGE_MAX_ZOOM;
	canvas_limited_zoom = (MAIN_IMAGE_MAX_CANVAS_DIMENSION
			- 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD) / largest_dimension;
	return CLAMP(canvas_limited_zoom, MAIN_IMAGE_MIN_ZOOM,
			MAIN_IMAGE_MAX_ZOOM);
}

static gdouble clampZoom(const struct TabData *tabData, gdouble zoom) {
	gdouble maximum_zoom;

	maximum_zoom = maximumZoomForImage(tabData);
	if (zoom < MAIN_IMAGE_MIN_ZOOM)
		return MAIN_IMAGE_MIN_ZOOM;
	if (zoom > maximum_zoom)
		return maximum_zoom;
	return zoom;
}

static gdouble getAdjustmentUpperBound(GtkAdjustment *adjustment) {
	gdouble lower, upper;
	lower = gtk_adjustment_get_lower(adjustment);
	upper = gtk_adjustment_get_upper(adjustment)
			- gtk_adjustment_get_page_size(adjustment);
	if (upper < lower)
		upper = lower;
	return upper;
}

static gdouble clampAdjustmentValue(GtkAdjustment *adjustment, gdouble value) {
	gdouble lower, upper;

	lower = gtk_adjustment_get_lower(adjustment);
	upper = getAdjustmentUpperBound(adjustment);

	if (value < lower)
		return lower;
	if (value > upper)
		return upper;
	return value;
}

static gboolean adjustmentBoundsCoverCanvas(struct TabData *tabData,
		gdouble canvasW, gdouble canvasH) {
	GtkAdjustment *hadj, *vadj;
	gdouble hUpper, vUpper;

	if (tabData == NULL || tabData->ViewPort == NULL)
		return FALSE;

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	hUpper = gtk_adjustment_get_upper(hadj);
	vUpper = gtk_adjustment_get_upper(vadj);

	return hUpper + 1.0 >= canvasW && vUpper + 1.0 >= canvasH;
}

static void maybeApplyPendingZoomScroll(struct TabData *tabData) {
	GtkAdjustment *hadj, *vadj;
	gdouble hNew, vNew;

	if (tabData == NULL || !tabData->pendingZoomScrollOnAdjust
			|| tabData->ViewPort == NULL)
		return;

	if (!adjustmentBoundsCoverCanvas(tabData,
			tabData->pendingZoomScrollCanvasSize[0],
			tabData->pendingZoomScrollCanvasSize[1])) {
		G3DBG(
				"maybeApplyPendingZoomScroll: waiting target=(%.2f,%.2f) canvas=(%.2f,%.2f)\n",
				tabData->pendingZoomScrollTarget[0],
				tabData->pendingZoomScrollTarget[1],
				tabData->pendingZoomScrollCanvasSize[0],
				tabData->pendingZoomScrollCanvasSize[1]);
		return;
	}

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	hNew = clampAdjustmentValue(hadj, tabData->pendingZoomScrollTarget[0]);
	vNew = clampAdjustmentValue(vadj, tabData->pendingZoomScrollTarget[1]);

	gtk_adjustment_set_value(hadj, hNew);
	gtk_adjustment_set_value(vadj, vNew);
	tabData->pendingZoomScrollOnAdjust = FALSE;
	G3DBG("maybeApplyPendingZoomScroll: applied target=(%.2f,%.2f)\n",
			hNew, vNew);
	debugDumpViewportState("maybeApplyPendingZoomScroll:end", tabData);
}

static void getViewportSize(struct TabData *tabData, gdouble *pageW, gdouble *pageH) {
	GtkAdjustment *hadj, *vadj;
	gdouble allocW, allocH, pageAdjW, pageAdjH;

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	allocW = gtk_widget_get_allocated_width(tabData->ViewPort);
	allocH = gtk_widget_get_allocated_height(tabData->ViewPort);
	pageAdjW = gtk_adjustment_get_page_size(hadj);
	pageAdjH = gtk_adjustment_get_page_size(vadj);

	if (allocW > 1.0 && pageAdjW > 1.0)
		*pageW = MIN(allocW, pageAdjW);
	else if (allocW > 1.0)
		*pageW = allocW;
	else
		*pageW = pageAdjW;

	if (allocH > 1.0 && pageAdjH > 1.0)
		*pageH = MIN(allocH, pageAdjH);
	else if (allocH > 1.0)
		*pageH = allocH;
	else
		*pageH = pageAdjH;
}

static void setMainImageZoom(struct TabData *tabData, gdouble newZoom,
		gdouble focusX, gdouble focusY) {
	GtkAdjustment *hadj, *vadj;
	gdouble oldZoom;
	gdouble oldOriginX, oldOriginY;
	gdouble hvalue, vvalue;
	gdouble hImgFocus, vImgFocus;
	gdouble hNewValue, vNewValue;
	gdouble pageW, pageH;
	gdouble newOriginX, newOriginY;
	gdouble hLower, vLower, hMax, vMax;
	gdouble newImageW, newImageH;
	gdouble newCanvasW, newCanvasH;
	gint newWidth, newHeight;

	newZoom = clampZoom(tabData, newZoom);
	oldZoom = tabData->viewZoom;
	oldOriginX = tabData->viewOrigin[0];
	oldOriginY = tabData->viewOrigin[1];
	debugDumpViewportState("setMainImageZoom:begin", tabData);

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	hvalue = gtk_adjustment_get_value(hadj);
	vvalue = gtk_adjustment_get_value(vadj);
	getViewportSize(tabData, &pageW, &pageH);

	if (focusX < 0)
		focusX = pageW / 2.0;
	if (focusY < 0)
		focusY = pageH / 2.0;

	hImgFocus = (hvalue + focusX - oldOriginX) / oldZoom;
	vImgFocus = (vvalue + focusY - oldOriginY) / oldZoom;

	tabData->viewZoom = newZoom;
	newImageW = tabData->XSize * tabData->viewZoom;
	newImageH = tabData->YSize * tabData->viewZoom;
	newOriginX = MAIN_IMAGE_CANVAS_MIN_PAD;
	newOriginY = MAIN_IMAGE_CANVAS_MIN_PAD;
	newCanvasW = newImageW + 2.0 * newOriginX;
	newCanvasH = newImageH + 2.0 * newOriginY;

	tabData->viewOrigin[0] = newOriginX;
	tabData->viewOrigin[1] = newOriginY;
	tabData->viewCanvasSize[0] = newCanvasW;
	tabData->viewCanvasSize[1] = newCanvasH;

	newWidth = (gint) newCanvasW;
	newHeight = (gint) newCanvasH;
	gtk_widget_set_size_request(tabData->drawing_area, newWidth, newHeight);
	gtk_widget_queue_resize(tabData->drawing_area);
	gtk_widget_queue_resize(tabData->ViewPort);
	gtk_widget_queue_draw(tabData->drawing_area);
	if (tabData->zoom_area != NULL)
		gtk_widget_queue_draw(tabData->zoom_area);

	hNewValue = hImgFocus * newZoom + newOriginX - focusX;
	vNewValue = vImgFocus * newZoom + newOriginY - focusY;
	hLower = gtk_adjustment_get_lower(hadj);
	vLower = gtk_adjustment_get_lower(vadj);
	hMax = hLower + MAX(newCanvasW - pageW, 0.0);
	vMax = vLower + MAX(newCanvasH - pageH, 0.0);

	if (hNewValue < hLower)
		hNewValue = hLower;
	if (hNewValue > hMax)
		hNewValue = hMax;

	if (vNewValue < vLower)
		vNewValue = vLower;
	if (vNewValue > vMax)
		vNewValue = vMax;

	tabData->pendingZoomScrollTarget[0] = hNewValue;
	tabData->pendingZoomScrollTarget[1] = vNewValue;
	tabData->pendingZoomScrollCanvasSize[0] = newCanvasW;
	tabData->pendingZoomScrollCanvasSize[1] = newCanvasH;
	tabData->pendingZoomScrollOnAdjust = TRUE;
	maybeApplyPendingZoomScroll(tabData);
	G3DBG(
			"setMainImageZoom:end newZoom=%.6f focus=(%.2f,%.2f) page=(%.2f,%.2f) newOrigin=(%.2f,%.2f) newCanvas=(%.2f,%.2f) newAdj=(%.2f,%.2f)\n",
			newZoom, focusX, focusY, pageW, pageH, newOriginX, newOriginY, newCanvasW,
			newCanvasH, hNewValue, vNewValue);
	debugDumpViewportState("setMainImageZoom:end", tabData);
}

static gdouble calculateZoomToFit(struct TabData *tabData) {
	gdouble pageW, pageH;
	gdouble fitX, fitY;

	getViewportSize(tabData, &pageW, &pageH);
	if (pageW <= 1.0 || pageH <= 1.0)
		return 1.0;

	fitX = pageW / tabData->XSize;
	fitY = pageH / tabData->YSize;
	return clampZoom(tabData, MIN(fitX, fitY));
}

static void centerImageInView(struct TabData *tabData) {
	GtkAdjustment *hadj, *vadj;
	gdouble pageW, pageH;
	gdouble imageW, imageH;
	gdouble hLower, vLower, hMax, vMax;
	gdouble hNew, vNew;

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	getViewportSize(tabData, &pageW, &pageH);

	imageW = tabData->XSize * tabData->viewZoom;
	imageH = tabData->YSize * tabData->viewZoom;

	hNew = tabData->viewOrigin[0] + imageW / 2.0 - pageW / 2.0;
	vNew = tabData->viewOrigin[1] + imageH / 2.0 - pageH / 2.0;

	hLower = gtk_adjustment_get_lower(hadj);
	vLower = gtk_adjustment_get_lower(vadj);
	hMax = hLower + MAX(tabData->viewCanvasSize[0] - pageW, 0.0);
	vMax = vLower + MAX(tabData->viewCanvasSize[1] - pageH, 0.0);

	if (hNew < hLower)
		hNew = hLower;
	if (hNew > hMax)
		hNew = hMax;
	if (vNew < vLower)
		vNew = vLower;
	if (vNew > vMax)
		vNew = vMax;

	gtk_adjustment_set_value(hadj, hNew);
	gtk_adjustment_set_value(vadj, vNew);
	G3DBG(
			"centerImageInView: page=(%.2f,%.2f) image=(%.2f,%.2f) targetAdj=(%.2f,%.2f)\n",
			pageW, pageH, imageW, imageH, hNew, vNew);
	debugDumpViewportState("centerImageInView:end", tabData);
}

static void maybeApplyPendingRecenter(struct TabData *tabData) {
	if (tabData == NULL || !tabData->pendingRecenterOnAdjust
			|| tabData->ViewPort == NULL)
		return;

	if (!adjustmentBoundsCoverCanvas(tabData, tabData->viewCanvasSize[0],
			tabData->viewCanvasSize[1])) {
#ifdef G3DATA2_DEBUG
		GtkAdjustment *hadj, *vadj;
		gdouble hUpper, vUpper;

		hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
		vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
		hUpper = gtk_adjustment_get_upper(hadj);
		vUpper = gtk_adjustment_get_upper(vadj);
		G3DBG(
				"maybeApplyPendingRecenter: waiting (bounds too small) upper=(%.2f,%.2f) canvas=(%.2f,%.2f)\n",
				hUpper, vUpper, tabData->viewCanvasSize[0], tabData->viewCanvasSize[1]);
#endif
		return;
	}

	debugDumpViewportState("maybeApplyPendingRecenter:begin", tabData);
	centerImageInView(tabData);
	tabData->pendingRecenterOnAdjust = FALSE;
	debugDumpViewportState("maybeApplyPendingRecenter:end", tabData);
}

static void adjustmentChangedEvent(GtkAdjustment *adjustment, gpointer data) {
	struct TabData *tabData;
	(void) adjustment;

	tabData = (struct TabData *) data;
	debugDumpViewportState("adjustmentChangedEvent", tabData);
	maybeApplyPendingZoomScroll(tabData);
	maybeApplyPendingRecenter(tabData);
}

static void zoomToFitAndCenter(struct TabData *tabData) {
	debugDumpViewportState("zoomToFitAndCenter:begin", tabData);
	setMainImageZoom(tabData, calculateZoomToFit(tabData), -1.0, -1.0);
	tabData->pendingRecenterOnAdjust = TRUE;
	maybeApplyPendingRecenter(tabData);
	debugDumpViewportState("zoomToFitAndCenter:end", tabData);
}

static void disableZoomToFit(struct TabData *tabData) {
	if (tabData == NULL)
		return;
	tabData->zoomedToFit = FALSE;
	tabData->fittedViewportWidth = -1;
	tabData->fittedViewportHeight = -1;
	tabData->pendingZoomScrollOnAdjust = FALSE;
	tabData->pendingRecenterOnAdjust = FALSE;
}

static void applyStickyZoomToFit(struct TabData *tabData, gboolean force) {
	gint viewport_width, viewport_height;

	if (tabData == NULL || !tabData->zoomedToFit
			|| tabData->drawing_area == NULL || tabData->ViewPort == NULL)
		return;
	if (tabData->XSize <= 0 || tabData->YSize <= 0)
		return;
	viewport_width = gtk_widget_get_allocated_width(tabData->ViewPort);
	viewport_height = gtk_widget_get_allocated_height(tabData->ViewPort);
	if (viewport_width <= 1 || viewport_height <= 1)
		return;
	if (!force && viewport_width == tabData->fittedViewportWidth
			&& viewport_height == tabData->fittedViewportHeight)
		return;

	tabData->fittedViewportWidth = viewport_width;
	tabData->fittedViewportHeight = viewport_height;
	debugDumpViewportState("applyStickyZoomToFit:ready", tabData);
	zoomToFitAndCenter(tabData);
	debugDumpViewportState("applyStickyZoomToFit:done", tabData);
}

static gboolean applyInitialZoomToFit(gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	debugDumpViewportState("applyInitialZoomToFit", tabData);
	applyStickyZoomToFit(tabData, FALSE);
	return G_SOURCE_REMOVE;
}

static void viewportSizeAllocateEvent(GtkWidget *widget, GtkAllocation *allocation,
		gpointer data) {
	(void) widget;
	G3DBG("viewportSizeAllocateEvent: alloc=%dx%d\n", allocation->width,
			allocation->height);
	applyStickyZoomToFit((struct TabData *) data, FALSE);
	maybeApplyPendingZoomScroll((struct TabData *) data);
	maybeApplyPendingRecenter((struct TabData *) data);
}

static void getImageCoords(struct TabData *tabData, gdouble widgetX,
		gdouble widgetY, gdouble *imageX, gdouble *imageY) {
	if (tabData->viewZoom <= 0)
		tabData->viewZoom = 1.0;
	if (tabData->imageScale <= 0)
		tabData->imageScale = 1.0;
	*imageX = (widgetX - tabData->viewOrigin[0])
			/ (tabData->viewZoom * tabData->imageScale);
	*imageY = (widgetY - tabData->viewOrigin[1])
			/ (tabData->viewZoom * tabData->imageScale);
}

static GtkWidget *g3TableNew(guint rows, guint columns, gboolean homogeneous) {
	GtkWidget *grid = gtk_grid_new();
	(void) rows;
	(void) columns;
	gtk_grid_set_row_homogeneous(GTK_GRID(grid), homogeneous);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), homogeneous);
	return grid;
}

static void g3TableSetRowSpacings(GtkWidget *grid, guint spacing) {
	gtk_grid_set_row_spacing(GTK_GRID(grid), spacing);
}

static void g3TableSetColSpacings(GtkWidget *grid, guint spacing) {
	gtk_grid_set_column_spacing(GTK_GRID(grid), spacing);
}

static void g3TableAttach(GtkWidget *grid, GtkWidget *child, guint left,
		guint right, guint top, guint bottom, guint xoptions, guint yoptions,
		guint xpadding, guint ypadding) {
	gtk_widget_set_hexpand(child, (xoptions & GTK_EXPAND) != 0);
	gtk_widget_set_vexpand(child, (yoptions & GTK_EXPAND) != 0);

	if (xoptions & GTK_FILL)
		gtk_widget_set_halign(child, GTK_ALIGN_FILL);
	else
		gtk_widget_set_halign(child, GTK_ALIGN_START);

	if (yoptions & GTK_FILL)
		gtk_widget_set_valign(child, GTK_ALIGN_FILL);
	else
		gtk_widget_set_valign(child, GTK_ALIGN_START);

	gtk_widget_set_margin_start(child, xpadding);
	gtk_widget_set_margin_end(child, xpadding);
	gtk_widget_set_margin_top(child, ypadding);
	gtk_widget_set_margin_bottom(child, ypadding);

	gtk_grid_attach(GTK_GRID(grid), child, left, top, right - left, bottom - top);
}

static GtkWidget *g3AlignmentNew(gfloat xalign, gfloat yalign, gfloat xscale,
		gfloat yscale) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	(void) xscale;
	(void) yscale;
	if (xalign <= 0.01f)
		gtk_widget_set_halign(box, GTK_ALIGN_START);
	else if (xalign >= 0.99f)
		gtk_widget_set_halign(box, GTK_ALIGN_END);
	else
		gtk_widget_set_halign(box, GTK_ALIGN_FILL);

	if (yalign <= 0.01f)
		gtk_widget_set_valign(box, GTK_ALIGN_START);
	else if (yalign >= 0.99f)
		gtk_widget_set_valign(box, GTK_ALIGN_END);
	else
		gtk_widget_set_valign(box, GTK_ALIGN_FILL);

	return box;
}

static void applyMiddleButtonAxisShortcut(struct TabData *tabData, gdouble imageX,
		gdouble imageY) {
	gint i, j;

	for (i = 0; i < 2; i++) {
		if (!tabData->bpressed[i]) {
			tabData->axiscoords[i][0] = imageX;
			tabData->axiscoords[i][1] = imageY;
			for (j = 0; j < 4; j++)
				if (i != j)
					gtk_widget_set_sensitive(tabData->setxybutton[j], TRUE);
			gtk_widget_set_sensitive(tabData->xyentry[i], TRUE);
			gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i], TRUE);
			gtk_widget_grab_focus(tabData->xyentry[i]);
			tabData->setxypressed[i] = FALSE;
			tabData->bpressed[i] = TRUE;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tabData->setxybutton[i]),
					FALSE);
			setButtonSensitivity(tabData);
			triggerUpdateDrawArea(tabData->drawing_area);
			tabData->mousePointerCoords[0] = imageX;
			tabData->mousePointerCoords[1] = imageY;
			refreshProcessingInformation(tabData);
			persistCalibration(tabData);
			break;
		}
	}
}

gboolean updateImageArea(GtkWidget *widget, cairo_t *cr, gpointer data) {
	gint i;
	guint series_index, point_index;
	struct TabData *tabData;

	tabData = (struct TabData *) data;

	(void) widget;

	/* Paint canvas background first; image is drawn on top with zoom/origin */
	cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
	cairo_paint(cr);

	cairo_save(cr);
	cairo_translate(cr, tabData->viewOrigin[0], tabData->viewOrigin[1]);
	cairo_scale(cr, tabData->viewZoom, tabData->viewZoom);
	cairo_set_source_surface(cr, tabData->image, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);

	for (i = 0; i < 4; i++) {
		if (tabData->bpressed[i]) {
			drawMarker(cr, (gint) (tabData->axiscoords[i][0] * tabData->imageScale
					* tabData->viewZoom + tabData->viewOrigin[0]), (gint) (tabData->axiscoords[i][1]
					* tabData->imageScale * tabData->viewZoom + tabData->viewOrigin[1]), i / 2);
		}
	}

	if (tabData->document == NULL)
		return TRUE;
	for (series_index = 0; series_index < tabData->document->series->len;
			series_index++) {
		DataSeries *series;
		series = g_ptr_array_index(tabData->document->series, series_index);
		if (!series->visible)
			continue;
		for (point_index = 0; point_index < series->points->len; point_index++) {
			SamplePoint *point;
			point = g_ptr_array_index(series->points, point_index);
			drawSeriesMarker(cr,
					point->source_x_px * tabData->imageScale * tabData->viewZoom
							+ tabData->viewOrigin[0],
					point->source_y_px * tabData->imageScale * tabData->viewZoom
							+ tabData->viewOrigin[1],
					series->marker_rgba,
					series == tabData->document->active_series,
					point == tabData->document->hovered_point,
					image_document_point_is_selected(tabData->document, point));
		}
	}

	return TRUE;
}
/****************************************************************/
/* This function sets the sensitivity of the buttons depending	*/
/* the control variables.					*/
/****************************************************************/
static void setButtonSensitivity(struct TabData *tabData) {
	updateExportMenuSensitivity(tabData);
	updateEditMenuSensitivity(tabData);
}

static void reportDatastoreError(const gchar *operation, GError *error) {
	if (error == NULL)
		return;
	g_printerr("Database error while %s: %s\n", operation, error->message);
	g_error_free(error);
}

static DataSeries *activeSeries(struct TabData *tabData) {
	if (tabData == NULL || tabData->document == NULL)
		return NULL;
	return tabData->document->active_series;
}

static void syncActivePointCount(struct TabData *tabData) {
	DataSeries *series;

	series = activeSeries(tabData);
	tabData->numpoints = series != NULL ? (gint) series->points->len : 0;
	if (tabData->nump_entry != NULL)
		setNumberOfPointsEntryValue(tabData->nump_entry, tabData->numpoints);
	setButtonSensitivity(tabData);
}

static void calibrationFromTab(const struct TabData *tabData,
		CalibrationState *calibration) {
	gint i;

	calibration_state_clear(calibration);
	for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
		calibration->axis_x[i] = tabData->axiscoords[i][0];
		calibration->axis_y[i] = tabData->axiscoords[i][1];
		calibration->axis_value[i] = tabData->realcoords[i];
		calibration->position_set[i] = tabData->bpressed[i];
		calibration->value_set[i] = tabData->valueset[i];
	}
	calibration->log_axis[0] = tabData->logxy[0];
	calibration->log_axis[1] = tabData->logxy[1];
}

static void persistCalibration(struct TabData *tabData) {
	CalibrationState calibration;
	GError *error;

	if (tabData == NULL || tabData->document == NULL || tabData->loadingStore)
		return;
	refreshSeriesWidgets(tabData);
	if (appDatastore == NULL || tabData->document->image_id <= 0)
		return;
	calibrationFromTab(tabData, &calibration);
	error = NULL;
	if (!datastore_save_calibration(appDatastore, tabData->document->image_id,
			&calibration, &error))
		reportDatastoreError("saving calibration", error);
}

static void clearProcessingInformation(struct TabData *tabData) {
	gtk_entry_set_text(GTK_ENTRY(tabData->xc_entry), "");
	gtk_entry_set_text(GTK_ENTRY(tabData->yc_entry), "");
	gtk_entry_set_text(GTK_ENTRY(tabData->xerr_entry), "");
	gtk_entry_set_text(GTK_ENTRY(tabData->yerr_entry), "");
}

/* Keep the coordinate readout in sync when calibration changes without
 * requiring the user to move the pointer again. */
static void refreshProcessingInformation(struct TabData *tabData) {
	gint i;
	gchar buf[32];
	struct PointValue calculatedValue;

	if (tabData->mousePointerCoords[0] < 0
			|| tabData->mousePointerCoords[1] < 0
			|| tabData->mousePointerCoords[0] >= tabData->sourceXSize
			|| tabData->mousePointerCoords[1] >= tabData->sourceYSize) {
		clearProcessingInformation(tabData);
		return;
	}

	for (i = 0; i < 4; i++) {
		if (!tabData->valueset[i] || !tabData->bpressed[i]) {
			clearProcessingInformation(tabData);
			return;
		}
	}

	calculatedValue = calculatePointValue(tabData->mousePointerCoords[0],
			tabData->mousePointerCoords[1], tabData);
	snprintf(buf, sizeof(buf), "%16.10g", calculatedValue.Xv);
	gtk_entry_set_text(GTK_ENTRY(tabData->xc_entry), buf);
	snprintf(buf, sizeof(buf), "%16.10g", calculatedValue.Yv);
	gtk_entry_set_text(GTK_ENTRY(tabData->yc_entry), buf);
	snprintf(buf, sizeof(buf), "%16.10g", calculatedValue.Xerr);
	gtk_entry_set_text(GTK_ENTRY(tabData->xerr_entry), buf);
	snprintf(buf, sizeof(buf), "%16.10g", calculatedValue.Yerr);
	gtk_entry_set_text(GTK_ENTRY(tabData->yerr_entry), buf);
}

/****************************************************************/
/* This function sets the numpoints entry to numpoints variable	*/
/* value.							*/
/****************************************************************/
void setNumberOfPointsEntryValue(GtkWidget *np_entry, gint np) {
	char buf[128];

	sprintf(buf, "%d", np);
	gtk_entry_set_text(GTK_ENTRY(np_entry), buf);
}

static gboolean tabHasPersistentImage(const struct TabData *tabData) {
	return appDatastore != NULL && tabData != NULL && tabData->document != NULL
			&& tabData->document->image_id > 0;
}

static void saveSeries(struct TabData *tabData, DataSeries *series,
		const gchar *operation) {
	GError *error;

	if (!tabHasPersistentImage(tabData) || series == NULL || series->id <= 0)
		return;
	error = NULL;
	if (!datastore_update_series(appDatastore, series, &error))
		reportDatastoreError(operation, error);
}

static gboolean calibrationIsComplete(const struct TabData *tabData) {
	gint i;

	for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
		if (!tabData->bpressed[i] || !tabData->valueset[i])
			return FALSE;
	return TRUE;
}

static void refreshSeriesWidgets(struct TabData *tabData) {
	DataSeries *series;
	GdkRGBA color;
	gint active_index;
	guint i, selection_count;
	gchar *selected_text;

	if (tabData == NULL || tabData->series_combo == NULL
			|| tabData->document == NULL)
		return;
	series = activeSeries(tabData);
	active_index = image_document_index_of_series(tabData->document, series);
	tabData->loadingStore = TRUE;
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(tabData->series_combo));
	for (i = 0; i < tabData->document->series->len; i++) {
		DataSeries *item;
		gchar *caption;
		item = g_ptr_array_index(tabData->document->series, i);
		caption = g_strdup_printf("%s (%u)", item->label, item->points->len);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tabData->series_combo),
				caption);
		g_free(caption);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(tabData->series_combo), active_index);
	gtk_entry_set_text(GTK_ENTRY(tabData->series_label_entry),
			series != NULL ? series->label : "");
	if (series != NULL) {
		rgba_to_components(series->marker_rgba, &color.red, &color.green,
				&color.blue, &color.alpha);
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(tabData->series_color_button),
				&color);
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(tabData->series_visible_check), series->visible);
	}
	tabData->loadingStore = FALSE;

	gtk_widget_set_sensitive(tabData->series_label_entry, series != NULL);
	gtk_widget_set_sensitive(tabData->series_color_button, series != NULL);
	gtk_widget_set_sensitive(tabData->series_visible_check, series != NULL);
	gtk_widget_set_sensitive(tabData->delete_series_button,
			tabData->document->series->len > 1);

	selected_text = NULL;
	selection_count = image_document_selection_count(tabData->document);
	if (selection_count > 1) {
		selected_text = g_strdup_printf("%u points selected", selection_count);
	} else if (selection_count == 1
			&& tabData->document->selected_point != NULL) {
		SamplePoint *point;
		DataSeries *selected_series;
		point = tabData->document->selected_point;
		selected_series = tabData->document->selected_series;
		if (calibrationIsComplete(tabData)) {
			struct PointValue value;
			value = calculatePointValue(point->source_x_px, point->source_y_px,
					tabData);
			selected_text = g_strdup_printf("%s, point %" G_GINT64_FORMAT
					": X %.8g, Y %.8g",
					selected_series != NULL ? selected_series->label : "Selected",
					point->sample_order + 1, value.Xv, value.Yv);
		} else {
			selected_text = g_strdup_printf("%s, point %" G_GINT64_FORMAT
					": pixel %.2f, %.2f",
					selected_series != NULL ? selected_series->label : "Selected",
					point->sample_order + 1, point->source_x_px,
					point->source_y_px);
		}
	} else {
		selected_text = g_strdup("No point selected");
	}
	gtk_label_set_text(GTK_LABEL(tabData->selected_point_label), selected_text);
	g_free(selected_text);
	updateEditMenuSensitivity(tabData);
}

static void activeSeriesChanged(GtkComboBox *combo, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	gint index;
	GError *error;

	tabData = (struct TabData *) data;
	if (tabData->loadingStore)
		return;
	index = gtk_combo_box_get_active(combo);
	if (index < 0 || (guint) index >= tabData->document->series->len)
		return;
	series = g_ptr_array_index(tabData->document->series, index);
	image_document_set_active_series(tabData->document, series);
	if (tabHasPersistentImage(tabData) && series->id > 0) {
		error = NULL;
		if (!datastore_set_active_series(appDatastore,
				tabData->document->image_id, series->id, &error))
			reportDatastoreError("selecting a series", error);
	}
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void seriesLabelChanged(GtkEditable *editable, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	gchar *caption;
	gint index;

	tabData = (struct TabData *) data;
	if (tabData->loadingStore)
		return;
	series = activeSeries(tabData);
	if (series == NULL)
		return;
	g_free(series->label);
	series->label = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
	saveSeries(tabData, series, "renaming a series");
	/* Keep the selector caption current without disturbing keyboard focus. */
	index = image_document_index_of_series(tabData->document, series);
	caption = g_strdup_printf("%s (%u)", series->label, series->points->len);
	tabData->loadingStore = TRUE;
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(tabData->series_combo),
			index);
	gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(tabData->series_combo),
			index, caption);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tabData->series_combo),
			index);
	tabData->loadingStore = FALSE;
	g_free(caption);
}

static void seriesColorChanged(GtkColorButton *button, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	GdkRGBA color;

	tabData = (struct TabData *) data;
	if (tabData->loadingStore)
		return;
	series = activeSeries(tabData);
	if (series == NULL)
		return;
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);
	series->marker_rgba = rgba_from_components(color.red, color.green,
			color.blue, color.alpha);
	saveSeries(tabData, series, "changing a series colour");
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void seriesVisibilityChanged(GtkToggleButton *button, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;

	tabData = (struct TabData *) data;
	if (tabData->loadingStore)
		return;
	series = activeSeries(tabData);
	if (series == NULL)
		return;
	series->visible = gtk_toggle_button_get_active(button);
	saveSeries(tabData, series, "changing series visibility");
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void editModeChanged(GtkToggleButton *button, gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	if (!gtk_toggle_button_get_active(button))
		return;
	tabData->editMode = TRUE;
	tabData->movedPoint = NULL;
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void addModeChanged(GtkToggleButton *button, gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	if (!gtk_toggle_button_get_active(button))
		return;
	tabData->editMode = FALSE;
	tabData->movedPoint = NULL;
	if (tabData->document != NULL) {
		tabData->document->hovered_point = NULL;
		tabData->document->hovered_series = NULL;
	}
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void addSeries(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	gchar *label;
	GError *error;

	(void) widget;
	tabData = (struct TabData *) data;
	label = image_document_next_series_label(tabData->document);
	series = image_document_add_series(tabData->document, label,
			image_document_next_color(tabData->document));
	g_free(label);
	image_document_set_active_series(tabData->document, series);
	if (tabHasPersistentImage(tabData)) {
		error = NULL;
		if (!datastore_insert_series(appDatastore, tabData->document->image_id,
				series, &error)
				|| !datastore_set_active_series(appDatastore,
						tabData->document->image_id, series->id, &error))
			reportDatastoreError("creating a series", error);
	}
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	gtk_widget_grab_focus(tabData->series_label_entry);
	gtk_editable_select_region(GTK_EDITABLE(tabData->series_label_entry), 0, -1);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void deleteSeries(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	GtkWidget *dialog;
	GError *error;

	(void) widget;
	tabData = (struct TabData *) data;
	series = activeSeries(tabData);
	if (series == NULL || tabData->document->series->len <= 1)
		return;
	if (series->points->len > 0) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(window),
				GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
				"Delete series ‘%s’ and its %u points?", series->label,
				series->points->len);
		gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
				"_Delete", GTK_RESPONSE_ACCEPT, NULL);
		if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
			gtk_widget_destroy(dialog);
			return;
		}
		gtk_widget_destroy(dialog);
	}
	if (tabHasPersistentImage(tabData) && series->id > 0) {
		error = NULL;
		if (!datastore_delete_series(appDatastore, series->id, &error)) {
			reportDatastoreError("deleting a series", error);
			return;
		}
	}
	image_document_remove_series(tabData->document, series);
	series = activeSeries(tabData);
	if (tabHasPersistentImage(tabData) && series != NULL && series->id > 0) {
		error = NULL;
		if (!datastore_set_active_series(appDatastore,
				tabData->document->image_id, series->id, &error))
			reportDatastoreError("selecting the remaining series", error);
	}
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void deleteSelectedPoint(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	GPtrArray *selected_points;
	guint i;

	(void) widget;
	tabData = (struct TabData *) data;
	if (tabData == NULL || tabData->document == NULL)
		return;
	series = tabData->document->selected_series;
	if (series == NULL || image_document_selection_count(tabData->document) == 0)
		return;
	selected_points = g_ptr_array_new();
	for (i = 0; i < tabData->document->selected_points->len; i++)
		g_ptr_array_add(selected_points,
				g_ptr_array_index(tabData->document->selected_points, i));
	image_document_clear_selection(tabData->document);
	for (i = 0; i < selected_points->len; i++) {
		SamplePoint *point;
		GError *error;

		point = g_ptr_array_index(selected_points, i);
		if (tabHasPersistentImage(tabData) && point->id > 0) {
			error = NULL;
			if (!datastore_delete_point(appDatastore, point->id, &error)) {
				reportDatastoreError("deleting a selected point", error);
				continue;
			}
		}
		data_series_remove_point(series, point);
	}
	g_ptr_array_free(selected_points, TRUE);
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void triggerUpdateDrawArea(GtkWidget *area) {
	gtk_widget_queue_draw(area);
}

void triggerLimitedUpdateDrawArea(GtkWidget *area, gint x, gint y) {
	gtk_widget_queue_draw_area(area, x - (MARKERSIZE + MARKERTHICKNESS),
			y - (MARKERSIZE + MARKERTHICKNESS),
			2 * (MARKERSIZE + MARKERTHICKNESS),
			2 * (MARKERSIZE + MARKERTHICKNESS));
}

static SamplePoint *findPointAt(struct TabData *tabData, gdouble imageX,
		gdouble imageY, DataSeries **matchedSeries) {
	gint pass;
	guint series_index, point_index;
	gdouble best_distance, threshold;
	SamplePoint *best_point;
	DataSeries *best_series;

	best_point = NULL;
	best_series = NULL;
	best_distance = G_MAXDOUBLE;
	threshold = 7.0;
	if (tabData->document == NULL)
		return NULL;
	for (pass = 0; pass < 2; pass++) {
		for (series_index = 0; series_index < tabData->document->series->len;
				series_index++) {
			DataSeries *series;
			series = g_ptr_array_index(tabData->document->series, series_index);
			if (!series->visible
					|| (pass == 0 && series != tabData->document->active_series)
					|| (pass == 1 && series == tabData->document->active_series))
				continue;
			for (point_index = 0; point_index < series->points->len; point_index++) {
				SamplePoint *point;
				gdouble dx, dy, distance;
				point = g_ptr_array_index(series->points, point_index);
				dx = (point->source_x_px - imageX) * tabData->imageScale
						* tabData->viewZoom;
				dy = (point->source_y_px - imageY) * tabData->imageScale
						* tabData->viewZoom;
				distance = sqrt(dx * dx + dy * dy);
				if (distance <= threshold && distance < best_distance) {
					best_distance = distance;
					best_point = point;
					best_series = series;
				}
			}
		}
		if (best_point != NULL)
			break;
	}
	if (matchedSeries != NULL)
		*matchedSeries = best_series;
	return best_point;
}

static void selectPoint(struct TabData *tabData, DataSeries *series,
		SamplePoint *point, gboolean extend_selection) {
	if (tabData->document == NULL)
		return;
	if (series != NULL && series != tabData->document->active_series) {
		GError *error;
		image_document_set_active_series(tabData->document, series);
		if (tabHasPersistentImage(tabData) && series->id > 0) {
			error = NULL;
			if (!datastore_set_active_series(appDatastore,
					tabData->document->image_id, series->id, &error))
				reportDatastoreError("selecting a point's series", error);
		}
		syncActivePointCount(tabData);
		refreshSeriesWidgets(tabData);
	}
	if (point == NULL && extend_selection)
		return;
	image_document_select_point(tabData->document, series, point,
			extend_selection);
	refreshSeriesWidgets(tabData);
	refreshProcessingInformation(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

/****************************************************************/
/* When a button is pressed inside the drawing area this 	*/
/* function is called, it handles axispoints and graphpoints	*/
/* and paints a square in that position.			*/
/****************************************************************/
gint mouseButtonPressEvent(GtkWidget *widget, GdkEventButton *event,
		gpointer data) {
	gint i, j;
	gdouble imageX, imageY;
	gboolean settingAxis, shiftSelecting;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;
	gtk_widget_grab_focus(tabData->drawing_area);

	getImageCoords(tabData, event->x, event->y, &imageX, &imageY);
	settingAxis = tabData->setxypressed[0] || tabData->setxypressed[1]
			|| tabData->setxypressed[2] || tabData->setxypressed[3];
	shiftSelecting = (event->state & GDK_SHIFT_MASK) != 0 && !settingAxis;

	if (event->button == 1) { /* If button 1 (leftmost) is pressed */
		if (shiftSelecting && tabData->edit_mode_button != NULL)
			gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(tabData->edit_mode_button), TRUE);
		if ((tabData->editMode || MovePointMode || shiftSelecting) && !settingAxis) {
			DataSeries *series;
			SamplePoint *point;
			series = NULL;
			point = findPointAt(tabData, imageX, imageY, &series);
			selectPoint(tabData, series, point, shiftSelecting);
			if (point != NULL && !shiftSelecting
					&& image_document_point_is_selected(tabData->document, point)) {
				tabData->movedPoint = point;
				tabData->movedSeries = series;
				tabData->movedOrigCoords[0] = point->source_x_px;
				tabData->movedOrigCoords[1] = point->source_y_px;
				tabData->movedOrigMousePtrCoords[0] = imageX;
				tabData->movedOrigMousePtrCoords[1] = imageY;
			}
		} else {
			/* If none of the set axispoint buttons been pressed */
			if (!settingAxis) {
				DataSeries *series;
				SamplePoint *point;
				GError *error;
				if (imageX < 0 || imageY < 0 || imageX >= tabData->sourceXSize
						|| imageY >= tabData->sourceYSize)
					return TRUE;
				series = activeSeries(tabData);
				if (series == NULL)
					return TRUE;
				point = data_series_add_point(series, imageX, imageY);
				error = NULL;
				if (tabHasPersistentImage(tabData) && series->id > 0 && point != NULL
						&& !datastore_insert_point(appDatastore, series->id, point,
								&error))
					reportDatastoreError("adding a point", error);
				syncActivePointCount(tabData);
				refreshSeriesWidgets(tabData);
			} else {
				for (i = 0; i < 4; i++)
					if (tabData->setxypressed[i]) { /* If the "Set point 1 on x axis" button is pressed */
						tabData->axiscoords[i][0] = imageX; /* Save coordinates */
						tabData->axiscoords[i][1] = imageY;
						for (j = 0; j < 4; j++)
							if (i != j)
								gtk_widget_set_sensitive(
										tabData->setxybutton[j], TRUE);
						gtk_widget_set_sensitive(tabData->xyentry[i], TRUE); /* Sensitize the entry */
						gtk_editable_set_editable(
								(GtkEditable *) tabData->xyentry[i], TRUE);
						gtk_widget_grab_focus(tabData->xyentry[i]); /* Focus on entry */
						tabData->setxypressed[i] = FALSE; /* Mark the button as not pressed */
						tabData->bpressed[i] = TRUE; /* Mark that axis point's been set */
						gtk_toggle_button_set_active(
								GTK_TOGGLE_BUTTON(tabData->setxybutton[i]),
								FALSE); /* Pop up the button */
						persistCalibration(tabData);
					}
			}
			setButtonSensitivity(tabData);
		}
	} else if (event->button == 2) { /* Is the middle button pressed ? */
		GtkAdjustment *hadj, *vadj;
		hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
		vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
		tabData->middlePanning = TRUE;
		tabData->middlePanMoved = FALSE;
		tabData->middlePanStartMouse[0] = event->x_root;
		tabData->middlePanStartMouse[1] = event->y_root;
		tabData->middlePanStartAdj[0] = gtk_adjustment_get_value(hadj);
		tabData->middlePanStartAdj[1] = gtk_adjustment_get_value(vadj);
	} else if (event->button == 3) { /* Is the right button pressed ? */
		for (i = 2; i < 4; i++)
			if (!tabData->bpressed[i]) {
				tabData->axiscoords[i][0] = imageX;
				tabData->axiscoords[i][1] = imageY;
				for (j = 0; j < 4; j++)
					if (i != j)
						gtk_widget_set_sensitive(tabData->setxybutton[j], TRUE);
				gtk_widget_set_sensitive(tabData->xyentry[i], TRUE);
				gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i],
						TRUE);
				gtk_widget_grab_focus(tabData->xyentry[i]);
				tabData->setxypressed[i] = FALSE;
				tabData->bpressed[i] = TRUE;
				gtk_toggle_button_set_active(
						GTK_TOGGLE_BUTTON(tabData->setxybutton[i]), FALSE);
				persistCalibration(tabData);
				break;
			}
	}

	triggerUpdateDrawArea(tabData->drawing_area);
	if (imageX >= 0 && imageY >= 0 && imageX < tabData->sourceXSize
			&& imageY < tabData->sourceYSize) {
		tabData->mousePointerCoords[0] = imageX;
		tabData->mousePointerCoords[1] = imageY;
	}
	refreshProcessingInformation(tabData);

	setButtonSensitivity(tabData);
	return TRUE;
}

/****************************************************************/
/* This function is called when a button is released on the	*/
/* drawing area, currently this function does not perform any	*/
/* task.							*/
/****************************************************************/
gint mouseButtonReleaseEvent(GtkWidget *widget, GdkEventButton *event,
		gpointer data) {
	gdouble imageX, imageY;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;

	getImageCoords(tabData, event->x, event->y, &imageX, &imageY);

	if (event->button == 1) {
		if (tabData->movedPoint != NULL) {
			GError *error;
			tabData->movedPoint->source_x_px = CLAMP(
					tabData->movedOrigCoords[0]
							+ (imageX - tabData->movedOrigMousePtrCoords[0]),
					0.0, tabData->sourceXSize - 1.0);
			tabData->movedPoint->source_y_px = CLAMP(
					tabData->movedOrigCoords[1]
							+ (imageY - tabData->movedOrigMousePtrCoords[1]),
					0.0, tabData->sourceYSize - 1.0);
			error = NULL;
			if (tabHasPersistentImage(tabData) && tabData->movedPoint->id > 0
					&& !datastore_update_point(appDatastore, tabData->movedPoint,
							&error))
				reportDatastoreError("moving a point", error);
			tabData->movedPoint = NULL;
			tabData->movedSeries = NULL;
			triggerUpdateDrawArea(tabData->drawing_area);
			refreshSeriesWidgets(tabData);
		}
	} else if (event->button == 2) {
		if (tabData->middlePanning) {
			if (!tabData->middlePanMoved)
				applyMiddleButtonAxisShortcut(tabData, imageX, imageY);
			tabData->middlePanning = FALSE;
			tabData->middlePanMoved = FALSE;
		}
	} else if (event->button == 3) {
	}
	return TRUE;
}

/****************************************************************/
/* This function is called when movement is detected in the	*/
/* drawing area, it captures the coordinates and zoom in om the */
/* position and plots it on the zoom area.			*/
/****************************************************************/
gint mouseMotionEvent(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
	gdouble imageX, imageY;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;

		if (tabData->middlePanning) {
			GtkAdjustment *hadj, *vadj;
			gdouble dx, dy, newH, newV, oldH, oldV;

		hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
		vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));

			dx = event->x_root - tabData->middlePanStartMouse[0];
			dy = event->y_root - tabData->middlePanStartMouse[1];
			if (fabs(dx) > 1.0 || fabs(dy) > 1.0) {
				tabData->middlePanMoved = TRUE;
				disableZoomToFit(tabData);
			}

			oldH = gtk_adjustment_get_value(hadj);
			oldV = gtk_adjustment_get_value(vadj);
			(void) oldH;
			(void) oldV;
			newH = tabData->middlePanStartAdj[0] - dx;
			newV = tabData->middlePanStartAdj[1] - dy;

			if (newH < gtk_adjustment_get_lower(hadj))
				newH = gtk_adjustment_get_lower(hadj);
			if (newH > getAdjustmentUpperBound(hadj))
				newH = getAdjustmentUpperBound(hadj);

			if (newV < gtk_adjustment_get_lower(vadj))
				newV = gtk_adjustment_get_lower(vadj);
			if (newV > getAdjustmentUpperBound(vadj))
				newV = getAdjustmentUpperBound(vadj);

			gtk_adjustment_set_value(hadj, newH);
			gtk_adjustment_set_value(vadj, newV);
			G3DBG(
					"middlePan: dx=%.2f dy=%.2f oldAdj=(%.2f,%.2f) newAdj=(%.2f,%.2f) upper=(%.2f,%.2f)\n",
					dx, dy, oldH, oldV, newH, newV, getAdjustmentUpperBound(hadj),
					getAdjustmentUpperBound(vadj));
			return TRUE;
		}

	getImageCoords(tabData, event->x, event->y, &imageX, &imageY);
	/* on drawing area. */

	if (imageX >= 0 && imageY >= 0 && imageX < tabData->sourceXSize
			&& imageY < tabData->sourceYSize) {
		if (tabData->movedPoint != NULL) {
			tabData->movedPoint->source_x_px = CLAMP(
					tabData->movedOrigCoords[0]
							+ (imageX - tabData->movedOrigMousePtrCoords[0]),
					0.0, tabData->sourceXSize - 1.0);
			tabData->movedPoint->source_y_px = CLAMP(
					tabData->movedOrigCoords[1]
							+ (imageY - tabData->movedOrigMousePtrCoords[1]),
					0.0, tabData->sourceYSize - 1.0);
			tabData->mousePointerCoords[0] = tabData->movedPoint->source_x_px;
			tabData->mousePointerCoords[1] = tabData->movedPoint->source_y_px;

			triggerUpdateDrawArea(tabData->drawing_area);
		} else {
			tabData->mousePointerCoords[0] = imageX;
			tabData->mousePointerCoords[1] = imageY;
			if (tabData->editMode && tabData->document != NULL) {
				DataSeries *hovered_series;
				SamplePoint *hovered_point;
				hovered_series = NULL;
				hovered_point = findPointAt(tabData, imageX, imageY,
						&hovered_series);
				if (hovered_point != tabData->document->hovered_point) {
					tabData->document->hovered_point = hovered_point;
					tabData->document->hovered_series = hovered_series;
					triggerUpdateDrawArea(tabData->drawing_area);
				}
			}
		}

		triggerUpdateDrawArea(tabData->zoom_area);

		refreshProcessingInformation(tabData);
	} else {
		clearProcessingInformation(tabData);
	}
	return TRUE;
}

gint mouseScrollEvent(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	struct TabData *tabData;
	GtkAdjustment *hadj, *vadj;
	gdouble panStep;
	gdouble deltaX, deltaY;
	gdouble newZoom;
	gdouble imageX, imageY;
	gdouble focusX, focusY;
	gboolean ctrlDown, shiftDown;

	tabData = (struct TabData *) data;
	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	ctrlDown = (event->state & GDK_CONTROL_MASK) != 0;
	shiftDown = (event->state & GDK_SHIFT_MASK) != 0;
	G3DBG(
			"mouseScrollEvent: dir=%d state=0x%x delta=(%.4f,%.4f) ctrl=%d shift=%d x=%.2f y=%.2f\n",
			(int) event->direction, (unsigned int) event->state, event->delta_x,
			event->delta_y, ctrlDown, shiftDown, event->x, event->y);

	if (ctrlDown) {
		disableZoomToFit(tabData);
		newZoom = tabData->viewZoom;
		if (event->direction == GDK_SCROLL_UP) {
			newZoom *= MAIN_IMAGE_ZOOM_STEP;
		} else if (event->direction == GDK_SCROLL_DOWN) {
			newZoom /= MAIN_IMAGE_ZOOM_STEP;
		} else if (event->direction == GDK_SCROLL_SMOOTH) {
			if (event->delta_y < 0.0) {
				newZoom *= MAIN_IMAGE_ZOOM_STEP;
			} else if (event->delta_y > 0.0) {
				newZoom /= MAIN_IMAGE_ZOOM_STEP;
			} else {
				return TRUE;
			}
		} else {
			return TRUE;
		}

		if (tabData->XSize <= 0 || tabData->YSize <= 0) {
			focusX = -1.0;
			focusY = -1.0;
		} else {
			getImageCoords(tabData, event->x, event->y, &imageX, &imageY);
			imageX = CLAMP(imageX, 0.0, tabData->sourceXSize - 1.0);
			imageY = CLAMP(imageY, 0.0, tabData->sourceYSize - 1.0);
			focusX = imageX * tabData->imageScale * tabData->viewZoom
					+ tabData->viewOrigin[0]
					- gtk_adjustment_get_value(hadj);
			focusY = imageY * tabData->imageScale * tabData->viewZoom
					+ tabData->viewOrigin[1]
					- gtk_adjustment_get_value(vadj);
		}

		setMainImageZoom(tabData, newZoom, focusX, focusY);
		return TRUE;
	}

	panStep = MAX(30.0, gtk_adjustment_get_page_size(vadj) * 0.08);
	deltaX = 0.0;
	deltaY = 0.0;

	if (event->direction == GDK_SCROLL_UP) {
		deltaY = -panStep;
	} else if (event->direction == GDK_SCROLL_DOWN) {
		deltaY = panStep;
	} else if (event->direction == GDK_SCROLL_LEFT) {
		deltaX = -panStep;
	} else if (event->direction == GDK_SCROLL_RIGHT) {
		deltaX = panStep;
	} else if (event->direction == GDK_SCROLL_SMOOTH) {
		deltaX = event->delta_x * panStep;
		deltaY = event->delta_y * panStep;
	}

	if (shiftDown && deltaX == 0.0)
		deltaX = deltaY;

	if (!shiftDown && event->direction != GDK_SCROLL_LEFT
			&& event->direction != GDK_SCROLL_RIGHT)
		deltaX = 0.0;
	if (deltaX != 0.0 || (deltaY != 0.0 && !shiftDown))
		disableZoomToFit(tabData);

	if (deltaX != 0.0) {
		gdouble newH = gtk_adjustment_get_value(hadj) + deltaX;
		gdouble hLower = gtk_adjustment_get_lower(hadj);
		gdouble hUpper = getAdjustmentUpperBound(hadj);
		if (newH < hLower)
			newH = hLower;
		if (newH > hUpper)
			newH = hUpper;
		gtk_adjustment_set_value(hadj, newH);
	}

	if (deltaY != 0.0 && !shiftDown) {
		gdouble newV = gtk_adjustment_get_value(vadj) + deltaY;
		gdouble vLower = gtk_adjustment_get_lower(vadj);
		gdouble vUpper = getAdjustmentUpperBound(vadj);
		if (newV < vLower)
			newV = vLower;
		if (newV > vUpper)
			newV = vUpper;
		gtk_adjustment_set_value(vadj, newV);
	}

	return TRUE;
}

static gboolean scrollbarChangeValue(GtkRange *range, GtkScrollType scroll,
		gdouble value, gpointer data) {
	(void) range;
	(void) scroll;
	(void) value;
	disableZoomToFit((struct TabData *) data);
	return FALSE;
}

/****************************************************************/
/* This function is called when the "Set point 1/2 on x/y axis"	*/
/* button is pressed. It inactivates the other "Set" buttons	*/
/* and makes sure the button stays down even when pressed on.	*/
/****************************************************************/
void setAxisMarkerSetMode(GtkToggleButton *widget, gpointer data) {
	gint index, i;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	index = buttonData->index;
	tabData = buttonData->tabData;

	if (gtk_toggle_button_get_active(widget)) { /* Is the button pressed on ? */
		tabData->setxypressed[index] = TRUE; /* The button is pressed down */
		for (i = 0; i < 4; i++) {
			if (index != i)
				gtk_widget_set_sensitive(tabData->setxybutton[i], FALSE);
		}
		if (tabData->bpressed[index]) { /* If the x axis point is already set */
			//			remthis = -(index + 1); /* remove the square */
			//			remove_last(GTK_WIDGET(widget), NULL);
		}
		tabData->bpressed[index] = FALSE; /* Set x axis point 1 to unset */
		gtk_widget_queue_draw(tabData->drawing_area);
		refreshProcessingInformation(tabData);
		persistCalibration(tabData);
	} else { /* If button is trying to get unpressed */
		if (tabData->setxypressed[index])
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE); /* Set button down */
	}
}

static void applyExportPreferencesToTabs(void) {
	gint i;

	if (mainnotebook == NULL)
		return;
	for (i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(mainnotebook)); i++) {
		GtkWidget *page;
		struct TabData *tabData;

		page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(mainnotebook), i);
		tabData = g_object_get_data(G_OBJECT(page), DATA_STORE_NAME);
		if (tabData == NULL)
			continue;
		tabData->ordering = exportOrdering;
		tabData->UseErrors = exportUseErrors;
	}
}

static void exportOrderingChanged(GtkCheckMenuItem *widget, gpointer data) {
	gint ordering;

	if (!gtk_check_menu_item_get_active(widget))
		return;
	ordering = GPOINTER_TO_INT(data);
	if (ordering < 0 || ordering >= ORDERBNUM)
		return;
	exportOrdering = ordering;
	applyExportPreferencesToTabs();
	saveExportPreferences();
}

static void exportErrorsChanged(GtkCheckMenuItem *widget, gpointer data) {
	(void) data;
	exportUseErrors = gtk_check_menu_item_get_active(widget);
	applyExportPreferencesToTabs();
	saveExportPreferences();
}

/****************************************************************/
/* When the value of the entry of any axis point is changed, 	*/
/* this function gets called.					*/
/****************************************************************/
void readXYEntryValues(GtkWidget *entry, gpointer data) {
	const gchar *xy_text;
	gchar *end;
	gdouble value;
	gboolean valid;
	gint index;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	index = buttonData->index;
	tabData = buttonData->tabData;

	xy_text = gtk_entry_get_text(GTK_ENTRY(entry));
	value = g_ascii_strtod(xy_text, &end);
	valid = end != xy_text;
	while (g_ascii_isspace(*end))
		end++;
	valid = valid && *end == '\0' && isfinite(value);
	if (valid)
		tabData->realcoords[index] = value;
	tabData->valueset[index] = valid
			&& (!tabData->logxy[index / 2] || value > 0);

	setButtonSensitivity(tabData);
	refreshProcessingInformation(tabData);
	persistCalibration(tabData);
}

/****************************************************************/
/* If the "X/Y axis is logarithmic" check button is toggled	*/
/* this function gets called. It sets the logx variable to its	*/
/* correct value corresponding to the buttons state.		*/
/****************************************************************/
void checkValuesOnLogarithmicAxis(GtkToggleButton *widget, gpointer data) {
	gint index;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	index = buttonData->index;
	tabData = buttonData->tabData;

	tabData->logxy[index] = (gtk_toggle_button_get_active(widget)); /* If checkbutton is pressed down */
	/* logxy = TRUE else FALSE. */
	if (tabData->logxy[index]) {
		if (tabData->realcoords[index * 2] <= 0) { /* If a negative value has been insert */
			tabData->valueset[index * 2] = FALSE;
			gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[index*2]), ""); /* Zero it */
		}
		if (tabData->realcoords[index * 2 + 1] <= 0) { /* If a negative value has been insert */
			tabData->valueset[index * 2 + 1] = FALSE;
			gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[index*2+1]), ""); /* Zero it */
		}
	}
	setButtonSensitivity(tabData);
	refreshProcessingInformation(tabData);
	persistCalibration(tabData);
}

/****************************************************************/
/* This function removes the last inserted point or the point	*/
/* indexed by remthis (<0).					*/
/****************************************************************/
void removeLastPoint(GtkWidget *widget, gpointer data) {
	DataSeries *series;
	SamplePoint *point;
	GError *error;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;
	series = activeSeries(tabData);
	if (series == NULL || series->points->len == 0)
		return;
	point = g_ptr_array_index(series->points, series->points->len - 1);
	error = NULL;
	if (tabHasPersistentImage(tabData) && point->id > 0
			&& !datastore_delete_point(appDatastore, point->id, &error))
		reportDatastoreError("removing the last point", error);
	if (image_document_point_is_selected(tabData->document, point))
		image_document_clear_selection(tabData->document);
	g_ptr_array_remove_index(series->points, series->points->len - 1);
	syncActivePointCount(tabData);

	triggerUpdateDrawArea(tabData->drawing_area);
	refreshSeriesWidgets(tabData);
	refreshProcessingInformation(tabData);
}

/****************************************************************/
/* This function sets the proper variables and then calls 	*/
/* remove_last, to remove all points except the axis points.	*/
/****************************************************************/
void removeAllPoints(GtkWidget *widget, gpointer data) {
	DataSeries *series;
	GError *error;
	GtkWidget *dialog;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;
	series = activeSeries(tabData);
	if (series == NULL || series->points->len == 0)
		return;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
			"Clear all %u points from series ‘%s’?", series->points->len,
			series->label);
	gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
			"_Clear", GTK_RESPONSE_ACCEPT, NULL);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	gtk_widget_destroy(dialog);
	error = NULL;
	if (tabHasPersistentImage(tabData) && series->id > 0
			&& !datastore_delete_series_points(appDatastore, series->id, &error))
		reportDatastoreError("clearing a series", error);
	g_ptr_array_set_size(series->points, 0);
	image_document_clear_selection(tabData->document);
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

/****************************************************************/
/* This function handles all of the keypresses done within the	*/
/* main window and handles the  appropriate measures.		*/
/****************************************************************/
gint keyPressEvent(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	GtkAdjustment *adjustment;
	gdouble adj_val;
	GdkCursor *cursor;
	GdkDisplay *display;
	struct TabData *tabData;

	if (gtk_notebook_get_n_pages((GtkNotebook *) mainnotebook) > 0) {
		tabData =
				(struct TabData *) g_object_get_data(
						G_OBJECT(gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook,
										gtk_notebook_get_current_page((GtkNotebook *) mainnotebook))),
						DATA_STORE_NAME);
		if (tabData == NULL)
			return 0;

		if (event->keyval == GDK_KEY_Left) {
			disableZoomToFit(tabData);
			adjustment = gtk_scrollable_get_hadjustment(
					(GtkScrollable *) tabData->ViewPort);
			adj_val = gtk_adjustment_get_value(adjustment);
			adj_val -= gtk_adjustment_get_page_size(adjustment) / 10.0;
			if (adj_val < gtk_adjustment_get_lower(adjustment))
				adj_val = gtk_adjustment_get_lower(adjustment);
			gtk_adjustment_set_value(adjustment, adj_val);
			gtk_scrollable_set_hadjustment((GtkScrollable *) tabData->ViewPort,
					adjustment);
		} else if (event->keyval == GDK_KEY_Right) {
			disableZoomToFit(tabData);
			adjustment = gtk_scrollable_get_hadjustment(
					(GtkScrollable *) tabData->ViewPort);
			adj_val = gtk_adjustment_get_value(adjustment);
			adj_val += gtk_adjustment_get_page_size(adjustment) / 10.0;
			if (adj_val
					> (gtk_adjustment_get_upper(adjustment)
							- gtk_adjustment_get_page_size(adjustment)))
				adj_val = (gtk_adjustment_get_upper(adjustment)
						- gtk_adjustment_get_page_size(adjustment));
			gtk_adjustment_set_value(adjustment, adj_val);
			gtk_scrollable_set_hadjustment((GtkScrollable *) tabData->ViewPort,
					adjustment);
		} else if (event->keyval == GDK_KEY_Up) {
			disableZoomToFit(tabData);
			adjustment = gtk_scrollable_get_vadjustment(
					(GtkScrollable *) tabData->ViewPort);
			adj_val = gtk_adjustment_get_value(adjustment);
			adj_val -= gtk_adjustment_get_page_size(adjustment) / 10.0;
			if (adj_val < gtk_adjustment_get_lower(adjustment))
				adj_val = gtk_adjustment_get_lower(adjustment);
			gtk_adjustment_set_value(adjustment, adj_val);
			gtk_scrollable_set_vadjustment((GtkScrollable *) tabData->ViewPort,
					adjustment);
		} else if (event->keyval == GDK_KEY_Down) {
			disableZoomToFit(tabData);
			adjustment = gtk_scrollable_get_vadjustment(
					(GtkScrollable *) tabData->ViewPort);
			adj_val = gtk_adjustment_get_value(adjustment);
			adj_val += gtk_adjustment_get_page_size(adjustment) / 10.0;
			if (adj_val
					> (gtk_adjustment_get_upper(adjustment)
							- gtk_adjustment_get_page_size(adjustment)))
				adj_val = (gtk_adjustment_get_upper(adjustment)
						- gtk_adjustment_get_page_size(adjustment));
			gtk_adjustment_set_value(adjustment, adj_val);
			gtk_scrollable_set_vadjustment((GtkScrollable *) tabData->ViewPort,
					adjustment);
		} else if (event->keyval == GDK_KEY_Control_L) {
			display = gtk_widget_get_display(tabData->drawing_area);
			cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
			gdk_window_set_cursor(
					gtk_widget_get_parent_window(tabData->drawing_area),
					cursor);
			g_object_unref(cursor);
			MovePointMode = TRUE;
		} else if (event->keyval == GDK_KEY_plus
				|| event->keyval == GDK_KEY_KP_Add) {
			disableZoomToFit(tabData);
			setMainImageZoom(tabData, tabData->viewZoom * MAIN_IMAGE_ZOOM_STEP,
					-1, -1);
		} else if (event->keyval == GDK_KEY_minus
				|| event->keyval == GDK_KEY_KP_Subtract) {
			disableZoomToFit(tabData);
			setMainImageZoom(tabData, tabData->viewZoom / MAIN_IMAGE_ZOOM_STEP,
					-1, -1);
		} else if ((event->keyval == GDK_KEY_Delete
				|| event->keyval == GDK_KEY_BackSpace)
				&& gtk_window_get_focus(GTK_WINDOW(window)) == tabData->drawing_area) {
			deleteSelectedPoint(NULL, tabData);
		}
	}
	return 0;
}

/****************************************************************/
/****************************************************************/
gint keyReleaseEvent(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	GdkCursor *cursor;
	GdkDisplay *display;
	struct TabData *tabData;

	if (gtk_notebook_get_n_pages((GtkNotebook *) mainnotebook) > 0) {
		tabData =
				(struct TabData *) g_object_get_data(
						G_OBJECT(gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook,
										gtk_notebook_get_current_page((GtkNotebook *) mainnotebook))),
						DATA_STORE_NAME);
		if (tabData == NULL)
			return 0;

		if (event->keyval == GDK_KEY_Control_L) {
			display = gtk_widget_get_display(tabData->drawing_area);
			cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
			gdk_window_set_cursor(
					gtk_widget_get_parent_window(tabData->drawing_area),
					cursor);
			g_object_unref(cursor);
			MovePointMode = FALSE;
		}
	}
	return 0;
}

/****************************************************************/
/* This function loads the image, and inserts it into the tab	*/
/* and sets up all of the different signals associated with it.	*/
/****************************************************************/
gint addImageToTab(GtkWidget *drawing_area_alignment, char *filename,
		gdouble Scale, gdouble maxX, gdouble maxY, struct TabData *tabData) {

	gdouble mScale;
	GdkCursor *cursor;
	GdkDisplay *display;
	GtkWidget *dialog;

	tabData->image = cairo_image_surface_create_from_png(filename);
	if (cairo_surface_status(tabData->image) != CAIRO_STATUS_SUCCESS) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(window), /* Notify user of the error */
		GTK_DIALOG_DESTROY_WITH_PARENT, /* with a dialog */
		GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Error loading file '%s'",
				filename);
		gtk_dialog_run(GTK_DIALOG (dialog));
		gtk_widget_destroy(dialog);

		return -1; /* exit */
	}

	tabData->XSize = cairo_image_surface_get_width(tabData->image);
	tabData->YSize = cairo_image_surface_get_height(tabData->image);
	tabData->sourceXSize = tabData->XSize;
	tabData->sourceYSize = tabData->YSize;
	tabData->imageScale = 1.0;
	G3DBG("addImageToTab: loaded '%s' image=%dx%d scale_arg=%.6f\n", filename,
			tabData->XSize, tabData->YSize, Scale);

	mScale = -1;
	if (maxX != -1 && maxY != -1) {
		if (tabData->XSize > maxX) {
			mScale = (double) maxX / tabData->XSize;
		}
		if (tabData->YSize > maxY
				&& (mScale < 0 || (double) maxY / tabData->YSize < mScale))
			mScale = (double) maxY / tabData->YSize;
	}

	if (Scale == -1 && mScale != -1)
		Scale = mScale;

	if (Scale != -1) {
		cairo_surface_t *source_image;
		cairo_surface_t *scaled_image;
		cairo_t *scaled_cr;

		tabData->imageScale = Scale;
		tabData->XSize *= Scale;
		tabData->YSize *= Scale;

		source_image = tabData->image;
		cairo_surface_flush(source_image);
		scaled_image = cairo_surface_create_similar(source_image,
				CAIRO_CONTENT_COLOR, tabData->XSize, tabData->YSize);
		scaled_cr = cairo_create(scaled_image);
		cairo_scale(scaled_cr, Scale, Scale);
		cairo_set_source_surface(scaled_cr, source_image, 0, 0);
		cairo_paint(scaled_cr);
		cairo_destroy(scaled_cr);
		cairo_surface_destroy(source_image);
		tabData->image = scaled_image;
	}

	tabData->drawing_area = gtk_drawing_area_new(); /* Create new drawing area */
	gtk_widget_set_can_focus(tabData->drawing_area, TRUE);
	tabData->viewOrigin[0] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewOrigin[1] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[0] = tabData->XSize + 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[1] = tabData->YSize + 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	gtk_widget_set_size_request(tabData->drawing_area,
			(gint) tabData->viewCanvasSize[0], (gint) tabData->viewCanvasSize[1]);

	g_signal_connect(G_OBJECT (tabData->drawing_area), "draw",
			G_CALLBACK (updateImageArea), tabData);

	g_signal_connect(G_OBJECT (tabData->drawing_area), "button_press_event", /* Connect drawing area to */
	G_CALLBACK (mouseButtonPressEvent), tabData);
	/* button_press_event. */

	g_signal_connect(G_OBJECT (tabData->drawing_area), "button_release_event", /* Connect drawing area to */
	G_CALLBACK (mouseButtonReleaseEvent), tabData);
	/* button_release_event */

	g_signal_connect(G_OBJECT (tabData->drawing_area), "motion_notify_event", /* Connect drawing area to */
	G_CALLBACK (mouseMotionEvent), tabData);
	/* motion_notify_event. */

	g_signal_connect(G_OBJECT (tabData->drawing_area), "scroll_event",
			G_CALLBACK (mouseScrollEvent), tabData);

	gtk_widget_set_events(
			tabData->drawing_area,
			GDK_EXPOSURE_MASK | /* Set the events active */
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
					| GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
					| GDK_SCROLL_MASK);

	gtk_container_add((GtkContainer *) drawing_area_alignment,
			tabData->drawing_area);

	gtk_widget_show(tabData->drawing_area);
	debugDumpViewportState("addImageToTab:after_drawing_area_show", tabData);

	display = gtk_widget_get_display(tabData->drawing_area);
	cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
	gdk_window_set_cursor(gtk_widget_get_parent_window(tabData->drawing_area),
			cursor);
	g_object_unref(cursor);

	tabData->zoomedToFit = TRUE;
	tabData->fittedViewportWidth = -1;
	tabData->fittedViewportHeight = -1;
	g_idle_add(applyInitialZoomToFit, tabData);
	debugDumpViewportState("addImageToTab:scheduled_initial_fit", tabData);

	return 0;
}

static void applyCalibrationToTab(struct TabData *tabData,
		const CalibrationState *calibration) {
	gint i;
	gchar buffer[64];

	tabData->loadingStore = TRUE;
	for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
		tabData->axiscoords[i][0] = calibration->axis_x[i];
		tabData->axiscoords[i][1] = calibration->axis_y[i];
		tabData->realcoords[i] = calibration->axis_value[i];
		tabData->bpressed[i] = calibration->position_set[i];
		tabData->valueset[i] = calibration->value_set[i];
		gtk_widget_set_sensitive(tabData->xyentry[i],
				calibration->position_set[i]);
		gtk_editable_set_editable(GTK_EDITABLE(tabData->xyentry[i]),
				calibration->position_set[i]);
		if (calibration->value_set[i]) {
			g_ascii_dtostr(buffer, sizeof(buffer), calibration->axis_value[i]);
			gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[i]), buffer);
		} else {
			gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[i]), "");
		}
	}
	for (i = 0; i < 2; i++) {
		tabData->logxy[i] = calibration->log_axis[i];
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tabData->logcheckbutton[i]),
				calibration->log_axis[i]);
	}
	tabData->loadingStore = FALSE;
	setButtonSensitivity(tabData);
	refreshProcessingInformation(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static void restoreStoredDocument(struct TabData *tabData,
		const gchar *filename) {
	CalibrationState calibration;
	DataSeries *series;
	gchar *label;
	gboolean existing;
	gint64 image_id;
	GError *error;

	error = NULL;
	image_id = 0;
	existing = FALSE;
	if (appDatastore != NULL
			&& datastore_resolve_image(appDatastore, filename,
					tabData->sourceXSize, tabData->sourceYSize, &image_id,
					&existing, &error)) {
		tabData->document->image_id = image_id;
		if (!datastore_load_document(appDatastore, image_id, tabData->document,
				&calibration, &error)) {
			reportDatastoreError("restoring image data", error);
			calibration_state_clear(&calibration);
		}
	} else {
		reportDatastoreError("identifying the image", error);
		calibration_state_clear(&calibration);
	}

	if (tabData->document->series->len == 0) {
		label = image_document_next_series_label(tabData->document);
		series = image_document_add_series(tabData->document, label,
				image_document_next_color(tabData->document));
		g_free(label);
		if (appDatastore != NULL && image_id > 0) {
			error = NULL;
			if (!datastore_insert_series(appDatastore, image_id, series, &error)
					|| !datastore_set_active_series(appDatastore, image_id,
							series->id, &error))
				reportDatastoreError("creating the first series", error);
		}
	}
	applyCalibrationToTab(tabData, &calibration);
	refreshSeriesWidgets(tabData);
	syncActivePointCount(tabData);
	(void) existing;
}

/****************************************************************/
/* This callback is called when the file - exit menuoptioned is */
/* selected.							*/
/****************************************************************/
GCallback menuFileExit(void) {
	closeApplicationHandler(NULL, NULL, NULL);

	return NULL;
}

/****************************************************************/
/* This callback sets up the thumbnail in the Fileopen dialog.	*/
/****************************************************************/
static void updateFileChooserPreview(GtkFileChooser *file_chooser,
		gpointer data) {
	GtkWidget *preview;
	char *filename;
	GdkPixbuf *pixbuf;
	gboolean have_preview;

	preview = GTK_WIDGET (data);
	filename = gtk_file_chooser_get_preview_filename(file_chooser);

	pixbuf = gdk_pixbuf_new_from_file_at_size(filename, 128, 128, NULL);
	have_preview = (pixbuf != NULL);
	g_free(filename);

	gtk_image_set_from_pixbuf(GTK_IMAGE (preview), pixbuf);
	if (pixbuf)
		g_object_unref(pixbuf);

	gtk_file_chooser_set_preview_widget_active(file_chooser, have_preview);
}

struct TabData * allocateTabMemory() {
	return g_new0(struct TabData, 1);
}

static void freeTabData(gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	if (tabData == NULL)
		return;
	if (tabData->image != NULL)
		cairo_surface_destroy(tabData->image);
	image_document_free(tabData->document);
	g_free(tabData);
}

/****************************************************************/
/* This function sets up a new tab, sets up all of the widgets 	*/
/* needed.							*/
/****************************************************************/
gint setupNewTab(char *filename, gdouble Scale, gdouble maxX, gdouble maxY,
		gboolean UsePreSetCoords, gdouble *TempCoords, gboolean *Uselogxy,
		gboolean *UseError) {
	GtkWidget *table; /* GTK table/box variables for packing */
	GtkWidget *tophbox, *bottomhbox;
	GtkWidget *trvbox, *tlvbox, *brvbox, *blvbox, *subvbox;
	GtkWidget *xy_label[4]; /* Labels for texts in window */
	GtkWidget *logcheckb[2]; /* Logarithmic checkbuttons */
	GtkWidget *nump_label, *ScrollWindow, *controls_scroll; /* Various widgets */
	GtkWidget *APlabel, *PIlabel, *ZAlabel, *Llabel, *Slabel, *tab_label;
	GtkWidget *alignment, *fixed;
	GtkWidget *x_label, *y_label, *tmplabel;
	GSList *group;
	GtkWidget *dialog;
	GtkWidget *pm_label, *pm_label2;
	GtkWidget *drawing_area_alignment;
	GtkWidget *series_buttons, *add_series_button, *add_mode_button;
	gint controls_min_width, controls_natural_width;

	gchar buf[256], buf2[256];
	gint i, TabNum;
	gboolean FileInCwd;
	static gint NumberOfTabs = 0;

	struct TabData *tabData;

	hideStartPage();

	if ((tabData = allocateTabMemory()) == NULL) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(window), /* Notify user of the error */
		GTK_DIALOG_DESTROY_WITH_PARENT, /* with a dialog */
		GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
				"Cannot open more tabs, memory allocation failed");
		gtk_dialog_run(GTK_DIALOG (dialog));
		gtk_widget_destroy(dialog);
		return -1;
	}
	NumberOfTabs++;

	strncpy(buf2, filename, 256);
	if (strcmp(dirname(buf2), getcwd(buf, 256)) == 0) {
		tab_label = gtk_label_new(basename(filename));
		FileInCwd = TRUE;
	} else {
		tab_label = gtk_label_new(filename);
		FileInCwd = FALSE;
	}

	table = g3TableNew(1, 2, FALSE); /* Create table */
	gtk_container_set_border_width(GTK_CONTAINER (table), WINDOW_BORDER);
	g3TableSetRowSpacings(table, SECT_SEP); /* Set spacings */
	g3TableSetColSpacings(table, 0);
	TabNum = gtk_notebook_append_page((GtkNotebook *) mainnotebook, table,
			tab_label);
	if (TabNum == -1) {
		g_free(tabData);
		return -1;
	}

	g_object_set_data_full(G_OBJECT(table), DATA_STORE_NAME, (gpointer) tabData,
			freeTabData);

	if (TempCoords != NULL) {
		tabData->realcoords[0] = TempCoords[0];
		tabData->realcoords[2] = TempCoords[1];
		tabData->realcoords[1] = TempCoords[2];
		tabData->realcoords[3] = TempCoords[3];
	}
	if (Uselogxy != NULL) {
		tabData->logxy[0] = Uselogxy[0];
		tabData->logxy[1] = Uselogxy[1];
	}
	if (UseError != NULL)
		exportUseErrors = *UseError;
	tabData->UseErrors = exportUseErrors;

	/* Init datastructures */

	tabData->bpressed[0] = FALSE;
	tabData->bpressed[1] = FALSE;
	tabData->bpressed[2] = FALSE;
	tabData->bpressed[3] = FALSE;

	tabData->valueset[0] = FALSE;
	tabData->valueset[1] = FALSE;
	tabData->valueset[2] = FALSE;
	tabData->valueset[3] = FALSE;

	tabData->numpoints = 0;
	tabData->ordering = exportOrdering;

	tabData->mousePointerCoords[0] = -1.0;
	tabData->mousePointerCoords[1] = -1.0;
	tabData->viewZoom = 1.0;
	tabData->viewOrigin[0] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewOrigin[1] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[0] = 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[1] = 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->XSize = 0;
	tabData->YSize = 0;
	tabData->sourceXSize = 0;
	tabData->sourceYSize = 0;
	tabData->imageScale = 1.0;

	tabData->logxy[0] = FALSE;
	tabData->logxy[1] = FALSE;

	tabData->setxypressed[0] = FALSE;
	tabData->setxypressed[1] = FALSE;
	tabData->setxypressed[2] = FALSE;
	tabData->setxypressed[3] = FALSE;

	tabData->document = image_document_new(0);
	tabData->loadingStore = FALSE;
	tabData->editMode = FALSE;
	tabData->movedPoint = NULL;
	tabData->movedSeries = NULL;
	tabData->series_combo = NULL;
	tabData->series_label_entry = NULL;
	tabData->series_color_button = NULL;
	tabData->series_visible_check = NULL;
	tabData->delete_series_button = NULL;
	tabData->edit_mode_button = NULL;
	tabData->selected_point_label = NULL;

	tabData->middlePanning = FALSE;
	tabData->middlePanMoved = FALSE;
	tabData->zoomedToFit = FALSE;
	tabData->fittedViewportWidth = -1;
	tabData->fittedViewportHeight = -1;
	tabData->pendingRecenterOnAdjust = FALSE;
	tabData->pendingZoomScrollOnAdjust = FALSE;
	tabData->pendingZoomScrollTarget[0] = 0.0;
	tabData->pendingZoomScrollTarget[1] = 0.0;
	tabData->pendingZoomScrollCanvasSize[0] = 0.0;
	tabData->pendingZoomScrollCanvasSize[1] = 0.0;

	for (i = 0; i < 4; i++) {
		tabData->xyentry[i] = gtk_entry_new(); /* Create text entry */
		gtk_entry_set_max_length(GTK_ENTRY (tabData->xyentry[i]), 20);
		gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i], FALSE);
		gtk_widget_set_sensitive(tabData->xyentry[i], FALSE); /* Inactivate it */
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (tabData->xyentry[i]), "changed", /* Init the entry to call */
		G_CALLBACK (readXYEntryValues), buttonData);
		/* read_x1_entry whenever */
		gtk_widget_set_tooltip_text(tabData->xyentry[i], entryxytt[i]);
	}

	x_label = gtk_label_new(x_string);
	y_label = gtk_label_new(y_string);
	tabData->xc_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->xc_entry), 16);
	gtk_editable_set_editable((GtkEditable *) tabData->xc_entry, FALSE);
	tabData->yc_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->yc_entry), 16);
	gtk_editable_set_editable((GtkEditable *) tabData->yc_entry, FALSE);

	pm_label = gtk_label_new(pm_string);
	pm_label2 = gtk_label_new(pm_string);
	tabData->xerr_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->xerr_entry), 16);
	gtk_editable_set_editable((GtkEditable *) tabData->xerr_entry, FALSE);
	tabData->yerr_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->yerr_entry), 16);
	gtk_editable_set_editable((GtkEditable *) tabData->yerr_entry, FALSE);

	nump_label = gtk_label_new(nump_string);
	tabData->nump_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->nump_entry), 10);
	gtk_editable_set_editable((GtkEditable *) tabData->nump_entry, FALSE);
	setNumberOfPointsEntryValue(tabData->nump_entry, tabData->numpoints);

	tabData->zoom_area = gtk_drawing_area_new(); /* Create new drawing area */
	gtk_widget_set_size_request(tabData->zoom_area, ZOOMPIXSIZE, ZOOMPIXSIZE);
	g_signal_connect(G_OBJECT (tabData->zoom_area), "draw",
			G_CALLBACK (updateZoomArea), tabData);

	for (i = 0; i < 4; i++) {
		xy_label[i] = gtk_label_new(NULL);
		gtk_label_set_markup((GtkLabel *) xy_label[i], xy_label_text[i]);
	}

	for (i = 0; i < 4; i++) {
		tmplabel = gtk_label_new(NULL);
		gtk_label_set_markup_with_mnemonic((GtkLabel *) tmplabel,
				setxylabel[i]);
		tabData->setxybutton[i] = gtk_toggle_button_new(); /* Create button */
		gtk_container_add((GtkContainer *) tabData->setxybutton[i], tmplabel);
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (tabData->setxybutton[i]), "toggled", /* Connect button */
		G_CALLBACK (setAxisMarkerSetMode), buttonData);
		gtk_widget_set_tooltip_text(tabData->setxybutton[i], setxytts[i]);
	}

	for (i = 0; i < 2; i++) {
		logcheckb[i] = gtk_check_button_new_with_mnemonic(loglabel[i]); /* Create check button */
		tabData->logcheckbutton[i] = logcheckb[i];
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (logcheckb[i]), "toggled", /* Connect button */
		G_CALLBACK (checkValuesOnLogarithmicAxis), buttonData);
		gtk_widget_set_tooltip_text(logcheckb[i], logxytt[i]);
		gtk_toggle_button_set_active((GtkToggleButton *) logcheckb[i],
				tabData->logxy[i]);
	}

	tophbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SECT_SEP);
	alignment = g3AlignmentNew(0, 0, 0, 0);
	g3TableAttach(table, alignment, 0, 1, 0, 1, 5, 0, 0, 0);
	gtk_container_add((GtkContainer *) alignment, tophbox);

	bottomhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SECT_SEP);
	alignment = g3AlignmentNew(0, 0, 1, 1);
	g3TableAttach(table, alignment, 0, 1, 1, 2, 5, 5, 0, 0);
	gtk_container_add((GtkContainer *) alignment, bottomhbox);

	tlvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX (tophbox), tlvbox, FALSE, FALSE, ELEM_SEP);
	APlabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (APlabel), APheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, APlabel);
	gtk_box_pack_start(GTK_BOX (tlvbox), alignment, FALSE, FALSE, 0);
	table = g3TableNew(3, 4, FALSE);
	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, table, FRAME_INDENT, 0);
	g3TableSetRowSpacings(table, ELEM_SEP);
	g3TableSetColSpacings(table, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX (tlvbox), fixed, FALSE, FALSE, 0);
	for (i = 0; i < 4; i++) {
		g3TableAttach(table, tabData->setxybutton[i], 0, 1, i,
				i + 1, 5, 0, 0, 0);
		g3TableAttach(table, xy_label[i], 1, 2, i, i + 1, 0, 0, 0,
				0);
		g3TableAttach(table, tabData->xyentry[i], 2, 3, i, i + 1,
				0, 0, 0, 0);
	}

	trvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX (tophbox), trvbox, FALSE, FALSE, ELEM_SEP);

	PIlabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (PIlabel), PIheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, PIlabel);
	gtk_box_pack_start(GTK_BOX (trvbox), alignment, FALSE, FALSE, 0);

	table = g3TableNew(4, 2, FALSE);
	g3TableSetRowSpacings(table, ELEM_SEP);
	g3TableSetColSpacings(table, ELEM_SEP);
	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, table, FRAME_INDENT, 0);
	gtk_box_pack_start(GTK_BOX (trvbox), fixed, FALSE, FALSE, 0);
	g3TableAttach(table, x_label, 0, 1, 0, 1, 0, 0, 0, 0);
	g3TableAttach(table, tabData->xc_entry, 1, 2, 0, 1, 0, 0, 0,
			0);
	g3TableAttach(table, pm_label, 2, 3, 0, 1, 0, 0, 0, 0);
	g3TableAttach(table, tabData->xerr_entry, 3, 4, 0, 1, 0, 0, 0,
			0);
	g3TableAttach(table, y_label, 0, 1, 1, 2, 0, 0, 0, 0);
	g3TableAttach(table, tabData->yc_entry, 1, 2, 1, 2, 0, 0, 0,
			0);
	g3TableAttach(table, pm_label2, 2, 3, 1, 2, 0, 0, 0, 0);
	g3TableAttach(table, tabData->yerr_entry, 3, 4, 1, 2, 0, 0, 0,
			0);

	table = g3TableNew(3, 1, FALSE);
	g3TableSetRowSpacings(table, 6);
	g3TableSetColSpacings(table, 6);
	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, table, FRAME_INDENT, 0);
	gtk_box_pack_start(GTK_BOX (trvbox), fixed, FALSE, FALSE, 0);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, nump_label);
	g3TableAttach(table, alignment, 0, 1, 0, 1, 0, 0, 0, 0);
	g3TableAttach(table, tabData->nump_entry, 1, 2, 0, 1, 0, 0, 0,
			0);

	blvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, GROUP_SEP);
	controls_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(controls_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(controls_scroll, TRUE);
	gtk_widget_set_valign(controls_scroll, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(bottomhbox), controls_scroll, FALSE, TRUE,
			ELEM_SEP);
	gtk_container_add(GTK_CONTAINER(controls_scroll), blvbox);

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX(blvbox), subvbox, FALSE, FALSE, 0);
	Slabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(Slabel), "<b>Data series</b>");
	gtk_widget_set_halign(Slabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(subvbox), Slabel, FALSE, FALSE, 0);

	tabData->series_combo = gtk_combo_box_text_new();
	gtk_widget_set_tooltip_text(tabData->series_combo,
			"Choose the series that receives new points and is exported");
	g_signal_connect(tabData->series_combo, "changed",
			G_CALLBACK(activeSeriesChanged), tabData);
	gtk_box_pack_start(GTK_BOX(subvbox), tabData->series_combo, FALSE, FALSE, 0);

	series_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ELEM_SEP);
	add_series_button = gtk_button_new_with_mnemonic("_New series");
	tabData->delete_series_button = gtk_button_new_with_mnemonic("Delete s_eries");
	g_signal_connect(add_series_button, "clicked", G_CALLBACK(addSeries), tabData);
	g_signal_connect(tabData->delete_series_button, "clicked",
			G_CALLBACK(deleteSeries), tabData);
	gtk_box_pack_start(GTK_BOX(series_buttons), add_series_button, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(series_buttons), tabData->delete_series_button,
			TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(subvbox), series_buttons, FALSE, FALSE, 0);

	tabData->series_label_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(tabData->series_label_entry),
			"Series label");
	gtk_widget_set_tooltip_text(tabData->series_label_entry,
			"Label for the active series");
	g_signal_connect(tabData->series_label_entry, "changed",
			G_CALLBACK(seriesLabelChanged), tabData);
	gtk_box_pack_start(GTK_BOX(subvbox), tabData->series_label_entry, FALSE,
			FALSE, 0);

	series_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ELEM_SEP);
	tabData->series_color_button = gtk_color_button_new();
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(tabData->series_color_button),
			FALSE);
	gtk_widget_set_tooltip_text(tabData->series_color_button,
			"Marker colour for the active series");
	g_signal_connect(tabData->series_color_button, "color-set",
			G_CALLBACK(seriesColorChanged), tabData);
	tabData->series_visible_check = gtk_check_button_new_with_mnemonic("_Visible");
	g_signal_connect(tabData->series_visible_check, "toggled",
			G_CALLBACK(seriesVisibilityChanged), tabData);
	gtk_box_pack_start(GTK_BOX(series_buttons), tabData->series_color_button,
			FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(series_buttons), tabData->series_visible_check,
			FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(subvbox), series_buttons, FALSE, FALSE, 0);

	group = NULL;
	add_mode_button = gtk_radio_button_new_with_label(group, "Add points");
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(add_mode_button));
	tabData->edit_mode_button = gtk_radio_button_new_with_label(group,
			"Select / edit points");
	gtk_widget_set_tooltip_text(tabData->edit_mode_button,
			"Select or drag markers; Shift-click adds or removes points from the selection");
	g_signal_connect(add_mode_button, "toggled", G_CALLBACK(addModeChanged),
			tabData);
	g_signal_connect(tabData->edit_mode_button, "toggled",
			G_CALLBACK(editModeChanged), tabData);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(add_mode_button), TRUE);
	gtk_box_pack_start(GTK_BOX(subvbox), add_mode_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(subvbox), tabData->edit_mode_button, FALSE, FALSE,
			0);

	tabData->selected_point_label = gtk_label_new("No point selected");
	gtk_label_set_xalign(GTK_LABEL(tabData->selected_point_label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(tabData->selected_point_label), TRUE);
	gtk_box_pack_start(GTK_BOX(subvbox), tabData->selected_point_label, FALSE,
			FALSE, 0);
	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	tabData->zoomareabox = subvbox;
	gtk_box_pack_start(GTK_BOX (blvbox), subvbox, FALSE, FALSE, 0);
	ZAlabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (ZAlabel), ZAheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, ZAlabel);
	gtk_box_pack_start(GTK_BOX (subvbox), alignment, FALSE, FALSE, 0);
	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, tabData->zoom_area, FRAME_INDENT, 0);
	gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0);

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	tabData->logbox = subvbox;
	gtk_box_pack_start(GTK_BOX (blvbox), subvbox, FALSE, FALSE, 0);
	Llabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (Llabel), Lheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, Llabel);
	gtk_box_pack_start(GTK_BOX (subvbox), alignment, FALSE, FALSE, 0);
	for (i = 0; i < 2; i++) {
		fixed = gtk_fixed_new();
		gtk_fixed_put((GtkFixed *) fixed, logcheckb[i], FRAME_INDENT, 0);
		gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0); /* Pack checkbutton in vert. box */
	}

	if (FileInCwd) {
		strncpy(tabData->FileNames, basename(filename), 256);
	} else {
		strncpy(tabData->FileNames, filename, 256);
	}

	snprintf(buf, 256, Window_Title, tabData->FileNames); /* Print window title in buffer */
	gtk_window_set_title(GTK_WINDOW (window), buf); /* Set window title */


	/* A scrolled window does not normally propagate its child's width. Use the
	 * completed controls' natural width without tying it to the wider axis grid. */
	gtk_widget_get_preferred_width(blvbox, &controls_min_width,
			&controls_natural_width);
	gtk_scrolled_window_set_min_content_width(
			GTK_SCROLLED_WINDOW(controls_scroll), controls_natural_width);

	brvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, GROUP_SEP);
	gtk_box_pack_start(GTK_BOX (bottomhbox), brvbox, TRUE, TRUE, 0);

	ScrollWindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy((GtkScrolledWindow *) ScrollWindow,
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	tabData->ViewPort = gtk_viewport_new(NULL, NULL);
	g_signal_connect(G_OBJECT(tabData->ViewPort), "size-allocate",
			G_CALLBACK(viewportSizeAllocateEvent), tabData);
	g_signal_connect(
			G_OBJECT(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort))),
			"changed", G_CALLBACK(adjustmentChangedEvent), tabData);
	g_signal_connect(
			G_OBJECT(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort))),
			"changed", G_CALLBACK(adjustmentChangedEvent), tabData);

	gtk_box_pack_start(GTK_BOX (brvbox), ScrollWindow, TRUE, TRUE, 0);
	drawing_area_alignment = g3AlignmentNew(0, 0, 0, 0);
	gtk_widget_set_hexpand(drawing_area_alignment, TRUE);
	gtk_widget_set_vexpand(drawing_area_alignment, TRUE);
	gtk_widget_set_halign(drawing_area_alignment, GTK_ALIGN_FILL);
	gtk_widget_set_valign(drawing_area_alignment, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER (tabData->ViewPort),
			drawing_area_alignment);
	gtk_container_add(GTK_CONTAINER (ScrollWindow), tabData->ViewPort);
	g_signal_connect(gtk_scrolled_window_get_hscrollbar(
			GTK_SCROLLED_WINDOW(ScrollWindow)), "change-value",
			G_CALLBACK(scrollbarChangeValue), tabData);
	g_signal_connect(gtk_scrolled_window_get_vscrollbar(
			GTK_SCROLLED_WINDOW(ScrollWindow)), "change-value",
			G_CALLBACK(scrollbarChangeValue), tabData);

	gtk_widget_show_all(window);

	gtk_notebook_set_current_page((GtkNotebook *) mainnotebook, TabNum);

	if (addImageToTab(drawing_area_alignment, filename, Scale, maxX, maxY,
			tabData) == -1) {
		gtk_notebook_remove_page((GtkNotebook *) mainnotebook, TabNum);
		return -1;
	}

	restoreStoredDocument(tabData, filename);

	if (UsePreSetCoords) {
		tabData->axiscoords[0][0] = 0;
		tabData->axiscoords[0][1] = tabData->sourceYSize - 1;
		tabData->axiscoords[1][0] = tabData->sourceXSize - 1;
		tabData->axiscoords[1][1] = tabData->sourceYSize - 1;
		tabData->axiscoords[2][0] = 0;
		tabData->axiscoords[2][1] = tabData->sourceYSize - 1;
		tabData->axiscoords[3][0] = 0;
		tabData->axiscoords[3][1] = 0;
		for (i = 0; i < 4; i++) {
			gtk_widget_set_sensitive(tabData->xyentry[i], TRUE);
			gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i],
					TRUE);
			sprintf(buf, "%lf", tabData->realcoords[i]);
			gtk_entry_set_text((GtkEntry *) tabData->xyentry[i], buf);
			tabData->valueset[i] = TRUE;
			tabData->bpressed[i] = TRUE;
			tabData->setxypressed[i] = FALSE;
		}
		if (Uselogxy != NULL) {
			for (i = 0; i < 2; i++) {
				tabData->logxy[i] = Uselogxy[i];
				gtk_toggle_button_set_active(
						GTK_TOGGLE_BUTTON(tabData->logcheckbutton[i]), Uselogxy[i]);
			}
		}
		persistCalibration(tabData);
		setButtonSensitivity(tabData);
	}

	gtk_widget_set_sensitive(close_menu_item, TRUE);

	// Check if any widget have been hidden, and hide if that is the case
	if (HideZoomArea)
		if (tabData->zoomareabox != NULL
		)
			gtk_widget_hide(tabData->zoomareabox);
	if (HideLog)
		if (tabData->logbox != NULL
		)
			gtk_widget_hide(tabData->logbox);
	addRecentFile(filename);

	return 0;
}

/****************************************************************/
/****************************************************************/
void dragDropReceivedEventHandler(GtkWidget *widget,
		GdkDragContext *drag_context, gint x, gint y, GtkSelectionData *data,
		guint info, guint event_time) {
	gchar *c;
    char *str1, *token, *saveptr1;
	gint i;
	GtkWidget *dialog;

//	GdkAtom type_atom = gtk_selection_data_get_data_type(data);
//	GdkAtom target_atom = gtk_selection_data_get_target(data);
//	printf("%d %s %s\n", gtk_selection_data_get_format(data), gdk_atom_name(type_atom), gdk_atom_name(target_atom));

	switch (gtk_selection_data_get_format(data)) {
	case 8: {
		guchar *data_str;
		gint length;

		data_str = (guchar *) gtk_selection_data_get_data_with_length(data,
				&length);

		for (i = 1, str1 = (char *) data_str; ; i++, str1 = NULL) {
            token = strtok_r(str1, DROPPED_URI_DELIMITER, &saveptr1);
            if (token == NULL)
                break;

//            printf("Received uri : %s\n", token);
			if ((c = strstr(token, URI_IDENTIFIER)) == NULL) {
				dialog = gtk_message_dialog_new(GTK_WINDOW(window), /* Notify user of the error */
				GTK_DIALOG_DESTROY_WITH_PARENT, /* with a dialog */
				GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						"Cannot extract local filename from uri '%s'", token);
				gtk_dialog_run(GTK_DIALOG (dialog));
				gtk_widget_destroy(dialog);
				break;
			}
			setupNewTab(&(c[strlen(URI_IDENTIFIER)]), 1.0, -1, -1, FALSE, NULL, NULL, NULL);
		}
		break;
	}
	default: {
		dialog = gtk_message_dialog_new(GTK_WINDOW(window), /* Notify user of the unknown drag-n-drop type */
		GTK_DIALOG_DESTROY_WITH_PARENT, 					/* with a dialog */
		GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Unknown dropped data format: %d",
				gtk_selection_data_get_format(data));
		gtk_dialog_run(GTK_DIALOG (dialog));
		gtk_widget_destroy(dialog);
		break;
	}
	}
	gtk_drag_finish(drag_context, FALSE, FALSE, event_time);
}

/****************************************************************/
/* This callback handles the file - open dialog.		*/
/****************************************************************/
GCallback menuFileOpen(void) {
	GtkWidget *dialog, *scalespinbutton, *hboxextra, *scalelabel;
	GtkImage *preview;
	GtkAdjustment *scaleadj;
	GtkFileFilter *filefilter;

	dialog = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW (window),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);

	// Set filtering of files to open to filetypes gdk_pixbuf can handle
	filefilter = gtk_file_filter_new();
	gtk_file_filter_add_pixbuf_formats(filefilter);
	gtk_file_chooser_set_filter((GtkFileChooser *) dialog,
			(GtkFileFilter *) filefilter);

	hboxextra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ELEM_SEP);

	scalelabel = gtk_label_new(scale_string);

	scaleadj = (GtkAdjustment *) gtk_adjustment_new(1, 0.1, 100, 0.1, 0.1, 1);
	scalespinbutton = gtk_spin_button_new(scaleadj, 0.1, 1);

	gtk_box_pack_start(GTK_BOX (hboxextra), scalelabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hboxextra), scalespinbutton, FALSE, FALSE, 0);

	gtk_file_chooser_set_extra_widget((GtkFileChooser *) dialog, hboxextra);

	gtk_widget_show(hboxextra);
	gtk_widget_show(scalelabel);
	gtk_widget_show(scalespinbutton);

	preview = (GtkImage *) gtk_image_new();
	gtk_file_chooser_set_preview_widget((GtkFileChooser *) dialog,
			(GtkWidget *) preview);
	g_signal_connect(dialog, "update-preview",
			G_CALLBACK (updateFileChooserPreview), preview);

	if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;

		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
		setupNewTab(filename,
				gtk_spin_button_get_value((GtkSpinButton *) scalespinbutton),
				-1, -1, FALSE, NULL, NULL, NULL);

		g_free(filename);
	}

	gtk_widget_destroy(dialog);

	return NULL;
}

/****************************************************************/
/* This function destroys a dialog.				*/
/****************************************************************/
void destroyDialog(GtkWidget *widget, gpointer data) {
	gtk_grab_remove(GTK_WIDGET(widget));
}

/****************************************************************/
/* This function closes a dialog.				*/
/****************************************************************/
void closeDialog(GtkWidget *widget, gpointer data) {
	gtk_widget_destroy(GTK_WIDGET(data));
}

/****************************************************************/
/* This Callback generates the help - about dialog.		*/
/****************************************************************/
GCallback menuHelpAbout(void) {
	gchar *authors[] = AUTHORS;

	gtk_show_about_dialog((GtkWindow *) window, "authors", authors, "comments",
			COMMENTS, "copyright", COPYRIGHT, "license", LICENSE, "name",
			PROGNAME, "version", VERSION, "website", HOMEPAGEURL,
			"website-label", HOMEPAGELABEL, NULL);

	return NULL;
}

/****************************************************************/
/* This function is called when a tab is closed. It removes the	*/
/* page from the notebook, all widgets within the page are	*/
/* destroyed.							*/
/****************************************************************/
GCallback menuTabClose(void) {
	gint page_num = gtk_notebook_get_current_page((GtkNotebook *) mainnotebook);
	GtkWidget *page;

	struct TabData *tabData;
	if (page_num < 0) {
		showStartPageIfNeeded();
		return NULL;
	}
	page = gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook, page_num);
	if (page == NULL) {
		showStartPageIfNeeded();
		return NULL;
	}
	tabData = (struct TabData *) g_object_get_data(
			G_OBJECT(page), DATA_STORE_NAME);
	if (tabData == NULL) {
		showStartPageIfNeeded();
		return NULL;
	}

	gtk_notebook_remove_page((GtkNotebook *) mainnotebook, page_num); /* This appearently takes care of everything */

	if (countDataTabs() == 0)
		gtk_widget_set_sensitive(close_menu_item, FALSE);
	updateExportMenuSensitivity(getCurrentTabData());
	updateEditMenuSensitivity(getCurrentTabData());
	showStartPageIfNeeded();

	return NULL;
}

/****************************************************************/
/* This callback handles the fullscreen toggling.		*/
/****************************************************************/
GCallback toggleFullscreen(GtkWidget *widget, gpointer func_data) {
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		gtk_window_fullscreen(GTK_WINDOW (window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW (window));
	}
	return NULL;
}

static struct TabData *getCurrentTabData(void) {
	gint page_num;
	GtkWidget *page;

	if (gtk_notebook_get_n_pages((GtkNotebook *) mainnotebook) <= 0)
		return NULL;

	page_num = gtk_notebook_get_current_page((GtkNotebook *) mainnotebook);
	page = gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook, page_num);
	if (page == NULL)
		return NULL;
	return (struct TabData *) g_object_get_data(G_OBJECT(page), DATA_STORE_NAME);
}

static gboolean documentHasAnyPoints(const ImageDocument *document) {
	guint i;

	if (document == NULL)
		return FALSE;
	for (i = 0; i < document->series->len; i++) {
		DataSeries *series;
		series = g_ptr_array_index(document->series, i);
		if (series->points->len > 0)
			return TRUE;
	}
	return FALSE;
}

static void updateExportMenuSensitivity(struct TabData *tabData) {
	gboolean calibrated, current_ready, all_ready;
	DataSeries *series;

	calibrated = tabData != NULL && calibrationIsComplete(tabData);
	series = tabData != NULL ? activeSeries(tabData) : NULL;
	current_ready = calibrated && series != NULL && series->points->len > 0;
	all_ready = calibrated && tabData->document != NULL
			&& documentHasAnyPoints(tabData->document);
	if (export_current_menu_item != NULL)
		gtk_widget_set_sensitive(export_current_menu_item, current_ready);
	if (export_all_menu_item != NULL)
		gtk_widget_set_sensitive(export_all_menu_item, all_ready);
}

static void updateEditMenuSensitivity(struct TabData *tabData) {
	DataSeries *series;
	gboolean has_points;
	guint selection_count;

	series = tabData != NULL ? activeSeries(tabData) : NULL;
	has_points = series != NULL && series->points->len > 0;
	selection_count = tabData != NULL && tabData->document != NULL
			? image_document_selection_count(tabData->document) : 0;
	if (remove_last_menu_item != NULL)
		gtk_widget_set_sensitive(remove_last_menu_item, has_points);
	if (clear_series_menu_item != NULL)
		gtk_widget_set_sensitive(clear_series_menu_item, has_points);
	if (delete_selected_menu_item != NULL) {
		gtk_menu_item_set_label(GTK_MENU_ITEM(delete_selected_menu_item),
				selection_count == 1 ? "_Delete selected point"
						: "_Delete selected points");
		gtk_widget_set_sensitive(delete_selected_menu_item, selection_count > 0);
	}
}

static void removeLastPointFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;

	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		removeLastPoint(widget, tabData);
}

static void clearSeriesFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;

	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		removeAllPoints(widget, tabData);
}

static void deleteSelectedFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;

	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		deleteSelectedPoint(widget, tabData);
}

static void showExportError(const gchar *message) {
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static gchar *suggestedExportName(struct TabData *tabData,
		gboolean all_series) {
	gchar *base, *name;

	base = g_path_get_basename(tabData->FileNames);
	name = g_strdup_printf("%s%s.dat", base, all_series ? "-all" : "");
	g_free(base);
	return name;
}

static void exportFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	GString *output;
	GtkWidget *dialog;
	GtkClipboard *clipboard;
	ExportTarget target;
	gboolean all_series;
	gint request;

	(void) widget;
	request = GPOINTER_TO_INT(data);
	all_series = request / EXPORT_TARGET_COUNT != 0;
	target = (ExportTarget) (request % EXPORT_TARGET_COUNT);
	tabData = getCurrentTabData();
	if (tabData == NULL)
		return;
	output = formatResultset(tabData, all_series,
			target == EXPORT_TO_CLIPBOARD);
	if (output == NULL)
		return;

	switch (target) {
	case EXPORT_TO_FILE: {
		gchar *filename, *suggested_name;
		GError *error;

		dialog = gtk_file_chooser_dialog_new(
				all_series ? "Export all series" : "Export current series",
				GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
				"_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT,
				NULL);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
				TRUE);
		suggested_name = suggestedExportName(tabData, all_series);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested_name);
		g_free(suggested_name);
		if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
			filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
			error = NULL;
			if (!g_file_set_contents(filename, output->str, output->len, &error)) {
				gchar *message;
				message = g_strdup_printf("Could not export to ‘%s’: %s", filename,
						error->message);
				showExportError(message);
				g_free(message);
				g_error_free(error);
			}
			g_free(filename);
		}
		gtk_widget_destroy(dialog);
		break;
	}
	case EXPORT_TO_CLIPBOARD:
		clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		/* Tabs and newlines paste directly as columns and rows in Calc. */
		gtk_clipboard_set_text(clipboard, output->str, output->len);
		gtk_clipboard_store(clipboard);
		break;
	case EXPORT_TO_STDOUT:
	default:
		fputs(output->str, stdout);
		fflush(stdout);
		break;
	}
	g_string_free(output, TRUE);
}

void zoomView100(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL) {
		disableZoomToFit(tabData);
		setMainImageZoom(tabData, 1.0, -1.0, -1.0);
	}
}

void zoomView200(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL) {
		disableZoomToFit(tabData);
		setMainImageZoom(tabData, 2.0, -1.0, -1.0);
	}
}

void zoomViewToFit(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL) {
		disableZoomToFit(tabData);
		tabData->zoomedToFit = TRUE;
		applyStickyZoomToFit(tabData, TRUE);
	}
}

/****************************************************************/
/* This callback handles the hide zoom area toggling.		*/
/****************************************************************/
GCallback hideZoomArea(GtkWidget *widget, gpointer func_data) {
	int i;
	struct TabData *tabData;

	for (i = 0; i < gtk_notebook_get_n_pages((GtkNotebook *) mainnotebook);
			i++) {
		tabData = (struct TabData *) g_object_get_data(
				G_OBJECT(gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook,
								i)), DATA_STORE_NAME);
		if (tabData == NULL)
			continue;
		if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
			gtk_widget_hide(tabData->zoomareabox);
		} else {
			gtk_widget_show(tabData->zoomareabox);
		}
	}
	HideZoomArea = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	return NULL;
}

/****************************************************************/
/* This callback handles the hide axis settings toggling.	*/
/****************************************************************/
GCallback hideAxisSettings(GtkWidget *widget, gpointer func_data) {
	int i;
	struct TabData *tabData;

	for (i = 0; i < gtk_notebook_get_n_pages((GtkNotebook *) mainnotebook);
			i++) {
		tabData = (struct TabData *) g_object_get_data(
				G_OBJECT(gtk_notebook_get_nth_page((GtkNotebook *) mainnotebook,
								i)), DATA_STORE_NAME);
		if (tabData == NULL)
			continue;
		if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
			gtk_widget_hide(tabData->logbox);
		} else {
			gtk_widget_show(tabData->logbox);
		}
	}
	HideLog = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	return NULL;
}

/****************************************************************/
/* This callback is called when the notebook page is changed.	*/
/* It sets up the ViewedTabNum value as well as the title of	*/
/* the window to match the image currently viewed.		*/
/****************************************************************/
GCallback notebookTabSwitchEventHandler(GtkNotebook *notebook, GtkWidget *page,
		guint page_num, gpointer user_data) {
	gchar buf[256];

	struct TabData *tabData;
	tabData = (struct TabData *) g_object_get_data(G_OBJECT(page),
			DATA_STORE_NAME);

	if (tabData != NULL) {
		sprintf(buf, Window_Title, tabData->FileNames); /* Print window title in buffer */
		gtk_window_set_title(GTK_WINDOW (window), buf); /* Set window title */
		setButtonSensitivity(tabData);
	} else {
		gtk_window_set_title(GTK_WINDOW(window), Window_Title_NoneOpen);
		updateExportMenuSensitivity(NULL);
	}
	return NULL;
}

/****************************************************************/
/* This is the main function, this function gets called when	*/
/* the program is executed. It allocates the necessary work-	*/
/* spaces and initialized the main window and its widgets.	*/
/****************************************************************/
int main(int argc, char **argv) {
	gint FileIndex[MAXNUMFILES], NumFiles = 0, i, maxX, maxY;
	gdouble Scale;
	gboolean UsePreSetCoords, UseError, Uselogxy[2];
	gdouble TempCoords[4];
	gdouble *TempCoordsPtr;

	GtkWidget *mainvbox;

	GtkWidget *menubar;
	GtkWidget *file_menu, *edit_menu, *view_menu, *help_menu;
	GtkWidget *file_root_item, *edit_root_item, *view_root_item, *help_root_item;
	GtkWidget *open_item, *quit_item, *about_item;
	GtkWidget *export_current_menu, *export_all_menu, *export_destination_item;
	GtkWidget *ordering_menu_item, *ordering_menu, *ordering_item[ORDERBNUM];
	GtkWidget *include_errors_item;
	GtkWidget *zoom_area_item, *axis_settings_item;
	GtkWidget *fullscreen_item;
	GtkWidget *zoom100_item, *zoom200_item, *zoomfit_item;
	GtkWidget *separator_item, *separator_item2;
	GtkAccelGroup *accel_group;
	const gchar *export_destination_labels[EXPORT_TARGET_COUNT] = {
		"To _stdout", "To _file…", "_Copy to clipboard"
	};
	gint scope, target;
	GSList *ordering_group;

	gtk_init(&argc, &argv); /* Init GTK */
	loadExportPreferences();

	if (argc > 1)
		if (strcmp(argv[1], "-h") == 0 || /* If no parameters given, -h or --help */
		strcmp(argv[1], "--help") == 0) {
			printf("%s", HelpText); /* Print help */
			exit(0); /* and exit */
		}

	maxX = -1;
	maxY = -1;
	Scale = -1;
	UseError = exportUseErrors;
	UsePreSetCoords = FALSE;
	Uselogxy[0] = FALSE;
	Uselogxy[1] = FALSE;
	for (i = 1; i < argc; i++) {
		if (*(argv[i]) == '-') {
			if (strcmp(argv[i], "-scale") == 0) {
				if (argc - i < 2) {
					printf("Too few parameters for -scale\n");
					exit(0);
				}
				if (sscanf(argv[i + 1], "%lf", &Scale) != 1) {
					printf("-scale parameter in invalid form !\n");
					exit(0);
				}
				i++;
				if (i >= argc)
					break;
			} else if (strcmp(argv[i], "-errors") == 0) {
				UseError = TRUE;
			} else if (strcmp(argv[i], "-lnx") == 0) {
				Uselogxy[0] = TRUE;
			} else if (strcmp(argv[i], "-lny") == 0) {
				Uselogxy[1] = TRUE;
			} else if (strcmp(argv[i], "-max") == 0) {
				if (argc - i < 3) {
					printf("Too few parameters for -max\n");
					exit(0);
				}
				if (sscanf(argv[i + 1], "%d", &maxX) != 1) {
					printf("-max first parameter in invalid form !\n");
					exit(0);
				}
				if (sscanf(argv[i + 2], "%d", &maxY) != 1) {
					printf("-max second parameter in invalid form !\n");
					exit(0);
				}
				i += 2;
				if (i >= argc)
					break;
			} else if (strcmp(argv[i], "-coords") == 0) {
				UsePreSetCoords = TRUE;
				if (argc - i < 5) {
					printf("Too few parameters for -coords\n");
					exit(0);
				}
				if (sscanf(argv[i + 1], "%lf", &TempCoords[0]) != 1) {
					printf("-max first parameter in invalid form !\n");
					exit(0);
				}
				if (sscanf(argv[i + 2], "%lf", &TempCoords[1]) != 1) {
					printf("-max second parameter in invalid form !\n");
					exit(0);
				}
				if (sscanf(argv[i + 3], "%lf", &TempCoords[2]) != 1) {
					printf("-max third parameter in invalid form !\n");
					exit(0);
				}
				if (sscanf(argv[i + 4], "%lf", &TempCoords[3]) != 1) {
					printf("-max fourth parameter in invalid form !\n");
					exit(0);
				}
				i += 4;
				if (i >= argc)
					break;
			} else {
				printf("Unknown parameter : %s\n", argv[i]);
				exit(0);
			}
			continue;
		} else {
			FileIndex[NumFiles] = i;
			NumFiles++;
		}
	}
	exportUseErrors = UseError;

	{
		GError *database_error;
		database_error = NULL;
		appDatastore = datastore_open_default(&database_error);
		if (appDatastore == NULL)
			reportDatastoreError("opening the data store; continuing without persistence",
					database_error);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL); /* Create window */
	gtk_window_set_default_size((GtkWindow *) window, 640, 480);
	gtk_window_set_title(GTK_WINDOW (window), Window_Title_NoneOpen); /* Set window title */
	gtk_window_set_resizable(GTK_WINDOW (window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER (window), 0); /* Set borders in window */
	mainvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(window), mainvbox);

	g_signal_connect(G_OBJECT (window), "delete_event", /* Init delete event of window */
	G_CALLBACK (closeApplicationHandler), NULL);
	g_signal_connect(G_OBJECT(window), "configure-event",
			G_CALLBACK(windowConfigureEvent), NULL);

	gtk_drag_dest_set(window, GTK_DEST_DEFAULT_ALL, ui_drop_target_entries,
			DROP_TARGET_NUM_DEFS, (GDK_ACTION_COPY | GDK_ACTION_MOVE));
	g_signal_connect(G_OBJECT (window), "drag-data-received", /* Drag and drop catch */
	G_CALLBACK (dragDropReceivedEventHandler), NULL);
	g_signal_connect_swapped(G_OBJECT (window), "key_press_event",
			G_CALLBACK (keyPressEvent), NULL);
	g_signal_connect(G_OBJECT (window), "key_release_event",
			G_CALLBACK (keyReleaseEvent), NULL);

	/* Create menu */
	accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW (window), accel_group);

	menubar = gtk_menu_bar_new();
	file_menu = gtk_menu_new();
	edit_menu = gtk_menu_new();
	view_menu = gtk_menu_new();
	help_menu = gtk_menu_new();
	file_menu_widget = file_menu;
	recent_menu_items = g_ptr_array_new();

	file_root_item = gtk_menu_item_new_with_mnemonic("_File");
	edit_root_item = gtk_menu_item_new_with_mnemonic("_Edit");
	view_root_item = gtk_menu_item_new_with_mnemonic("_View");
	help_root_item = gtk_menu_item_new_with_mnemonic("_Help");

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_root_item), file_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_root_item), edit_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_root_item), view_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_root_item), help_menu);

	open_item = gtk_menu_item_new_with_mnemonic("_Open");
	export_current_menu_item = gtk_menu_item_new_with_mnemonic(
			"Export _current series");
	export_all_menu_item = gtk_menu_item_new_with_mnemonic("Export _all series");
	close_menu_item = gtk_menu_item_new_with_mnemonic("_Close");
	quit_item = gtk_menu_item_new_with_mnemonic("_Quit");
	gtk_widget_set_sensitive(close_menu_item, FALSE);

	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item);

	ordering_menu_item = gtk_menu_item_new_with_mnemonic("Point _ordering");
	ordering_menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(ordering_menu_item), ordering_menu);
	ordering_group = NULL;
	for (i = 0; i < ORDERBNUM; i++) {
		ordering_item[i] = gtk_radio_menu_item_new_with_label(ordering_group,
				orderlabel[i]);
		ordering_group = gtk_radio_menu_item_get_group(
				GTK_RADIO_MENU_ITEM(ordering_item[i]));
		gtk_menu_shell_append(GTK_MENU_SHELL(ordering_menu), ordering_item[i]);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ordering_item[i]),
				i == exportOrdering);
		g_signal_connect(ordering_item[i], "toggled",
				G_CALLBACK(exportOrderingChanged), GINT_TO_POINTER(i));
	}
	include_errors_item = gtk_check_menu_item_new_with_mnemonic(
			"Include value _errors");
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(include_errors_item),
			exportUseErrors);
	g_signal_connect(include_errors_item, "toggled",
			G_CALLBACK(exportErrorsChanged), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), ordering_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), include_errors_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item);

	for (scope = 0; scope < 2; scope++) {
		GtkWidget *scope_menu;
		scope_menu = gtk_menu_new();
		for (target = 0; target < EXPORT_TARGET_COUNT; target++) {
			export_destination_item = gtk_menu_item_new_with_mnemonic(
					export_destination_labels[target]);
			gtk_menu_shell_append(GTK_MENU_SHELL(scope_menu),
					export_destination_item);
			g_signal_connect(export_destination_item, "activate",
					G_CALLBACK(exportFromMenu),
					GINT_TO_POINTER(scope * EXPORT_TARGET_COUNT + target));
		}
		if (scope == 0) {
			export_current_menu = scope_menu;
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(export_current_menu_item),
					export_current_menu);
		} else {
			export_all_menu = scope_menu;
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(export_all_menu_item),
					export_all_menu);
		}
	}
	gtk_widget_set_sensitive(export_current_menu_item, FALSE);
	gtk_widget_set_sensitive(export_all_menu_item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), export_current_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), export_all_menu_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), close_menu_item);
	separator_item2 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item2);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);

	loadRecentFiles();
	rebuildRecentFileMenu();

	remove_last_menu_item = gtk_menu_item_new_with_mnemonic(
			"_Remove last point");
	clear_series_menu_item = gtk_menu_item_new_with_mnemonic(
			"_Clear current series");
	delete_selected_menu_item = gtk_menu_item_new_with_mnemonic(
			"_Delete selected points");
	gtk_widget_set_sensitive(remove_last_menu_item, FALSE);
	gtk_widget_set_sensitive(clear_series_menu_item, FALSE);
	gtk_widget_set_sensitive(delete_selected_menu_item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), remove_last_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), clear_series_menu_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), delete_selected_menu_item);

	zoom_area_item = gtk_check_menu_item_new_with_label("Zoom area");
	axis_settings_item = gtk_check_menu_item_new_with_label("Axis settings");
	fullscreen_item = gtk_check_menu_item_new_with_mnemonic("_Full Screen");
	zoom100_item = gtk_menu_item_new_with_label("Zoom 100%");
	zoom200_item = gtk_menu_item_new_with_label("Zoom 200%");
	zoomfit_item = gtk_menu_item_new_with_label("Zoom to fit");

	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom_area_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), axis_settings_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom100_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom200_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoomfit_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), fullscreen_item);

	about_item = gtk_menu_item_new_with_mnemonic("_About");
	gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_root_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_root_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view_root_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_root_item);

	g_signal_connect(G_OBJECT(open_item), "activate", G_CALLBACK(menuFileOpen),
			NULL);
	g_signal_connect(G_OBJECT(close_menu_item), "activate",
			G_CALLBACK(menuTabClose), NULL);
	g_signal_connect(G_OBJECT(quit_item), "activate", G_CALLBACK(menuFileExit),
			NULL);
	g_signal_connect(remove_last_menu_item, "activate",
			G_CALLBACK(removeLastPointFromMenu), NULL);
	g_signal_connect(clear_series_menu_item, "activate",
			G_CALLBACK(clearSeriesFromMenu), NULL);
	g_signal_connect(delete_selected_menu_item, "activate",
			G_CALLBACK(deleteSelectedFromMenu), NULL);
	g_signal_connect(G_OBJECT(about_item), "activate", G_CALLBACK(menuHelpAbout),
			NULL);

	g_signal_connect(G_OBJECT(zoom_area_item), "toggled",
			G_CALLBACK(hideZoomArea), NULL);
	g_signal_connect(G_OBJECT(axis_settings_item), "toggled",
			G_CALLBACK(hideAxisSettings), NULL);
	g_signal_connect(G_OBJECT(fullscreen_item), "toggled",
			G_CALLBACK(toggleFullscreen), NULL);
	g_signal_connect(G_OBJECT(zoom100_item), "activate", G_CALLBACK(zoomView100),
			NULL);
	g_signal_connect(G_OBJECT(zoom200_item), "activate", G_CALLBACK(zoomView200),
			NULL);
	g_signal_connect(G_OBJECT(zoomfit_item), "activate", G_CALLBACK(zoomViewToFit),
			NULL);

	gtk_widget_add_accelerator(open_item, "activate", accel_group, GDK_KEY_O,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(close_menu_item, "activate", accel_group,
			GDK_KEY_W, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(quit_item, "activate", accel_group, GDK_KEY_Q,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(about_item, "activate", accel_group, GDK_KEY_H,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	gtk_widget_add_accelerator(zoom_area_item, "activate", accel_group,
			GDK_KEY_F5, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(axis_settings_item, "activate", accel_group,
			GDK_KEY_F6, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(fullscreen_item, "activate", accel_group,
			GDK_KEY_F11, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(zoom100_item, "activate", accel_group, GDK_KEY_1,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(zoom200_item, "activate", accel_group, GDK_KEY_2,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(zoomfit_item, "activate", accel_group, GDK_KEY_3,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	gtk_box_pack_start(GTK_BOX (mainvbox), menubar, FALSE, FALSE, 0);

	mainnotebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX (mainvbox), mainnotebook, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT (mainnotebook), "switch-page", /* Init switch-page event of notebook */
	G_CALLBACK (notebookTabSwitchEventHandler), NULL);

	if (UsePreSetCoords) {
		TempCoordsPtr = &(TempCoords[0]);
	} else {
		TempCoordsPtr = NULL;
	}

	for (i = 0; i < NumFiles; i++) {
		setupNewTab(argv[FileIndex[i]], Scale, maxX, maxY, UsePreSetCoords,
				TempCoordsPtr, &(Uselogxy[0]), &UseError);
	}
	showStartPageIfNeeded();

	gtk_widget_show_all(window); /* Show all widgets */
	showStartPageIfNeeded();

	gtk_main(); /* This is where it all starts */
	datastore_close(appDatastore);
	appDatastore = NULL;

	return (0); /* Exit. */
}
