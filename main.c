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
#include <pango/pangocairo.h>
#include <libgen.h>
#include "main.h"									/* Include predefined variables */
#include "datastore.h"
#include "history.h"
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
static const guint AXIS_READER_DELAY_MS = 300;
static const char *RECENT_GROUP = "RecentFiles";
static const char *RECENT_PATH_KEY_FMT = "path_%d";
static const char *RECENT_DATE_KEY_FMT = "date_%d";
static const char *EXPORT_PREF_GROUP = "Export";
static const char *EXPORT_ORDERING_KEY = "ordering";
static const char *EXPORT_ERRORS_KEY = "include_errors";
static const char *VIEW_PREF_GROUP = "View";
static const char *SHOW_POSITIONING_CIRCLE_KEY = "show_positioning_circle";

static const char *DROPPED_URI_DELIMITER = "\r\n";

#ifdef G3DATA2_DEBUG
#define G3DBG(...) g_printerr("[g3data2][debug] " __VA_ARGS__)
#else
#define G3DBG(...) ((void) 0)
#endif

static void setButtonSensitivity(struct TabData *tabData);
static void triggerUpdateDrawArea(GtkWidget *area);
static void refreshAxisReader(struct TabData *tabData);
static void refreshSeriesWidgets(struct TabData *tabData);
static void persistCalibration(struct TabData *tabData);
static void deleteSelectedPoint(GtkWidget *widget, gpointer data);
static void deleteSeries(GtkWidget *widget, gpointer data);
static void cancelCalibrationMode(GtkWidget *widget, gpointer data);
static gboolean calibrationIsComplete(const struct TabData *tabData);
static gboolean tabHasPersistentImage(const struct TabData *tabData);
static void updateExportMenuSensitivity(struct TabData *tabData);
static void updateEditMenuSensitivity(struct TabData *tabData);
static void refreshActionMenuSensitivity(GtkWidget *widget, gpointer data);
static void exportFromMenu(GtkWidget *widget, gpointer data);
static void exportOrderingChanged(GtkCheckMenuItem *widget, gpointer data);
static void exportErrorsChanged(GtkCheckMenuItem *widget, gpointer data);
static struct TabData *getCurrentTabData(void);
static void updateHistoryMenuSensitivity(struct TabData *tabData);
static void refreshCalibrationWorkflow(struct TabData *tabData);
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
GtkWidget *copy_current_menu_item;
GtkWidget *copy_all_menu_item;
GtkWidget *clear_series_menu_item;
GtkWidget *delete_selected_menu_item;
GtkWidget *undo_menu_item;
GtkWidget *redo_menu_item;

struct RecentFileEntry {
	gchar *path;
	gchar *date;
};

GPtrArray *recent_files;
GPtrArray *recent_menu_items;

// Declaration of global variables
gboolean HideLog = FALSE, HideZoomArea = FALSE;
static gint exportOrdering = 0;
static gboolean exportUseErrors = FALSE;
static gboolean axisReaderVisible = FALSE;
static gboolean axisReaderAltHeld = FALSE;
static gboolean axisReaderChorded = FALSE;
static gboolean showAxisReaderUncertainty = FALSE;
static guint axisReaderTimeoutId = 0;
static gboolean showPositioningCircle = FALSE;

// Declaration of extern functions
extern void drawMarker(cairo_t *cr, gint x, gint y, gint type);
extern void drawSeriesMarker(cairo_t *cr, gdouble x, gdouble y, guint32 rgba,
		gboolean active, gboolean hovered, gboolean selected);
static Datastore *appDatastore = NULL;

typedef enum {
	EXPORT_TO_STDOUT = 0,
	EXPORT_TO_FILE,
	EXPORT_TO_CLIPBOARD,
	EXPORT_TARGET_COUNT
} ExportTarget;

typedef enum {
	CLIPBOARD_TARGET_TSV = 0,
	CLIPBOARD_TARGET_TEXT
} ClipboardTarget;

typedef struct {
	gchar *text;
} ClipboardExport;

enum {
	SERIES_COL_COLOR,
	SERIES_COL_LABEL,
	SERIES_COL_COUNT,
	SERIES_COL_VISIBLE,
	SERIES_COL_POINTER,
	SERIES_N_COLUMNS
};

typedef struct {
	SamplePoint *point;
	guint index;
} PointRecord;

typedef struct {
	DataSeries *series;
	GArray *records;
	gboolean add_forward;
	gboolean attached;
} PointSetChange;

typedef struct {
	SamplePoint *point;
	gdouble old_x, old_y;
	gdouble new_x, new_y;
} PointMoveChange;

typedef struct {
	DataSeries *series;
	guint index;
	DataSeries *active_attached;
	DataSeries *active_detached;
	gboolean add_forward;
	gboolean attached;
} SeriesSetChange;

typedef struct {
	DataSeries *series;
	gchar *old_label;
	gchar *new_label;
	guint32 old_color;
	guint32 new_color;
	gboolean old_visible;
	gboolean new_visible;
} SeriesPropertyChange;

typedef struct {
	CalibrationState old_state;
	CalibrationState new_state;
} CalibrationChange;

typedef struct {
	gdouble old_diameter;
	gdouble new_diameter;
} PositioningCircleDiameterChange;

static const GtkTargetEntry CLIPBOARD_EXPORT_TARGETS[] = {
	{ "text/tab-separated-values", 0, CLIPBOARD_TARGET_TSV },
	{ "text/tab-separated-values;charset=utf-8", 0, CLIPBOARD_TARGET_TSV },
	{ "UTF8_STRING", 0, CLIPBOARD_TARGET_TEXT },
	{ "COMPOUND_TEXT", 0, CLIPBOARD_TARGET_TEXT },
	{ "TEXT", 0, CLIPBOARD_TARGET_TEXT },
	{ "STRING", 0, CLIPBOARD_TARGET_TEXT },
	{ "text/plain;charset=utf-8", 0, CLIPBOARD_TARGET_TEXT },
	{ "text/plain", 0, CLIPBOARD_TARGET_TEXT }
};

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

static gchar *getPreferencesPath(void) {
	return g_build_filename(g_get_user_config_dir(), "g3data2",
			"preferences.ini", NULL);
}

static void loadPreferences(void) {
	GKeyFile *key_file;
	GError *error;
	gchar *path;
	gint ordering;

	key_file = g_key_file_new();
	path = getPreferencesPath();
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

	error = NULL;
	showPositioningCircle = g_key_file_get_boolean(key_file, VIEW_PREF_GROUP,
			SHOW_POSITIONING_CIRCLE_KEY, &error);
	if (error != NULL) {
		showPositioningCircle = FALSE;
		g_clear_error(&error);
	}

	g_free(path);
	g_key_file_unref(key_file);
}

static void savePreferences(void) {
	GKeyFile *key_file;
	GError *error;
	gchar *path, *dirpath, *contents;
	gsize length;

	key_file = g_key_file_new();
	g_key_file_set_integer(key_file, EXPORT_PREF_GROUP, EXPORT_ORDERING_KEY,
			exportOrdering);
	g_key_file_set_boolean(key_file, EXPORT_PREF_GROUP, EXPORT_ERRORS_KEY,
			exportUseErrors);
	g_key_file_set_boolean(key_file, VIEW_PREF_GROUP,
			SHOW_POSITIONING_CIRCLE_KEY, showPositioningCircle);
	contents = g_key_file_to_data(key_file, &length, NULL);
	path = getPreferencesPath();
	dirpath = g_path_get_dirname(path);
	g_mkdir_with_parents(dirpath, 0755);
	error = NULL;
	if (!g_file_set_contents(path, contents, length, &error)) {
		g_warning("Could not save application preferences: %s", error->message);
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

	if (file_menu_widget == NULL || export_current_menu_item == NULL
			|| recent_files == NULL)
		return;

	clearRecentFileMenuItems();
	/* Recent files belong with Open, above the separator preceding exports. */
	insert_at = getMenuItemIndex(file_menu_widget, export_current_menu_item) - 1;
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

static gboolean pointIsInsideImage(const struct TabData *tabData,
		gdouble x, gdouble y) {
	return x >= 0.0 && y >= 0.0 && x < tabData->sourceXSize
			&& y < tabData->sourceYSize;
}

static void appendAxisReaderLines(cairo_t *cr, gdouble cursor_x,
		gdouble cursor_y, const gdouble x_axis[2], const gdouble y_axis[2]) {
	cairo_move_to(cr, cursor_x, cursor_y);
	cairo_line_to(cr, x_axis[0], x_axis[1]);
	cairo_move_to(cr, cursor_x, cursor_y);
	cairo_line_to(cr, y_axis[0], y_axis[1]);
}

static void visibleDrawingBounds(GtkWidget *widget, struct TabData *tabData,
		gdouble *left, gdouble *top, gdouble *right, gdouble *bottom) {
	GtkAllocation allocation;
	GtkAdjustment *hadjustment, *vadjustment;

	gtk_widget_get_allocation(widget, &allocation);
	*left = 0.0;
	*top = 0.0;
	*right = allocation.width;
	*bottom = allocation.height;
	if (tabData->ViewPort == NULL)
		return;
	hadjustment = gtk_scrollable_get_hadjustment(
			GTK_SCROLLABLE(tabData->ViewPort));
	vadjustment = gtk_scrollable_get_vadjustment(
			GTK_SCROLLABLE(tabData->ViewPort));
	if (hadjustment != NULL) {
		*left = MAX(*left, gtk_adjustment_get_value(hadjustment));
		*right = MIN(*right, *left + gtk_adjustment_get_page_size(hadjustment));
	}
	if (vadjustment != NULL) {
		*top = MAX(*top, gtk_adjustment_get_value(vadjustment));
		*bottom = MIN(*bottom, *top + gtk_adjustment_get_page_size(vadjustment));
	}
}

static gboolean calibratedAxisCrossesCard(const struct TabData *tabData,
		gint first, gint second, gdouble x, gdouble y, gdouble width,
		gdouble height) {
	gdouble scale, x1, y1, dx, dy, signed_distance;
	gdouble min_distance, max_distance;
	gdouble corners[4][2] = {
		{ x, y }, { x + width, y },
		{ x, y + height }, { x + width, y + height }
	};
	gint i;

	scale = tabData->imageScale * tabData->viewZoom;
	x1 = tabData->axiscoords[first][0] * scale + tabData->viewOrigin[0];
	y1 = tabData->axiscoords[first][1] * scale + tabData->viewOrigin[1];
	dx = (tabData->axiscoords[second][0]
			- tabData->axiscoords[first][0]) * scale;
	dy = (tabData->axiscoords[second][1]
			- tabData->axiscoords[first][1]) * scale;
	min_distance = G_MAXDOUBLE;
	max_distance = -G_MAXDOUBLE;
	for (i = 0; i < 4; i++) {
		signed_distance = dx * (corners[i][1] - y1)
				- dy * (corners[i][0] - x1);
		min_distance = MIN(min_distance, signed_distance);
		max_distance = MAX(max_distance, signed_distance);
	}
	return min_distance <= 0.0 && max_distance >= 0.0;
}

static gint axisReaderCandidatePenalty(const struct TabData *tabData,
		gdouble x, gdouble y, gdouble width, gdouble height) {
	gdouble scale, source_x, source_y, x_fraction, y_fraction;
	gdouble min_x_fraction, min_y_fraction;
	gdouble corners[4][2] = {
		{ x, y }, { x + width, y },
		{ x, y + height }, { x + width, y + height }
	};
	gint i, penalty;

	/* Crossing either calibrated axis is the strongest reason to reject a
	 * position. If all candidates avoid the axes, prefer a card wholly on the
	 * plot side of X1 and Y1 rather than obscuring the below/left margins. */
	penalty = 0;
	if (calibratedAxisCrossesCard(tabData, 0, 1, x, y, width, height))
		penalty += 100;
	if (calibratedAxisCrossesCard(tabData, 2, 3, x, y, width, height))
		penalty += 100;

	scale = tabData->imageScale * tabData->viewZoom;
	min_x_fraction = G_MAXDOUBLE;
	min_y_fraction = G_MAXDOUBLE;
	for (i = 0; i < 4; i++) {
		source_x = (corners[i][0] - tabData->viewOrigin[0]) / scale;
		source_y = (corners[i][1] - tabData->viewOrigin[1]) / scale;
		if (!calculateAxisPosition(source_x, source_y, tabData, &x_fraction,
				&y_fraction))
			return G_MAXINT / 2;
		min_x_fraction = MIN(min_x_fraction, x_fraction);
		min_y_fraction = MIN(min_y_fraction, y_fraction);
	}
	if (min_x_fraction < 0.0)
		penalty += 10;
	if (min_y_fraction < 0.0)
		penalty += 10;
	return penalty;
}

static void placeAxisReaderLabel(const struct TabData *tabData,
		gdouble cursor_x, gdouble cursor_y, gdouble width, gdouble height,
		gdouble left, gdouble top, gdouble right, gdouble bottom,
		gdouble *label_x, gdouble *label_y) {
	const gdouble gap = 10.0;
	const gdouble margin = 4.0;
	gdouble candidates[4][2] = {
		{ cursor_x + gap, cursor_y - gap - height }, /* above right */
		{ cursor_x - gap - width, cursor_y + gap }, /* below left */
		{ cursor_x - gap - width, cursor_y - gap - height }, /* above left */
		{ cursor_x + gap, cursor_y + gap } /* below right */
	};
	gint i, penalty, best_index, best_penalty;

	left += margin;
	top += margin;
	right -= margin;
	bottom -= margin;
	best_index = -1;
	best_penalty = G_MAXINT;
	for (i = 0; i < 4; i++) {
		if (candidates[i][0] >= left && candidates[i][1] >= top
				&& candidates[i][0] + width <= right
				&& candidates[i][1] + height <= bottom) {
			penalty = axisReaderCandidatePenalty(tabData, candidates[i][0],
					candidates[i][1], width, height) + i;
			if (penalty < best_penalty) {
				best_penalty = penalty;
				best_index = i;
			}
		}
	}
	if (best_index >= 0) {
		*label_x = candidates[best_index][0];
		*label_y = candidates[best_index][1];
		return;
	}
	*label_x = CLAMP(candidates[0][0], left, MAX(left, right - width));
	*label_y = CLAMP(candidates[0][1], top, MAX(top, bottom - height));
}

static gint axisReaderDecimalPlaces(gdouble uncertainty) {
	gdouble hundredth, exponent;

	hundredth = fabs(uncertainty) / 100.0;
	if (!isfinite(hundredth) || hundredth <= 0.0)
		return 10;
	/* Choose the finest decimal quantum that is not smaller than one
	 * hundredth of the calculated uncertainty. */
	exponent = ceil(log10(hundredth) - 1e-12);
	return CLAMP(-(gint) exponent, 0, 12);
}

static void drawPositioningCircle(cairo_t *cr, struct TabData *tabData) {
	gdouble scale, center_x, center_y, radius;

	if (!showPositioningCircle
			|| !pointIsInsideImage(tabData, tabData->mousePointerCoords[0],
					tabData->mousePointerCoords[1]))
		return;
	scale = tabData->imageScale * tabData->viewZoom;
	center_x = tabData->mousePointerCoords[0] * scale + tabData->viewOrigin[0];
	center_y = tabData->mousePointerCoords[1] * scale + tabData->viewOrigin[1];
	radius = tabData->positioningCircleDiameter * scale / 2.0;

	cairo_save(cr);
	cairo_rectangle(cr, tabData->viewOrigin[0], tabData->viewOrigin[1],
			tabData->XSize * tabData->viewZoom,
			tabData->YSize * tabData->viewZoom);
	cairo_clip(cr);
	/* A broad white stroke underneath the black stroke leaves a white edge on
	 * both its inner and outer sides. Stroke widths stay in screen pixels. */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.7);
	cairo_set_line_width(cr, 6.0);
	cairo_arc(cr, center_x, center_y, radius, 0.0, 2.0 * G_PI);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7);
	cairo_set_line_width(cr, 2.0);
	cairo_arc(cr, center_x, center_y, radius, 0.0, 2.0 * G_PI);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void drawAxisReader(GtkWidget *widget, cairo_t *cr,
		struct TabData *tabData) {
	struct PointValue value;
	gdouble x_axis_source[2], y_axis_source[2];
	gdouble cursor_x, cursor_y, x_axis[2], y_axis[2];
	gdouble scale, left, top, right, bottom, label_x, label_y;
	gdouble text_width, text_height, card_width, card_height;
	const gdouble padding = 5.0;
	gchar *text;
	PangoLayout *layout;
	gint layout_width, layout_height, x_decimals, y_decimals;

	if (!axisReaderVisible || !calibrationIsComplete(tabData)
			|| !pointIsInsideImage(tabData, tabData->mousePointerCoords[0],
					tabData->mousePointerCoords[1])
			|| !calculateAxisGuides(tabData->mousePointerCoords[0],
					tabData->mousePointerCoords[1], tabData, x_axis_source,
					y_axis_source))
		return;
	value = calculatePointValue(tabData->mousePointerCoords[0],
			tabData->mousePointerCoords[1], tabData);
	if (!isfinite(value.Xv) || !isfinite(value.Yv))
		return;

	scale = tabData->imageScale * tabData->viewZoom;
	cursor_x = tabData->mousePointerCoords[0] * scale + tabData->viewOrigin[0];
	cursor_y = tabData->mousePointerCoords[1] * scale + tabData->viewOrigin[1];
	x_axis[0] = x_axis_source[0] * scale + tabData->viewOrigin[0];
	x_axis[1] = x_axis_source[1] * scale + tabData->viewOrigin[1];
	y_axis[0] = y_axis_source[0] * scale + tabData->viewOrigin[0];
	y_axis[1] = y_axis_source[1] * scale + tabData->viewOrigin[1];

	cairo_save(cr);
	cairo_rectangle(cr, tabData->viewOrigin[0], tabData->viewOrigin[1],
			tabData->XSize * tabData->viewZoom,
			tabData->YSize * tabData->viewZoom);
	cairo_clip(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 5.0);
	appendAxisReaderLines(cr, cursor_x, cursor_y, x_axis, y_axis);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_set_line_width(cr, 2.0);
	appendAxisReaderLines(cr, cursor_x, cursor_y, x_axis, y_axis);
	cairo_stroke(cr);
	cairo_restore(cr);

	x_decimals = axisReaderDecimalPlaces(value.Xerr);
	y_decimals = axisReaderDecimalPlaces(value.Yerr);
	if (showAxisReaderUncertainty && isfinite(value.Xerr)
			&& isfinite(value.Yerr))
		text = g_strdup_printf("x=%.*f \302\261 %.*f\ny=%.*f \302\261 %.*f",
				x_decimals, value.Xv, x_decimals, value.Xerr,
				y_decimals, value.Yv, y_decimals, value.Yerr);
	else
		text = g_strdup_printf("x=%.*f\ny=%.*f", x_decimals, value.Xv,
				y_decimals, value.Yv);
	layout = gtk_widget_create_pango_layout(widget, text);
	pango_layout_get_pixel_size(layout, &layout_width, &layout_height);
	text_width = layout_width;
	text_height = layout_height;
	card_width = text_width + 2.0 * padding;
	card_height = text_height + 2.0 * padding;
	visibleDrawingBounds(widget, tabData, &left, &top, &right, &bottom);
	placeAxisReaderLabel(tabData, cursor_x, cursor_y, card_width, card_height,
			left, top, right, bottom, &label_x, &label_y);

	cairo_save(cr);
	cairo_set_source_rgba(cr, 1.0, 0.94, 0.12, 0.96);
	cairo_rectangle(cr, label_x, label_y, card_width, card_height);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.20, 0.18, 0.0, 0.75);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_move_to(cr, label_x + padding, label_y + padding);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);
	g_object_unref(layout);
	g_free(text);
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
	if (tabData->marqueeActive) {
		gdouble x = MIN(tabData->marqueeStart[0], tabData->marqueeEnd[0]);
		gdouble y = MIN(tabData->marqueeStart[1], tabData->marqueeEnd[1]);
		gdouble width = fabs(tabData->marqueeEnd[0] - tabData->marqueeStart[0]);
		gdouble height = fabs(tabData->marqueeEnd[1] - tabData->marqueeStart[1]);
		cairo_set_source_rgba(cr, 0.20, 0.45, 0.85, 0.16);
		cairo_rectangle(cr, x, y, width, height);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.15, 0.35, 0.75, 0.9);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}
	drawPositioningCircle(cr, tabData);
	drawAxisReader(widget, cr, tabData);

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
	calibration->positioning_circle_diameter =
			tabData->positioningCircleDiameter;
}

static void persistCalibration(struct TabData *tabData) {
	CalibrationState calibration;
	GError *error;

	if (tabData == NULL || tabData->document == NULL || tabData->loadingStore)
		return;
	refreshSeriesWidgets(tabData);
	calibrationFromTab(tabData, &calibration);
	tabData->committedCalibration = calibration;
	if (appDatastore == NULL || tabData->document->image_id <= 0)
		return;
	error = NULL;
	if (!datastore_save_calibration(appDatastore, tabData->document->image_id,
			&calibration, &error))
		reportDatastoreError("saving calibration", error);
}

static void calibrationToTab(struct TabData *tabData,
		const CalibrationState *calibration) {
	gint i;

	for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
		gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
		tabData->axiscoords[i][0] = calibration->axis_x[i];
		tabData->axiscoords[i][1] = calibration->axis_y[i];
		tabData->realcoords[i] = calibration->axis_value[i];
		tabData->bpressed[i] = calibration->position_set[i];
		tabData->valueset[i] = calibration->value_set[i];
		if (tabData->xyentry[i] != NULL) {
			if (calibration->value_set[i]) {
				g_ascii_dtostr(buffer, sizeof(buffer), calibration->axis_value[i]);
				gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[i]), buffer);
			} else {
				gtk_entry_set_text(GTK_ENTRY(tabData->xyentry[i]), "");
			}
			gtk_widget_set_sensitive(tabData->xyentry[i], TRUE);
			gtk_editable_set_editable(GTK_EDITABLE(tabData->xyentry[i]), TRUE);
		}
		if (tabData->setxybutton[i] != NULL)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					tabData->setxybutton[i]), FALSE);
		tabData->setxypressed[i] = FALSE;
	}
	for (i = 0; i < 2; i++) {
		tabData->logxy[i] = calibration->log_axis[i];
		if (tabData->logcheckbutton[i] != NULL)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					tabData->logcheckbutton[i]), calibration->log_axis[i]);
	}
	tabData->positioningCircleDiameter =
			calibration->positioning_circle_diameter;
}

static gboolean beginChangeTransaction(struct TabData *tabData,
		gboolean *started, GError **error) {
	*started = tabHasPersistentImage(tabData);
	return !*started || datastore_begin(appDatastore, error);
}

static gint pointRecordCompare(gconstpointer left, gconstpointer right) {
	const PointRecord *a = left;
	const PointRecord *b = right;
	return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

static gboolean finishChangeTransaction(gboolean started, gboolean success,
		GError **error) {
	if (!started)
		return success;
	if (success && datastore_commit(appDatastore, error))
		return TRUE;
	datastore_rollback(appDatastore);
	return FALSE;
}

static gboolean applyPointSetChange(gpointer context, gpointer data,
		gboolean forward, GError **error) {
	struct TabData *tabData;
	PointSetChange *change;
	gboolean attach, started, success;
	guint i;

	tabData = context;
	change = data;
	attach = change->add_forward ? forward : !forward;
	if (attach == change->attached)
		return TRUE;
	if (!beginChangeTransaction(tabData, &started, error))
		return FALSE;
	success = TRUE;
	if (tabHasPersistentImage(tabData)) {
		if (attach) {
			for (i = 0; success && i < change->records->len; i++) {
				PointRecord *record = &g_array_index(change->records, PointRecord, i);
				success = datastore_insert_point(appDatastore, change->series->id,
						record->point, error);
			}
		} else {
			for (i = 0; success && i < change->records->len; i++) {
				PointRecord *record = &g_array_index(change->records, PointRecord, i);
				success = datastore_delete_point(appDatastore, record->point->id,
						error);
			}
		}
	}
	if (!finishChangeTransaction(started, success, error))
		return FALSE;
	if (attach) {
		for (i = 0; i < change->records->len; i++) {
			PointRecord *record = &g_array_index(change->records, PointRecord, i);
			data_series_insert_point(change->series, record->point, record->index);
		}
	} else {
		for (i = change->records->len; i > 0; i--) {
			PointRecord *record = &g_array_index(change->records, PointRecord, i - 1);
			gint index = data_series_index_of_point(change->series, record->point);
			if (index >= 0)
				data_series_steal_point(change->series, (guint) index);
		}
		image_document_clear_selection(tabData->document);
	}
	change->attached = attach;
	return TRUE;
}

static void freePointSetChange(gpointer data) {
	PointSetChange *change;
	guint i;

	change = data;
	if (change == NULL)
		return;
	if (!change->attached) {
		for (i = 0; i < change->records->len; i++) {
			PointRecord *record = &g_array_index(change->records, PointRecord, i);
			sample_point_free(record->point);
		}
	}
	g_array_free(change->records, TRUE);
	g_free(change);
}

static gboolean applyPointMoveChange(gpointer context, gpointer data,
		gboolean forward, GError **error) {
	struct TabData *tabData = context;
	PointMoveChange *change = data;
	gdouble old_x = change->point->source_x_px;
	gdouble old_y = change->point->source_y_px;

	change->point->source_x_px = forward ? change->new_x : change->old_x;
	change->point->source_y_px = forward ? change->new_y : change->old_y;
	if (tabHasPersistentImage(tabData) && change->point->id > 0
			&& !datastore_update_point(appDatastore, change->point, error)) {
		change->point->source_x_px = old_x;
		change->point->source_y_px = old_y;
		return FALSE;
	}
	return TRUE;
}

static gboolean applySeriesSetChange(gpointer context, gpointer data,
		gboolean forward, GError **error) {
	struct TabData *tabData;
	SeriesSetChange *change;
	gboolean attach, started, success;
	guint i;

	tabData = context;
	change = data;
	attach = change->add_forward ? forward : !forward;
	if (attach == change->attached)
		return TRUE;
	if (!beginChangeTransaction(tabData, &started, error))
		return FALSE;
	success = TRUE;
	if (tabHasPersistentImage(tabData)) {
		if (attach) {
			success = datastore_insert_series(appDatastore,
					tabData->document->image_id, change->series, error);
			for (i = 0; success && i < change->series->points->len; i++)
				success = datastore_insert_point(appDatastore, change->series->id,
						g_ptr_array_index(change->series->points, i), error);
			if (success)
				success = datastore_set_active_series(appDatastore,
						tabData->document->image_id,
						change->active_attached->id, error);
		} else {
			success = datastore_delete_series(appDatastore, change->series->id,
					error);
			if (success && change->active_detached != NULL)
				success = datastore_set_active_series(appDatastore,
						tabData->document->image_id,
						change->active_detached->id, error);
		}
	}
	if (!finishChangeTransaction(started, success, error))
		return FALSE;
	if (attach) {
		image_document_insert_series(tabData->document, change->series,
				change->index);
		image_document_set_active_series(tabData->document,
				change->active_attached);
	} else {
		gint index = image_document_index_of_series(tabData->document,
				change->series);
		if (index >= 0)
			image_document_steal_series(tabData->document, (guint) index);
		if (change->active_detached != NULL)
			image_document_set_active_series(tabData->document,
					change->active_detached);
	}
	change->attached = attach;
	return TRUE;
}

static void freeSeriesSetChange(gpointer data) {
	SeriesSetChange *change;

	change = data;
	if (change == NULL)
		return;
	if (!change->attached)
		data_series_free(change->series);
	g_free(change);
}

static gboolean applySeriesPropertyChange(gpointer context, gpointer data,
		gboolean forward, GError **error) {
	struct TabData *tabData;
	SeriesPropertyChange *change;
	const gchar *label;
	gchar *previous_label;
	guint32 color, previous_color;
	gboolean visible, previous_visible, success;

	tabData = context;
	change = data;
	label = forward ? change->new_label : change->old_label;
	color = forward ? change->new_color : change->old_color;
	visible = forward ? change->new_visible : change->old_visible;
	previous_label = change->series->label;
	previous_color = change->series->marker_rgba;
	previous_visible = change->series->visible;
	change->series->label = g_strdup(label);
	change->series->marker_rgba = color;
	change->series->visible = visible;
	success = !tabHasPersistentImage(tabData)
			|| datastore_update_series(appDatastore, change->series, error);
	if (success) {
		g_free(previous_label);
	} else {
		g_free(change->series->label);
		change->series->label = previous_label;
		change->series->marker_rgba = previous_color;
		change->series->visible = previous_visible;
	}
	return success;
}

static void freeSeriesPropertyChange(gpointer data) {
	SeriesPropertyChange *change = data;
	if (change == NULL)
		return;
	g_free(change->old_label);
	g_free(change->new_label);
	g_free(change);
}

static gboolean applyCalibrationChange(gpointer context, gpointer data,
		gboolean forward, GError **error) {
	struct TabData *tabData;
	CalibrationChange *change;
	const CalibrationState *state;
	gint armed, i;

	tabData = context;
	change = data;
	state = forward ? &change->new_state : &change->old_state;
	armed = -1;
	for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
		if (tabData->setxypressed[i])
			armed = i;
	if (tabHasPersistentImage(tabData)
			&& !datastore_save_calibration(appDatastore,
					tabData->document->image_id, state, error))
		return FALSE;
	tabData->loadingStore = TRUE;
	calibrationToTab(tabData, state);
	tabData->committedCalibration = *state;
	tabData->loadingStore = FALSE;
	if (armed >= 0 && !calibrationIsComplete(tabData))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
				tabData->setxybutton[armed]), TRUE);
	return TRUE;
}

static gboolean applyPositioningCircleDiameterChange(gpointer context,
		gpointer data, gboolean forward, GError **error) {
	struct TabData *tabData = context;
	PositioningCircleDiameterChange *change = data;
	CalibrationState state = tabData->committedCalibration;
	gdouble diameter = forward ? change->new_diameter : change->old_diameter;

	state.positioning_circle_diameter = diameter;
	if (tabHasPersistentImage(tabData)
			&& !datastore_save_calibration(appDatastore,
					tabData->document->image_id, &state, error))
		return FALSE;
	tabData->positioningCircleDiameter = diameter;
	tabData->committedCalibration.positioning_circle_diameter = diameter;
	return TRUE;
}

static void refreshAfterHistoryChange(struct TabData *tabData) {
	syncActivePointCount(tabData);
	refreshSeriesWidgets(tabData);
	refreshCalibrationWorkflow(tabData);
	refreshAxisReader(tabData);
	updateHistoryMenuSensitivity(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

static gboolean executeHistoryCommand(struct TabData *tabData,
		HistoryCommand *command, const gchar *operation) {
	GError *error;

	error = NULL;
	if (!history_execute(tabData->history, command, &error)) {
		reportDatastoreError(operation, error);
		return FALSE;
	}
	refreshAfterHistoryChange(tabData);
	return TRUE;
}

static void refreshAxisReader(struct TabData *tabData) {
	if ((axisReaderVisible || showPositioningCircle) && tabData != NULL)
		triggerUpdateDrawArea(tabData->drawing_area);
}

static gboolean tabHasPersistentImage(const struct TabData *tabData) {
	return appDatastore != NULL && tabData != NULL && tabData->document != NULL
			&& tabData->document->image_id > 0;
}

static gboolean calibrationIsComplete(const struct TabData *tabData) {
	gint i;

	for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
		if (!tabData->bpressed[i] || !tabData->valueset[i])
			return FALSE;
	return TRUE;
}

static void refreshCalibrationWorkflow(struct TabData *tabData) {
	static const gchar *roles[G3_AXIS_POINT_COUNT] = { "X1", "X2", "Y1", "Y2" };
	gint i, armed, positioned;
	gchar *text;

	if (tabData == NULL || tabData->calibration_status_label == NULL)
		return;
	armed = -1;
	positioned = 0;
	for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
		if (tabData->setxypressed[i])
			armed = i;
		if (tabData->bpressed[i])
			positioned++;
		if (tabData->setxybutton[i] != NULL)
			gtk_button_set_label(GTK_BUTTON(tabData->setxybutton[i]),
					tabData->bpressed[i] ? "Update" : "Pick");
	}
	if (calibrationIsComplete(tabData)) {
		text = g_strdup("Calibration complete.\nUse Update to replace a reference point.");
	} else if (armed >= 0) {
		text = g_strdup_printf("Select %s on the image (%d of 4).\nThen enter its axis value.",
				roles[armed], MIN(positioned + 1, G3_AXIS_POINT_COUNT));
	} else if (tabData->axisWorkflowDismissed) {
		text = g_strdup("Calibration incomplete.\nSample points now and calibrate later.");
	} else {
		text = g_strdup("Set four reference points\nto calculate graph coordinates.");
	}
	gtk_label_set_text(GTK_LABEL(tabData->calibration_status_label), text);
	g_free(text);
	if (tabData->calibration_cancel_button != NULL) {
		gtk_widget_set_visible(tabData->calibration_cancel_button,
				!calibrationIsComplete(tabData) && armed >= 0);
	}
}

static void refreshSeriesWidgets(struct TabData *tabData) {
	DataSeries *series;
	GdkRGBA color;
	guint i, selection_count;
	gchar *selected_text;
	GtkTreeSelection *selection;
	GtkTreeIter active_iter;
	gboolean have_active_iter;

	if (tabData == NULL || tabData->series_store == NULL
			|| tabData->document == NULL)
		return;
	series = activeSeries(tabData);
	tabData->loadingStore = TRUE;
	gtk_list_store_clear(tabData->series_store);
	have_active_iter = FALSE;
	for (i = 0; i < tabData->document->series->len; i++) {
		DataSeries *item;
		GtkTreeIter iter;
		GdkRGBA item_color;
		item = g_ptr_array_index(tabData->document->series, i);
		rgba_to_components(item->marker_rgba, &item_color.red, &item_color.green,
				&item_color.blue, &item_color.alpha);
		gtk_list_store_append(tabData->series_store, &iter);
		gtk_list_store_set(tabData->series_store, &iter,
				SERIES_COL_COLOR, &item_color,
				SERIES_COL_LABEL, item->label,
				SERIES_COL_COUNT, item->points->len,
				SERIES_COL_VISIBLE, item->visible,
				SERIES_COL_POINTER, item, -1);
		if (item == series) {
			active_iter = iter;
			have_active_iter = TRUE;
		}
	}
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tabData->series_view));
	if (have_active_iter)
		gtk_tree_selection_select_iter(selection, &active_iter);
	if (series != NULL) {
		rgba_to_components(series->marker_rgba, &color.red, &color.green,
				&color.blue, &color.alpha);
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(tabData->series_color_button),
				&color);
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(tabData->series_visible_check), series->visible);
	}
	tabData->loadingStore = FALSE;

	gtk_widget_set_sensitive(tabData->series_color_button, series != NULL);
	gtk_widget_set_sensitive(tabData->series_visible_check, series != NULL);

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

static void activeSeriesChanged(GtkTreeSelection *selection, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GError *error;

	tabData = (struct TabData *) data;
	if (tabData->loadingStore)
		return;
	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		return;
	gtk_tree_model_get(model, &iter, SERIES_COL_POINTER, &series, -1);
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

static void seriesLabelEdited(GtkCellRendererText *renderer, gchar *path_text,
		gchar *new_text, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	SeriesPropertyChange *change;
	GtkTreeIter iter;
	GtkTreePath *path;

	(void) renderer;
	tabData = (struct TabData *) data;
	if (tabData->loadingStore || new_text == NULL || *new_text == '\0')
		return;
	path = gtk_tree_path_new_from_string(path_text);
	if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabData->series_store), &iter,
			path)) {
		gtk_tree_path_free(path);
		return;
	}
	gtk_tree_model_get(GTK_TREE_MODEL(tabData->series_store), &iter,
			SERIES_COL_POINTER, &series, -1);
	gtk_tree_path_free(path);
	if (series == NULL || g_strcmp0(series->label, new_text) == 0)
		return;
	change = g_new0(SeriesPropertyChange, 1);
	change->series = series;
	change->old_label = g_strdup(series->label);
	change->new_label = g_strdup(new_text);
	change->old_color = change->new_color = series->marker_rgba;
	change->old_visible = change->new_visible = series->visible;
	executeHistoryCommand(tabData, history_command_new("Rename Series",
			applySeriesPropertyChange, change, freeSeriesPropertyChange),
			"renaming a series");
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
	{
		SeriesPropertyChange *change = g_new0(SeriesPropertyChange, 1);
		change->series = series;
		change->old_label = g_strdup(series->label);
		change->new_label = g_strdup(series->label);
		change->old_color = series->marker_rgba;
		change->new_color = rgba_from_components(color.red, color.green,
				color.blue, color.alpha);
		change->old_visible = change->new_visible = series->visible;
		if (change->old_color == change->new_color) {
			freeSeriesPropertyChange(change);
			return;
		}
		executeHistoryCommand(tabData, history_command_new("Change Series Colour",
				applySeriesPropertyChange, change, freeSeriesPropertyChange),
				"changing a series colour");
	}
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
	{
		SeriesPropertyChange *change = g_new0(SeriesPropertyChange, 1);
		change->series = series;
		change->old_label = g_strdup(series->label);
		change->new_label = g_strdup(series->label);
		change->old_color = change->new_color = series->marker_rgba;
		change->old_visible = series->visible;
		change->new_visible = gtk_toggle_button_get_active(button);
		if (change->old_visible == change->new_visible) {
			freeSeriesPropertyChange(change);
			return;
		}
		executeHistoryCommand(tabData, history_command_new(
				"Change Series Visibility", applySeriesPropertyChange, change,
				freeSeriesPropertyChange), "changing series visibility");
	}
}

static void seriesVisibleToggled(GtkCellRendererToggle *renderer,
		gchar *path_text, gpointer data) {
	struct TabData *tabData = data;
	GtkTreePath *path;
	GtkTreeIter iter;
	DataSeries *series;
	SeriesPropertyChange *change;

	(void) renderer;
	path = gtk_tree_path_new_from_string(path_text);
	if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabData->series_store), &iter,
			path)) {
		gtk_tree_path_free(path);
		return;
	}
	gtk_tree_model_get(GTK_TREE_MODEL(tabData->series_store), &iter,
			SERIES_COL_POINTER, &series, -1);
	gtk_tree_path_free(path);
	if (series == NULL)
		return;
	change = g_new0(SeriesPropertyChange, 1);
	change->series = series;
	change->old_label = g_strdup(series->label);
	change->new_label = g_strdup(series->label);
	change->old_color = change->new_color = series->marker_rgba;
	change->old_visible = series->visible;
	change->new_visible = !series->visible;
	executeHistoryCommand(tabData, history_command_new(
			"Change Series Visibility", applySeriesPropertyChange, change,
			freeSeriesPropertyChange), "changing series visibility");
}

static void editSelectedSeriesLabel(struct TabData *tabData) {
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tabData->series_view));
	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		return;
	path = gtk_tree_model_get_path(model, &iter);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(tabData->series_view), 1);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(tabData->series_view), path, column,
			TRUE);
	gtk_tree_path_free(path);
}

static void renameSeriesFromMenu(GtkWidget *widget, gpointer data) {
	(void) widget;
	editSelectedSeriesLabel(data);
}

static gboolean seriesViewButtonPress(GtkWidget *widget,
		GdkEventButton *event, gpointer data) {
	struct TabData *tabData = data;
	GtkTreePath *path;

	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		editSelectedSeriesLabel(tabData);
		return TRUE;
	}
	if (event->button != 3)
		return FALSE;
	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
			&path, NULL, NULL, NULL)) {
		GtkWidget *menu, *rename_item, *delete_item;
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
		gtk_tree_path_free(path);
		menu = gtk_menu_new();
		rename_item = gtk_menu_item_new_with_label("Rename Series…");
		delete_item = gtk_menu_item_new_with_label("Delete Series…");
		gtk_widget_set_sensitive(delete_item,
				tabData->document->series->len > 1);
		g_signal_connect(rename_item, "activate",
				G_CALLBACK(renameSeriesFromMenu), tabData);
		g_signal_connect(delete_item, "activate", G_CALLBACK(deleteSeries),
				tabData);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename_item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete_item);
		g_signal_connect_swapped(menu, "selection-done",
				G_CALLBACK(gtk_widget_destroy), menu);
		gtk_widget_show_all(menu);
		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
		return TRUE;
	}
	return FALSE;
}

static gboolean seriesViewKeyPress(GtkWidget *widget, GdkEventKey *event,
		gpointer data) {
	(void) widget;
	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter
			|| event->keyval == GDK_KEY_F2) {
		editSelectedSeriesLabel(data);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Delete) {
		deleteSeries(NULL, data);
		return TRUE;
	}
	return FALSE;
}

static void cancelCalibrationMode(GtkWidget *widget, gpointer data) {
	struct TabData *tabData = data;
	gint i;
	(void) widget;
	tabData->axisWorkflowDismissed = TRUE;
	tabData->loadingStore = TRUE;
	for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
		tabData->setxypressed[i] = FALSE;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tabData->setxybutton[i]),
				FALSE);
	}
	tabData->loadingStore = FALSE;
	refreshCalibrationWorkflow(tabData);
}

static void addSeries(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	DataSeries *previous_active;
	SeriesSetChange *change;
	gchar *label;

	(void) widget;
	tabData = (struct TabData *) data;
	previous_active = activeSeries(tabData);
	label = image_document_next_series_label(tabData->document);
	series = data_series_new(label, image_document_next_color(tabData->document),
			tabData->document->series->len);
	g_free(label);
	change = g_new0(SeriesSetChange, 1);
	change->series = series;
	change->index = tabData->document->series->len;
	change->active_attached = series;
	change->active_detached = previous_active;
	change->add_forward = TRUE;
	change->attached = FALSE;
	if (executeHistoryCommand(tabData, history_command_new("Add Series",
			applySeriesSetChange, change, freeSeriesSetChange),
			"creating a series")) {
		GtkTreePath *path = gtk_tree_path_new_from_indices(change->index, -1);
		GtkTreeViewColumn *column = gtk_tree_view_get_column(
				GTK_TREE_VIEW(tabData->series_view), 1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(tabData->series_view), path,
				column, TRUE);
		gtk_tree_path_free(path);
	}
}

static void deleteSeries(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	DataSeries *remaining;
	SeriesSetChange *change;
	gint index;
	GtkWidget *dialog;

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
	index = image_document_index_of_series(tabData->document, series);
	remaining = (guint) index + 1 < tabData->document->series->len
			? g_ptr_array_index(tabData->document->series, index + 1)
			: g_ptr_array_index(tabData->document->series, index - 1);
	change = g_new0(SeriesSetChange, 1);
	change->series = series;
	change->index = (guint) index;
	change->active_attached = series;
	change->active_detached = remaining;
	change->add_forward = FALSE;
	change->attached = TRUE;
	executeHistoryCommand(tabData, history_command_new("Delete Series",
			applySeriesSetChange, change, freeSeriesSetChange),
			"deleting a series");
}

static void deleteSelectedPoint(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	DataSeries *series;
	PointSetChange *change;
	guint i;

	(void) widget;
	tabData = (struct TabData *) data;
	if (tabData == NULL || tabData->document == NULL)
		return;
	series = tabData->document->selected_series;
	if (series == NULL || image_document_selection_count(tabData->document) == 0)
		return;
	change = g_new0(PointSetChange, 1);
	change->series = series;
	change->records = g_array_new(FALSE, FALSE, sizeof(PointRecord));
	change->add_forward = FALSE;
	change->attached = TRUE;
	for (i = 0; i < tabData->document->selected_points->len; i++)
	{
		PointRecord record;
		record.point = g_ptr_array_index(tabData->document->selected_points, i);
		record.index = (guint) data_series_index_of_point(series, record.point);
		g_array_append_val(change->records, record);
	}
	/* Selection order is not necessarily sample order. */
	g_array_sort(change->records, (GCompareFunc) pointRecordCompare);
	executeHistoryCommand(tabData, history_command_new(
			change->records->len == 1 ? "Delete Point" : "Delete Points",
			applyPointSetChange, change, freePointSetChange),
			"deleting selected points");
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
	refreshAxisReader(tabData);
	triggerUpdateDrawArea(tabData->drawing_area);
}

/****************************************************************/
/* When a button is pressed inside the drawing area this 	*/
/* function is called, it handles axispoints and graphpoints	*/
/* and paints a square in that position.			*/
/****************************************************************/
gint mouseButtonPressEvent(GtkWidget *widget, GdkEventButton *event,
		gpointer data) {
	gint i, axis_index;
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
		if (settingAxis) {
			CalibrationChange *change;
			gchar *label;

			if (imageX < 0 || imageY < 0 || imageX >= tabData->sourceXSize
					|| imageY >= tabData->sourceYSize)
				return TRUE;
			for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
				if (tabData->setxypressed[i])
					break;
			if (i >= G3_AXIS_POINT_COUNT)
				return TRUE;
			axis_index = i;
			change = g_new0(CalibrationChange, 1);
			calibrationFromTab(tabData, &change->old_state);
			change->new_state = change->old_state;
			change->new_state.axis_x[i] = imageX;
			change->new_state.axis_y[i] = imageY;
			change->new_state.position_set[i] = TRUE;
			label = g_strdup_printf("Set %s", i == 0 ? "X1" : i == 1 ? "X2"
					: i == 2 ? "Y1" : "Y2");
			executeHistoryCommand(tabData, history_command_new(label,
					applyCalibrationChange, change, g_free), "setting an axis point");
			g_free(label);
			tabData->loadingStore = TRUE;
			for (i = 0; i < G3_AXIS_POINT_COUNT; i++) {
				tabData->setxypressed[i] = FALSE;
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
						tabData->setxybutton[i]), FALSE);
			}
			tabData->loadingStore = FALSE;
			for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
				if (!tabData->bpressed[i]) {
					gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
							tabData->setxybutton[i]), TRUE);
					break;
				}
			gtk_widget_grab_focus(tabData->xyentry[axis_index]);
			refreshCalibrationWorkflow(tabData);
		} else if (shiftSelecting) {
			tabData->leftPressPending = TRUE;
			tabData->marqueeActive = FALSE;
			tabData->leftPressWidget[0] = event->x;
			tabData->leftPressWidget[1] = event->y;
			tabData->marqueeStart[0] = tabData->marqueeEnd[0] = event->x;
			tabData->marqueeStart[1] = tabData->marqueeEnd[1] = event->y;
		} else {
			DataSeries *series;
			SamplePoint *point;
			series = NULL;
			point = findPointAt(tabData, imageX, imageY, &series);
			if (point != NULL && series == activeSeries(tabData)
					&& image_document_point_is_selected(tabData->document, point)) {
				tabData->movedPoint = point;
				tabData->movedSeries = series;
				tabData->movedOrigCoords[0] = point->source_x_px;
				tabData->movedOrigCoords[1] = point->source_y_px;
				tabData->movedOrigMousePtrCoords[0] = imageX;
				tabData->movedOrigMousePtrCoords[1] = imageY;
			} else {
				PointSetChange *change;
				PointRecord record;
				if (imageX < 0 || imageY < 0 || imageX >= tabData->sourceXSize
						|| imageY >= tabData->sourceYSize)
					return TRUE;
				series = activeSeries(tabData);
				if (series == NULL)
					return TRUE;
				record.index = series->points->len;
				record.point = sample_point_new(imageX, imageY,
						record.index == 0 ? 0 : ((SamplePoint *) g_ptr_array_index(
						series->points, record.index - 1))->sample_order + 1);
				change = g_new0(PointSetChange, 1);
				change->series = series;
				change->records = g_array_new(FALSE, FALSE, sizeof(PointRecord));
				g_array_append_val(change->records, record);
				change->add_forward = TRUE;
				change->attached = FALSE;
				executeHistoryCommand(tabData, history_command_new("Add Point",
						applyPointSetChange, change, freePointSetChange),
						"adding a point");
			}
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
	}

	triggerUpdateDrawArea(tabData->drawing_area);
	if (imageX >= 0 && imageY >= 0 && imageX < tabData->sourceXSize
			&& imageY < tabData->sourceYSize) {
		tabData->mousePointerCoords[0] = imageX;
		tabData->mousePointerCoords[1] = imageY;
	}
	refreshAxisReader(tabData);

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
			PointMoveChange *change;
			gdouble new_x, new_y;
			new_x = CLAMP(
					tabData->movedOrigCoords[0]
							+ (imageX - tabData->movedOrigMousePtrCoords[0]),
					0.0, tabData->sourceXSize - 1.0);
			new_y = CLAMP(
					tabData->movedOrigCoords[1]
							+ (imageY - tabData->movedOrigMousePtrCoords[1]),
					0.0, tabData->sourceYSize - 1.0);
			change = g_new0(PointMoveChange, 1);
			change->point = tabData->movedPoint;
			change->old_x = tabData->movedOrigCoords[0];
			change->old_y = tabData->movedOrigCoords[1];
			change->new_x = new_x;
			change->new_y = new_y;
			tabData->movedPoint->source_x_px = change->old_x;
			tabData->movedPoint->source_y_px = change->old_y;
			tabData->movedPoint = NULL;
			tabData->movedSeries = NULL;
			if (change->old_x != change->new_x || change->old_y != change->new_y)
				executeHistoryCommand(tabData, history_command_new("Move Point",
						applyPointMoveChange, change, g_free), "moving a point");
			else
				g_free(change);
		} else if (tabData->leftPressPending) {
			DataSeries *series = activeSeries(tabData);
			if (tabData->marqueeActive && series != NULL) {
				gdouble x1, y1, x2, y2;
				guint i;
				getImageCoords(tabData, tabData->marqueeStart[0],
						tabData->marqueeStart[1], &x1, &y1);
				getImageCoords(tabData, tabData->marqueeEnd[0],
						tabData->marqueeEnd[1], &x2, &y2);
				image_document_clear_selection(tabData->document);
				for (i = 0; i < series->points->len; i++) {
					SamplePoint *point = g_ptr_array_index(series->points, i);
					if (point->source_x_px >= MIN(x1, x2)
							&& point->source_x_px <= MAX(x1, x2)
							&& point->source_y_px >= MIN(y1, y2)
							&& point->source_y_px <= MAX(y1, y2))
						image_document_select_point(tabData->document, series, point,
								TRUE);
				}
			} else {
				DataSeries *matched_series = NULL;
				SamplePoint *point = findPointAt(tabData, imageX, imageY,
						&matched_series);
				if (matched_series != series)
					point = NULL;
				selectPoint(tabData, series, point, TRUE);
			}
			tabData->leftPressPending = FALSE;
			tabData->marqueeActive = FALSE;
			refreshSeriesWidgets(tabData);
			triggerUpdateDrawArea(tabData->drawing_area);
		}
	} else if (event->button == 2) {
		if (tabData->middlePanning) {
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
	if (tabData->leftPressPending) {
		gdouble dx = event->x - tabData->leftPressWidget[0];
		gdouble dy = event->y - tabData->leftPressWidget[1];
		if (fabs(dx) > 4.0 || fabs(dy) > 4.0)
			tabData->marqueeActive = TRUE;
		if (tabData->marqueeActive) {
			tabData->marqueeEnd[0] = event->x;
			tabData->marqueeEnd[1] = event->y;
			triggerUpdateDrawArea(tabData->drawing_area);
		}
	}

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
			if (tabData->document != NULL) {
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

		refreshAxisReader(tabData);
	} else {
		tabData->mousePointerCoords[0] = -1.0;
		tabData->mousePointerCoords[1] = -1.0;
		refreshAxisReader(tabData);
	}
	return TRUE;
}

static gboolean mouseLeaveEvent(GtkWidget *widget, GdkEventCrossing *event,
		gpointer data) {
	struct TabData *tabData = data;
	(void) widget;
	(void) event;
	tabData->mousePointerCoords[0] = -1.0;
	tabData->mousePointerCoords[1] = -1.0;
	refreshAxisReader(tabData);
	return FALSE;
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
	if (tabData->loadingStore)
		return;

	if (gtk_toggle_button_get_active(widget)) { /* Is the button pressed on ? */
		tabData->axisWorkflowDismissed = FALSE;
		tabData->setxypressed[index] = TRUE; /* The button is pressed down */
		for (i = 0; i < 4; i++) {
			if (index != i) {
				tabData->setxypressed[i] = FALSE;
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
						tabData->setxybutton[i]), FALSE);
			}
		}
		gtk_widget_queue_draw(tabData->drawing_area);
		refreshAxisReader(tabData);
	} else {
		tabData->setxypressed[index] = FALSE;
	}
	refreshCalibrationWorkflow(tabData);
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
	savePreferences();
}

static void exportErrorsChanged(GtkCheckMenuItem *widget, gpointer data) {
	(void) data;
	exportUseErrors = gtk_check_menu_item_get_active(widget);
	applyExportPreferencesToTabs();
	savePreferences();
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
	if (tabData->loadingStore)
		return;

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
	refreshAxisReader(tabData);
}

static void commitAxisEntry(struct TabData *tabData, gint index) {
	CalibrationChange *change;
	CalibrationState current;
	gchar *label;

	calibrationFromTab(tabData, &current);
	if (current.axis_value[index] == tabData->committedCalibration.axis_value[index]
			&& current.value_set[index]
					== tabData->committedCalibration.value_set[index])
		return;
	change = g_new0(CalibrationChange, 1);
	change->old_state = tabData->committedCalibration;
	change->new_state = current;
	label = g_strdup_printf("Change %s Value", index == 0 ? "X1"
			: index == 1 ? "X2" : index == 2 ? "Y1" : "Y2");
	executeHistoryCommand(tabData, history_command_new(label,
			applyCalibrationChange, change, g_free), "changing an axis value");
	g_free(label);
}

static void axisEntryActivated(GtkEntry *entry, gpointer data) {
	struct ButtonData *buttonData = data;
	(void) entry;
	commitAxisEntry(buttonData->tabData, buttonData->index);
}

static gboolean axisEntryFocusOut(GtkWidget *entry, GdkEventFocus *event,
		gpointer data) {
	struct ButtonData *buttonData = data;
	(void) entry;
	(void) event;
	commitAxisEntry(buttonData->tabData, buttonData->index);
	return FALSE;
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
	if (tabData->loadingStore)
		return;
	{
		CalibrationChange *change;
		gboolean enabled = gtk_toggle_button_get_active(widget);
		if (tabData->committedCalibration.log_axis[index] == enabled)
			return;
		change = g_new0(CalibrationChange, 1);
		change->old_state = tabData->committedCalibration;
		change->new_state = change->old_state;
		change->new_state.log_axis[index] = enabled;
		if (enabled) {
			if (change->new_state.axis_value[index * 2] <= 0)
				change->new_state.value_set[index * 2] = FALSE;
			if (change->new_state.axis_value[index * 2 + 1] <= 0)
				change->new_state.value_set[index * 2 + 1] = FALSE;
		}
		executeHistoryCommand(tabData, history_command_new(
				index == 0 ? "Change X Axis Scale" : "Change Y Axis Scale",
				applyCalibrationChange, change, g_free),
				"changing logarithmic axis setting");
	}
}

/****************************************************************/
/* Clear every sampled point in the active series as one action. */
/****************************************************************/
void removeAllPoints(GtkWidget *widget, gpointer data) {
	DataSeries *series;
	PointSetChange *change;
	GtkWidget *dialog;
	guint i;
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
	change = g_new0(PointSetChange, 1);
	change->series = series;
	change->records = g_array_new(FALSE, FALSE, sizeof(PointRecord));
	change->add_forward = FALSE;
	change->attached = TRUE;
	for (i = 0; i < series->points->len; i++) {
		PointRecord record;
		record.point = g_ptr_array_index(series->points, i);
		record.index = i;
		g_array_append_val(change->records, record);
	}
	executeHistoryCommand(tabData, history_command_new("Clear Series",
			applyPointSetChange, change, freePointSetChange), "clearing a series");
}

/****************************************************************/
/* This function handles all of the keypresses done within the	*/
/* main window and handles the  appropriate measures.		*/
/****************************************************************/
static void queueCurrentAxisReaderDraw(void) {
	struct TabData *tabData = getCurrentTabData();
	if (tabData != NULL)
		triggerUpdateDrawArea(tabData->drawing_area);
}

static void changePositioningCircleDiameter(gdouble change) {
	struct TabData *tabData;
	PositioningCircleDiameterChange *diameter_change;
	gdouble new_diameter;
	const gchar *label;

	tabData = getCurrentTabData();
	if (tabData == NULL)
		return;
	new_diameter = CLAMP(tabData->positioningCircleDiameter + change,
			0.5, 10000.0);
	if (new_diameter == tabData->positioningCircleDiameter)
		return;
	diameter_change = g_new0(PositioningCircleDiameterChange, 1);
	diameter_change->old_diameter = tabData->positioningCircleDiameter;
	diameter_change->new_diameter = new_diameter;
	label = change > 0.0 ? "Larger Positioning Circle"
			: "Smaller Positioning Circle";
	executeHistoryCommand(tabData, history_command_new(label,
			applyPositioningCircleDiameterChange, diameter_change, g_free),
			"changing positioning circle diameter");
}

static void changePositioningCircleFromMenu(GtkWidget *widget, gpointer data) {
	(void) widget;
	changePositioningCircleDiameter(0.5 * GPOINTER_TO_INT(data));
}

static void cancelAxisReaderDelay(void) {
	if (axisReaderTimeoutId != 0) {
		g_source_remove(axisReaderTimeoutId);
		axisReaderTimeoutId = 0;
	}
}

static void hideAxisReader(void) {
	gboolean was_visible = axisReaderVisible;
	cancelAxisReaderDelay();
	axisReaderVisible = FALSE;
	if (was_visible)
		queueCurrentAxisReaderDraw();
}

static gboolean showAxisReaderAfterDelay(gpointer data) {
	(void) data;
	axisReaderTimeoutId = 0;
	if (axisReaderAltHeld && !axisReaderChorded) {
		axisReaderVisible = TRUE;
		queueCurrentAxisReaderDraw();
	}
	return G_SOURCE_REMOVE;
}

gint keyPressEvent(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	GtkAdjustment *adjustment;
	gdouble adj_val;
	struct TabData *tabData;
	gboolean is_alt;

	(void) widget;
	(void) data;
	is_alt = event->keyval == GDK_KEY_Alt_L
			|| event->keyval == GDK_KEY_Alt_R;
	if (is_alt) {
		if (!axisReaderAltHeld) {
			axisReaderAltHeld = TRUE;
			axisReaderChorded = FALSE;
			axisReaderTimeoutId = g_timeout_add(AXIS_READER_DELAY_MS,
					showAxisReaderAfterDelay, NULL);
		}
		return 0;
	}
	if (axisReaderAltHeld) {
		axisReaderChorded = TRUE;
		hideAxisReader();
	}

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
	(void) widget;
	(void) data;
	if (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R) {
		axisReaderAltHeld = FALSE;
		axisReaderChorded = FALSE;
		hideAxisReader();
	}
	return 0;
}

static gboolean windowFocusOutEvent(GtkWidget *widget, GdkEventFocus *event,
		gpointer data) {
	(void) widget;
	(void) event;
	(void) data;
	axisReaderAltHeld = FALSE;
	axisReaderChorded = FALSE;
	hideAxisReader();
	return FALSE;
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
	tabData->positioningCircleDiameter =
			G3_DEFAULT_POSITIONING_CIRCLE_DIAMETER;
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
	g_signal_connect(G_OBJECT(tabData->drawing_area), "leave-notify-event",
			G_CALLBACK(mouseLeaveEvent), tabData);

	g_signal_connect(G_OBJECT (tabData->drawing_area), "scroll_event",
			G_CALLBACK (mouseScrollEvent), tabData);

	gtk_widget_set_events(
			tabData->drawing_area,
			GDK_EXPOSURE_MASK | /* Set the events active */
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
					| GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
					| GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK);

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
	tabData->loadingStore = TRUE;
	calibrationToTab(tabData, calibration);
	tabData->committedCalibration = *calibration;
	tabData->loadingStore = FALSE;
	setButtonSensitivity(tabData);
	refreshAxisReader(tabData);
	refreshCalibrationWorkflow(tabData);
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
	if (!calibrationIsComplete(tabData)) {
		gint i;
		for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
			if (!tabData->bpressed[i]) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
						tabData->setxybutton[i]), TRUE);
				break;
			}
	}
	refreshCalibrationWorkflow(tabData);
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
	history_free(tabData->history);
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
	GtkWidget *bottomhbox;
	GtkWidget *tlvbox, *brvbox, *blvbox, *subvbox;
	GtkWidget *xy_label[4]; /* Labels for texts in window */
	GtkWidget *logcheckb[2]; /* Logarithmic checkbuttons */
	GtkWidget *ScrollWindow, *controls_scroll; /* Various widgets */
	GtkWidget *APlabel, *ZAlabel, *Slabel, *tab_label;
	GtkWidget *alignment, *fixed;
	GtkWidget *dialog;
	GtkWidget *drawing_area_alignment;
	GtkWidget *series_buttons, *add_series_button, *series_scrolled;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

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
	tabData->history = history_new(tabData);
	calibration_state_clear(&tabData->committedCalibration);
	tabData->loadingStore = FALSE;
	tabData->movedPoint = NULL;
	tabData->movedSeries = NULL;
	tabData->series_view = NULL;
	tabData->series_store = NULL;
	tabData->series_color_button = NULL;
	tabData->series_visible_check = NULL;
	tabData->selected_point_label = NULL;
	tabData->calibration_status_label = NULL;
	tabData->calibration_cancel_button = NULL;

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
		gtk_entry_set_width_chars(GTK_ENTRY(tabData->xyentry[i]), 7);
		gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i], FALSE);
		gtk_widget_set_sensitive(tabData->xyentry[i], FALSE); /* Inactivate it */
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (tabData->xyentry[i]), "changed", /* Init the entry to call */
		G_CALLBACK (readXYEntryValues), buttonData);
		g_signal_connect(tabData->xyentry[i], "activate",
				G_CALLBACK(axisEntryActivated), buttonData);
		g_signal_connect(tabData->xyentry[i], "focus-out-event",
				G_CALLBACK(axisEntryFocusOut), buttonData);
		/* read_x1_entry whenever */
		gtk_widget_set_tooltip_text(tabData->xyentry[i], entryxytt[i]);
	}

	tabData->zoom_area = gtk_drawing_area_new(); /* Create new drawing area */
	gtk_widget_set_size_request(tabData->zoom_area, ZOOMPIXSIZE, ZOOMPIXSIZE);
	g_signal_connect(G_OBJECT (tabData->zoom_area), "draw",
			G_CALLBACK (updateZoomArea), tabData);

	for (i = 0; i < 4; i++) {
		xy_label[i] = gtk_label_new(NULL);
		gtk_label_set_markup((GtkLabel *) xy_label[i], xy_label_text[i]);
	}

	for (i = 0; i < 4; i++) {
		tabData->setxybutton[i] = gtk_toggle_button_new_with_label("Pick");
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

	bottomhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SECT_SEP);
	alignment = g3AlignmentNew(0, 0, 1, 1);
	g3TableAttach(table, alignment, 0, 1, 0, 1, 5, 5, 0, 0);
	gtk_container_add((GtkContainer *) alignment, bottomhbox);

	tlvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	APlabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (APlabel), APheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, APlabel);
	gtk_box_pack_start(GTK_BOX (tlvbox), alignment, FALSE, FALSE, 0);
	tabData->calibration_status_label = gtk_label_new(
			"Select X1 on the image (1 of 4).\nThen enter its axis value.");
	gtk_label_set_xalign(GTK_LABEL(tabData->calibration_status_label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(tabData->calibration_status_label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(tabData->calibration_status_label),
			28);
	gtk_box_pack_start(GTK_BOX(tlvbox), tabData->calibration_status_label,
			FALSE, FALSE, 0);
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
	for (i = 0; i < 2; i++)
		gtk_box_pack_start(GTK_BOX(tlvbox), logcheckb[i], FALSE, FALSE, 0);
	tabData->calibration_cancel_button = gtk_button_new_with_label(
			"Sample data instead");
	g_signal_connect(tabData->calibration_cancel_button, "clicked",
			G_CALLBACK(cancelCalibrationMode), tabData);
	gtk_box_pack_start(GTK_BOX(tlvbox), tabData->calibration_cancel_button,
			FALSE, FALSE, 0);

	blvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, GROUP_SEP);
	controls_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(controls_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(controls_scroll, TRUE);
	gtk_widget_set_hexpand(controls_scroll, FALSE);
	gtk_widget_set_valign(controls_scroll, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(bottomhbox), controls_scroll, FALSE, TRUE,
			ELEM_SEP);
	gtk_container_add(GTK_CONTAINER(controls_scroll), blvbox);
	gtk_box_pack_start(GTK_BOX(blvbox), tlvbox, FALSE, FALSE, 0);

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX(blvbox), subvbox, FALSE, FALSE, 0);
	Slabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(Slabel), "<b>Data series</b>");
	gtk_widget_set_halign(Slabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(subvbox), Slabel, FALSE, FALSE, 0);

	tabData->series_store = gtk_list_store_new(SERIES_N_COLUMNS,
			GDK_TYPE_RGBA, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_BOOLEAN,
			G_TYPE_POINTER);
	tabData->series_view = gtk_tree_view_new_with_model(
			GTK_TREE_MODEL(tabData->series_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tabData->series_view), TRUE);
	gtk_widget_set_tooltip_text(tabData->series_view,
			"Select the series that receives new points; double-click or press Enter to rename");
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "text", "\342\227\217", NULL);
	column = gtk_tree_view_column_new_with_attributes("", renderer,
			"foreground-rgba", SERIES_COL_COLOR, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(column, 24);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tabData->series_view), column);
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", G_CALLBACK(seriesLabelEdited), tabData);
	column = gtk_tree_view_column_new_with_attributes("Series", renderer,
			"text", SERIES_COL_LABEL, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(column, 105);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tabData->series_view), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("#", renderer,
			"text", SERIES_COL_COUNT, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(column, 42);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tabData->series_view), column);
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(renderer, "toggled", G_CALLBACK(seriesVisibleToggled),
			tabData);
	column = gtk_tree_view_column_new_with_attributes("Show", renderer,
			"active", SERIES_COL_VISIBLE, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width(column, 48);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tabData->series_view), column);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(
			tabData->series_view)), "changed", G_CALLBACK(activeSeriesChanged),
			tabData);
	g_signal_connect(tabData->series_view, "button-press-event",
			G_CALLBACK(seriesViewButtonPress), tabData);
	g_signal_connect(tabData->series_view, "key-press-event",
			G_CALLBACK(seriesViewKeyPress), tabData);
	series_scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(series_scrolled),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(series_scrolled, -1, 142);
	gtk_container_add(GTK_CONTAINER(series_scrolled), tabData->series_view);
	gtk_box_pack_start(GTK_BOX(subvbox), series_scrolled, FALSE, FALSE, 0);

	series_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ELEM_SEP);
	add_series_button = gtk_button_new_with_label("+");
	gtk_widget_set_tooltip_text(add_series_button, "Add a new data series");
	g_signal_connect(add_series_button, "clicked", G_CALLBACK(addSeries), tabData);
	gtk_box_pack_end(GTK_BOX(series_buttons), add_series_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(subvbox), series_buttons, FALSE, FALSE, 0);

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

	tabData->selected_point_label = gtk_label_new("No point selected");
	gtk_label_set_xalign(GTK_LABEL(tabData->selected_point_label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(tabData->selected_point_label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(tabData->selected_point_label), 28);
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

	tabData->logbox = tlvbox;

	if (FileInCwd) {
		strncpy(tabData->FileNames, basename(filename), 256);
	} else {
		strncpy(tabData->FileNames, filename, 256);
	}

	snprintf(buf, 256, Window_Title, tabData->FileNames); /* Print window title in buffer */
	gtk_window_set_title(GTK_WINDOW (window), buf); /* Set window title */


	gtk_scrolled_window_set_min_content_width(
			GTK_SCROLLED_WINDOW(controls_scroll), 225);

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
			tabData->loadingStore = TRUE;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					tabData->setxybutton[i]), FALSE);
			tabData->loadingStore = FALSE;
		}
		if (Uselogxy != NULL) {
			for (i = 0; i < 2; i++) {
				tabData->logxy[i] = Uselogxy[i];
				gtk_toggle_button_set_active(
						GTK_TOGGLE_BUTTON(tabData->logcheckbutton[i]), Uselogxy[i]);
			}
		}
		persistCalibration(tabData);
		refreshCalibrationWorkflow(tabData);
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

static void toggleAxisReaderUncertainty(GtkCheckMenuItem *widget,
		gpointer data) {
	(void) data;
	showAxisReaderUncertainty = gtk_check_menu_item_get_active(widget);
	queueCurrentAxisReaderDraw();
}

static void togglePositioningCircle(GtkCheckMenuItem *widget, gpointer data) {
	(void) data;
	showPositioningCircle = gtk_check_menu_item_get_active(widget);
	savePreferences();
	queueCurrentAxisReaderDraw();
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
	if (copy_current_menu_item != NULL)
		gtk_widget_set_sensitive(copy_current_menu_item, current_ready);
	if (copy_all_menu_item != NULL)
		gtk_widget_set_sensitive(copy_all_menu_item, all_ready);
}

static void updateEditMenuSensitivity(struct TabData *tabData) {
	DataSeries *series;
	gboolean has_points;
	guint selection_count;

	series = tabData != NULL ? activeSeries(tabData) : NULL;
	has_points = series != NULL && series->points->len > 0;
	selection_count = tabData != NULL && tabData->document != NULL
			? image_document_selection_count(tabData->document) : 0;
	if (clear_series_menu_item != NULL)
		gtk_widget_set_sensitive(clear_series_menu_item, has_points);
	if (delete_selected_menu_item != NULL) {
		gtk_menu_item_set_label(GTK_MENU_ITEM(delete_selected_menu_item),
				selection_count == 1 ? "_Delete selected point"
						: "_Delete selected points");
		gtk_widget_set_sensitive(delete_selected_menu_item, selection_count > 0);
	}
	updateHistoryMenuSensitivity(tabData);
}

static void updateHistoryMenuSensitivity(struct TabData *tabData) {
	const gchar *label;
	gchar *caption;

	label = tabData != NULL ? history_undo_label(tabData->history) : NULL;
	caption = label != NULL ? g_strdup_printf("_Undo %s", label)
			: g_strdup("_Undo");
	if (undo_menu_item != NULL) {
		gtk_menu_item_set_label(GTK_MENU_ITEM(undo_menu_item), caption);
		gtk_widget_set_sensitive(undo_menu_item, label != NULL);
	}
	g_free(caption);
	label = tabData != NULL ? history_redo_label(tabData->history) : NULL;
	caption = label != NULL ? g_strdup_printf("_Redo %s", label)
			: g_strdup("_Redo");
	if (redo_menu_item != NULL) {
		gtk_menu_item_set_label(GTK_MENU_ITEM(redo_menu_item), caption);
		gtk_widget_set_sensitive(redo_menu_item, label != NULL);
	}
	g_free(caption);
}

static void refreshActionMenuSensitivity(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;

	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	updateExportMenuSensitivity(tabData);
	updateEditMenuSensitivity(tabData);
}

static void undoFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	GError *error;

	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData == NULL)
		return;
	error = NULL;
	if (!history_undo(tabData->history, &error)) {
		if (error != NULL)
			reportDatastoreError("undoing a change", error);
		return;
	}
	refreshAfterHistoryChange(tabData);
}

static void redoFromMenu(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	GError *error;

	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData == NULL)
		return;
	error = NULL;
	if (!history_redo(tabData->history, &error)) {
		if (error != NULL)
			reportDatastoreError("redoing a change", error);
		return;
	}
	refreshAfterHistoryChange(tabData);
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

static void clipboardExportGet(GtkClipboard *clipboard,
		GtkSelectionData *selection_data, guint info, gpointer data) {
	ClipboardExport *export_data;

	(void) clipboard;
	export_data = (ClipboardExport *) data;
	if (export_data == NULL || export_data->text == NULL)
		return;
	if (info == CLIPBOARD_TARGET_TSV) {
		gtk_selection_data_set(selection_data,
				gtk_selection_data_get_target(selection_data), 8,
				(const guchar *) export_data->text,
				(gint) strlen(export_data->text));
	} else {
		gtk_selection_data_set_text(selection_data, export_data->text, -1);
	}
}

static void clipboardExportClear(GtkClipboard *clipboard, gpointer data) {
	ClipboardExport *export_data;

	(void) clipboard;
	export_data = (ClipboardExport *) data;
	if (export_data == NULL)
		return;
	g_free(export_data->text);
	g_free(export_data);
}

static void copyExportToClipboard(GtkClipboard *clipboard, const gchar *text) {
	ClipboardExport *export_data;

	export_data = g_new0(ClipboardExport, 1);
	export_data->text = g_strdup(text);
	if (!gtk_clipboard_set_with_data(clipboard, CLIPBOARD_EXPORT_TARGETS,
			G_N_ELEMENTS(CLIPBOARD_EXPORT_TARGETS), clipboardExportGet,
			clipboardExportClear, export_data)) {
		clipboardExportClear(clipboard, export_data);
		return;
	}
	gtk_clipboard_set_can_store(clipboard, CLIPBOARD_EXPORT_TARGETS,
			G_N_ELEMENTS(CLIPBOARD_EXPORT_TARGETS));
	gtk_clipboard_store(clipboard);
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
		/* Advertise TSV explicitly so spreadsheet applications do not infer
		 * Markdown from all-series "# label" rows. */
		copyExportToClipboard(clipboard, output->str);
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
	GtkWidget *zoom_area_item, *axis_settings_item, *show_uncertainty_item;
	GtkWidget *show_positioning_circle_item, *larger_circle_item;
	GtkWidget *smaller_circle_item;
	GtkWidget *fullscreen_item;
	GtkWidget *zoom100_item, *zoom200_item, *zoomfit_item;
	GtkWidget *separator_item, *separator_item2;
	GtkAccelGroup *accel_group;
	const gchar *export_destination_labels[EXPORT_TO_CLIPBOARD] = {
		"To _stdout", "To _file…"
	};
	gint scope, target;
	GSList *ordering_group;

	gtk_init(&argc, &argv); /* Init GTK */
	loadPreferences();

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
	g_signal_connect(G_OBJECT(window), "focus-out-event",
			G_CALLBACK(windowFocusOutEvent), NULL);

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
	g_signal_connect(file_menu, "show",
			G_CALLBACK(refreshActionMenuSensitivity), NULL);
	g_signal_connect(edit_menu, "show",
			G_CALLBACK(refreshActionMenuSensitivity), NULL);

	open_item = gtk_menu_item_new_with_mnemonic("_Open…");
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

	for (scope = 0; scope < 2; scope++) {
		GtkWidget *scope_menu;
		scope_menu = gtk_menu_new();
		for (target = 0; target < EXPORT_TO_CLIPBOARD; target++) {
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
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), ordering_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), include_errors_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), close_menu_item);
	separator_item2 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item2);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);

	loadRecentFiles();
	rebuildRecentFileMenu();

	undo_menu_item = gtk_menu_item_new_with_mnemonic("_Undo");
	redo_menu_item = gtk_menu_item_new_with_mnemonic("_Redo");
	clear_series_menu_item = gtk_menu_item_new_with_mnemonic(
			"_Clear current series…");
	delete_selected_menu_item = gtk_menu_item_new_with_mnemonic(
			"_Delete selected points");
	copy_current_menu_item = gtk_menu_item_new_with_mnemonic(
			"Copy current _series");
	copy_all_menu_item = gtk_menu_item_new_with_mnemonic("Copy _all series");
	gtk_widget_set_sensitive(undo_menu_item, FALSE);
	gtk_widget_set_sensitive(redo_menu_item, FALSE);
	gtk_widget_set_sensitive(clear_series_menu_item, FALSE);
	gtk_widget_set_sensitive(delete_selected_menu_item, FALSE);
	gtk_widget_set_sensitive(copy_current_menu_item, FALSE);
	gtk_widget_set_sensitive(copy_all_menu_item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), undo_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), redo_menu_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), clear_series_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), delete_selected_menu_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_current_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_all_menu_item);

	zoom_area_item = gtk_check_menu_item_new_with_label("Zoom area");
	axis_settings_item = gtk_check_menu_item_new_with_label("Axis settings");
	show_uncertainty_item = gtk_check_menu_item_new_with_label(
			"Show uncertainty");
	show_positioning_circle_item = gtk_check_menu_item_new_with_label(
			"Show positioning circle");
	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(show_positioning_circle_item),
			showPositioningCircle);
	larger_circle_item = gtk_menu_item_new_with_label("Larger circle");
	smaller_circle_item = gtk_menu_item_new_with_label("Smaller circle");
	fullscreen_item = gtk_check_menu_item_new_with_mnemonic("_Full Screen");
	zoom100_item = gtk_menu_item_new_with_label("Zoom 100%");
	zoom200_item = gtk_menu_item_new_with_label("Zoom 200%");
	zoomfit_item = gtk_menu_item_new_with_label("Zoom to fit");

	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom_area_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), axis_settings_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), show_uncertainty_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu),
			show_positioning_circle_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), larger_circle_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), smaller_circle_item);
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
	g_signal_connect(undo_menu_item, "activate", G_CALLBACK(undoFromMenu), NULL);
	g_signal_connect(redo_menu_item, "activate", G_CALLBACK(redoFromMenu), NULL);
	g_signal_connect(clear_series_menu_item, "activate",
			G_CALLBACK(clearSeriesFromMenu), NULL);
	g_signal_connect(delete_selected_menu_item, "activate",
			G_CALLBACK(deleteSelectedFromMenu), NULL);
	g_signal_connect(copy_current_menu_item, "activate",
			G_CALLBACK(exportFromMenu),
			GINT_TO_POINTER(EXPORT_TO_CLIPBOARD));
	g_signal_connect(copy_all_menu_item, "activate",
			G_CALLBACK(exportFromMenu),
			GINT_TO_POINTER(EXPORT_TARGET_COUNT + EXPORT_TO_CLIPBOARD));
	g_signal_connect(G_OBJECT(about_item), "activate", G_CALLBACK(menuHelpAbout),
			NULL);

	g_signal_connect(G_OBJECT(zoom_area_item), "toggled",
			G_CALLBACK(hideZoomArea), NULL);
	g_signal_connect(G_OBJECT(axis_settings_item), "toggled",
			G_CALLBACK(hideAxisSettings), NULL);
	g_signal_connect(show_uncertainty_item, "toggled",
			G_CALLBACK(toggleAxisReaderUncertainty), NULL);
	g_signal_connect(show_positioning_circle_item, "toggled",
			G_CALLBACK(togglePositioningCircle), NULL);
	g_signal_connect(larger_circle_item, "activate",
			G_CALLBACK(changePositioningCircleFromMenu), GINT_TO_POINTER(1));
	g_signal_connect(smaller_circle_item, "activate",
			G_CALLBACK(changePositioningCircleFromMenu), GINT_TO_POINTER(-1));
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
	gtk_widget_add_accelerator(undo_menu_item, "activate", accel_group,
			GDK_KEY_Z, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(redo_menu_item, "activate", accel_group,
			GDK_KEY_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(larger_circle_item, "activate", accel_group,
			GDK_KEY_period, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(smaller_circle_item, "activate", accel_group,
			GDK_KEY_comma, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

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
