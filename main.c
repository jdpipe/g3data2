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
static const gdouble MAIN_IMAGE_MAX_ZOOM = 8.0;
static const gdouble MAIN_IMAGE_ZOOM_STEP = 1.25;
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

static const char *DROPPED_URI_DELIMITER = "\r\n";

#ifdef G3DATA2_DEBUG
#define G3DBG(...) g_printerr("[g3data2][debug] " __VA_ARGS__)
#else
#define G3DBG(...) ((void) 0)
#endif

static void setButtonSensitivity(struct TabData *tabData);
static void triggerUpdateDrawArea(GtkWidget *area);
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

struct RecentFileEntry {
	gchar *path;
	gchar *date;
};

GPtrArray *recent_files;
GPtrArray *recent_menu_items;

// Declaration of global variables
gboolean MovePointMode = FALSE;
gboolean HideLog = FALSE, HideZoomArea = FALSE, HideOpProp = FALSE;

// Declaration of extern functions
extern void drawMarker(cairo_t *cr, gint x, gint y, gint type);
extern struct PointValue calculatePointValue(gdouble Xpos, gdouble Ypos,
		struct TabData *tabData);
extern void outputResultset(GtkWidget *widget, gpointer func_data);

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
			"%s: vp_alloc=%dx%d da_alloc=%dx%d hadj[v=%.2f lo=%.2f up=%.2f page=%.2f] vadj[v=%.2f lo=%.2f up=%.2f page=%.2f] zoom=%.6f origin=(%.2f,%.2f) canvas=(%.2f,%.2f) image=%dx%d pending_fit=%d\n",
			tag, vpW, vpH, daW, daH, hVal, hLower, hUpper, hPage, vVal, vLower, vUpper,
			vPage, tabData->viewZoom, tabData->viewOrigin[0], tabData->viewOrigin[1],
			tabData->viewCanvasSize[0], tabData->viewCanvasSize[1], tabData->XSize,
			tabData->YSize, tabData->pendingInitialZoomToFit);
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

	tabData = (struct TabData *) data;

	if (tabData->mousePointerCoords[0] >= 0 && tabData->mousePointerCoords[1] >= 0) {

		first = cairo_surface_create_similar(cairo_get_target(cr),
				CAIRO_CONTENT_COLOR, ZOOMPIXSIZE, ZOOMPIXSIZE);

		first_cr = cairo_create(first);
		cairo_scale(first_cr, ZOOMFACTOR, ZOOMFACTOR);
		cairo_set_source_surface(
				first_cr,
				tabData->image,
				-tabData->mousePointerCoords[0]
						+ ZOOMPIXSIZE / (2 * ZOOMFACTOR),
				-tabData->mousePointerCoords[1]
						+ ZOOMPIXSIZE / (2 * ZOOMFACTOR));
		cairo_paint(first_cr);
		cairo_scale(first_cr, 1.0 / ZOOMFACTOR, 1.0 / ZOOMFACTOR);

		drawMarker(first_cr, ZOOMPIXSIZE / 2, ZOOMPIXSIZE / 2, 2);

		cairo_set_source_surface(cr, first, 0, 0);
		cairo_paint(cr);

		cairo_surface_destroy(first);

		cairo_destroy(first_cr);
	}

	return TRUE;
}

static gdouble clampZoom(gdouble zoom) {
	if (zoom < MAIN_IMAGE_MIN_ZOOM)
		return MAIN_IMAGE_MIN_ZOOM;
	if (zoom > MAIN_IMAGE_MAX_ZOOM)
		return MAIN_IMAGE_MAX_ZOOM;
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

	newZoom = clampZoom(newZoom);
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

	gtk_adjustment_set_value(hadj, hNewValue);
	gtk_adjustment_set_value(vadj, vNewValue);
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
	return clampZoom(MIN(fitX, fitY));
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
	GtkAdjustment *hadj, *vadj;
	gdouble hUpper, vUpper;

	if (tabData == NULL || !tabData->pendingRecenterOnAdjust
			|| tabData->ViewPort == NULL)
		return;

	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tabData->ViewPort));
	hUpper = gtk_adjustment_get_upper(hadj);
	vUpper = gtk_adjustment_get_upper(vadj);

	if (hUpper + 1.0 < tabData->viewCanvasSize[0]
			|| vUpper + 1.0 < tabData->viewCanvasSize[1]) {
		G3DBG(
				"maybeApplyPendingRecenter: waiting (bounds too small) upper=(%.2f,%.2f) canvas=(%.2f,%.2f)\n",
				hUpper, vUpper, tabData->viewCanvasSize[0], tabData->viewCanvasSize[1]);
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
	maybeApplyPendingRecenter(tabData);
}

static void zoomToFitAndCenter(struct TabData *tabData) {
	debugDumpViewportState("zoomToFitAndCenter:begin", tabData);
	setMainImageZoom(tabData, calculateZoomToFit(tabData), -1.0, -1.0);
	tabData->pendingRecenterOnAdjust = TRUE;
	maybeApplyPendingRecenter(tabData);
	debugDumpViewportState("zoomToFitAndCenter:end", tabData);
}

static void maybeApplyInitialZoomToFit(struct TabData *tabData) {
	if (tabData == NULL || !tabData->pendingInitialZoomToFit
			|| tabData->drawing_area == NULL || tabData->ViewPort == NULL)
		return;
	if (tabData->XSize <= 0 || tabData->YSize <= 0)
		return;
	if (gtk_widget_get_allocated_width(tabData->ViewPort) <= 1
			|| gtk_widget_get_allocated_height(tabData->ViewPort) <= 1)
		return;

	debugDumpViewportState("maybeApplyInitialZoomToFit:ready", tabData);
	zoomToFitAndCenter(tabData);
	tabData->pendingInitialZoomToFit = FALSE;
	debugDumpViewportState("maybeApplyInitialZoomToFit:done", tabData);
}

static gboolean applyInitialZoomToFit(gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	debugDumpViewportState("applyInitialZoomToFit", tabData);
	maybeApplyInitialZoomToFit(tabData);
	return G_SOURCE_REMOVE;
}

static void viewportSizeAllocateEvent(GtkWidget *widget, GtkAllocation *allocation,
		gpointer data) {
	(void) widget;
	G3DBG("viewportSizeAllocateEvent: alloc=%dx%d\n", allocation->width,
			allocation->height);
	maybeApplyInitialZoomToFit((struct TabData *) data);
	maybeApplyPendingRecenter((struct TabData *) data);
}

static void getImageCoords(struct TabData *tabData, gdouble widgetX,
		gdouble widgetY, gdouble *imageX, gdouble *imageY) {
	if (tabData->viewZoom <= 0)
		tabData->viewZoom = 1.0;
	*imageX = (widgetX - tabData->viewOrigin[0]) / tabData->viewZoom;
	*imageY = (widgetY - tabData->viewOrigin[1]) / tabData->viewZoom;
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
			tabData->lastpoints[tabData->numlastpoints] = -(i + 1);
			tabData->numlastpoints++;
			setButtonSensitivity(tabData);
			triggerUpdateDrawArea(tabData->drawing_area);
			break;
		}
	}
}

gboolean updateImageArea(GtkWidget *widget, cairo_t *cr, gpointer data) {
	gint i;
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
			drawMarker(cr, (gint) (tabData->axiscoords[i][0]
					* tabData->viewZoom + tabData->viewOrigin[0]), (gint) (tabData->axiscoords[i][1]
					* tabData->viewZoom + tabData->viewOrigin[1]), i / 2);
		}
	}

	for (i = 0; i < tabData->numpoints; i++) {
		drawMarker(cr,
				(gint) (tabData->points[i][0] * tabData->viewZoom
						+ tabData->viewOrigin[0]), (gint) (tabData->points[i][1]
						* tabData->viewZoom + tabData->viewOrigin[1]), 2);
	}

	return TRUE;
}
/****************************************************************/
/* This function sets the sensitivity of the buttons depending	*/
/* the control variables.					*/
/****************************************************************/
static void setButtonSensitivity(struct TabData *tabData) {
	char ttbuf[256];

	if (tabData->Action == PRINT2FILE) {
		snprintf(ttbuf, sizeof(ttbuf), printfilett,
				gtk_entry_get_text(GTK_ENTRY (tabData->file_entry)));
		gtk_widget_set_tooltip_text(tabData->exportbutton, ttbuf);

		gtk_widget_set_sensitive(tabData->file_entry, TRUE);
		if (tabData->valueset[0] && tabData->valueset[1] && tabData->valueset[2]
				&& tabData->valueset[3] && tabData->bpressed[0]
				&& tabData->bpressed[1] && tabData->bpressed[2]
				&& tabData->bpressed[3] && tabData->numpoints > 0
				&& tabData->file_name_length > 0)
			gtk_widget_set_sensitive(tabData->exportbutton, TRUE);
		else
			gtk_widget_set_sensitive(tabData->exportbutton, FALSE);
	} else {
		gtk_widget_set_tooltip_text(tabData->exportbutton, printrestt);
		gtk_widget_set_sensitive(tabData->file_entry, FALSE);
		if (tabData->valueset[0] && tabData->valueset[1] && tabData->valueset[2]
				&& tabData->valueset[3] && tabData->bpressed[0]
				&& tabData->bpressed[1] && tabData->bpressed[2]
				&& tabData->bpressed[3] && tabData->numpoints > 0)
			gtk_widget_set_sensitive(tabData->exportbutton, TRUE);
		else
			gtk_widget_set_sensitive(tabData->exportbutton, FALSE);
	}

	if (tabData->numlastpoints == 0) {
		gtk_widget_set_sensitive(tabData->remlastbutton, FALSE);
		gtk_widget_set_sensitive(tabData->remallbutton, FALSE);
	} else {
		gtk_widget_set_sensitive(tabData->remlastbutton, TRUE);
		gtk_widget_set_sensitive(tabData->remallbutton, TRUE);
	}
}

gboolean allocatePointDataMemory(struct TabData *tabData) {
	gint i;

	if (tabData->lastpoints == NULL) {
		tabData->lastpoints = (gint *) malloc(
				sizeof(gint) * (tabData->MaxPoints + 4));
		if (tabData->lastpoints == NULL) {
			printf("Error allocating memory for lastpoints. Exiting.\n");
			return FALSE;
		}
		tabData->points = (void *) malloc(sizeof(gdouble *) * tabData->MaxPoints);
		if (tabData->points == NULL) {
			printf("Error allocating memory for points. Exiting.\n");
			return FALSE;
		}
		for (i = 0; i < tabData->MaxPoints; i++) {
			tabData->points[i] = (gdouble *) malloc(sizeof(gdouble) * 2);
			if (tabData->points[i] == NULL) {
				printf("Error allocating memory for points[%d]. Exiting.\n", i);
				return FALSE;
			}
		}
		return TRUE;
	}
	if (tabData->numpoints > tabData->MaxPoints - 1) {
		i = tabData->MaxPoints;
		tabData->MaxPoints += MAXPOINTS;
		tabData->lastpoints = realloc(tabData->lastpoints,
				sizeof(gint) * (tabData->MaxPoints + 4));
		if (tabData->lastpoints == NULL) {
			printf("Error reallocating memory for lastpoints. Exiting.\n");
			return FALSE;
		}
		tabData->points = realloc(tabData->points,
				sizeof(gdouble *) * tabData->MaxPoints);
		if (tabData->points == NULL) {
			printf("Error reallocating memory for points. Exiting.\n");
			return FALSE;
		}
		for (; i < tabData->MaxPoints; i++) {
			tabData->points[i] = malloc(sizeof(gdouble) * 2);
			if (tabData->points[i] == NULL) {
				printf("Error allocating memory for points[%d]. Exiting.\n", i);
				return FALSE;
			}
		}
	}
	return TRUE;
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

static void triggerUpdateDrawArea(GtkWidget *area) {
	gtk_widget_queue_draw(area);
}

void triggerLimitedUpdateDrawArea(GtkWidget *area, gint x, gint y) {
	gtk_widget_queue_draw_area(area, x - (MARKERSIZE + MARKERTHICKNESS),
			y - (MARKERSIZE + MARKERTHICKNESS),
			2 * (MARKERSIZE + MARKERTHICKNESS),
			2 * (MARKERSIZE + MARKERTHICKNESS));
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
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;

	getImageCoords(tabData, event->x, event->y, &imageX, &imageY);

	allocatePointDataMemory(tabData);

	if (event->button == 1) { /* If button 1 (leftmost) is pressed */
		if (MovePointMode) {
			for (i = 0; i < tabData->numpoints; i++) {
				if (fabs(tabData->points[i][0] - imageX) < GRABTRESHOLD
						&& fabs(tabData->points[i][1] - imageY) < GRABTRESHOLD) {
					//					printf("Moving point %d\n", i);
					tabData->movedPointIndex = i;
					tabData->movedOrigCoords[0] = tabData->points[i][0];
					tabData->movedOrigCoords[1] = tabData->points[i][1];
					tabData->movedOrigMousePtrCoords[0] = imageX;
					tabData->movedOrigMousePtrCoords[1] = imageY;
					break;
				}
			}
		} else {
			/* If none of the set axispoint buttons been pressed */
			if (!tabData->setxypressed[0] && !tabData->setxypressed[1]
					&& !tabData->setxypressed[2] && !tabData->setxypressed[3]) {
				tabData->points[tabData->numpoints][0] = imageX; /* Save x coordinate */
				tabData->points[tabData->numpoints][1] = imageY; /* Save x coordinate */
				tabData->lastpoints[tabData->numlastpoints] =
						tabData->numpoints; /* Save index of point */
				tabData->numlastpoints++; /* Increase lastpoint index */
				tabData->numpoints++; /* Increase point counter */
				setNumberOfPointsEntryValue(tabData->nump_entry,
						tabData->numpoints);

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
						tabData->lastpoints[tabData->numlastpoints] = -(i + 1); /* Remember that the points been put out */
						tabData->numlastpoints++; /* Increase index of lastpoints */

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
				tabData->lastpoints[tabData->numlastpoints] = -(i + 1);
				tabData->numlastpoints++;

				break;
			}
	}

	triggerUpdateDrawArea(tabData->drawing_area);

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
	gint i;
	gdouble imageX, imageY;
	struct TabData *tabData;

	(void) widget;
	tabData = (struct TabData *) data;

	getImageCoords(tabData, event->x, event->y, &imageX, &imageY);

	if (event->button == 1) {
		if (MovePointMode && tabData->movedPointIndex != NONESELECTED) {
			i = tabData->movedPointIndex;
			tabData->points[i][0] = tabData->movedOrigCoords[0]
					+ (imageX - tabData->movedOrigMousePtrCoords[0]);
			tabData->points[i][1] = tabData->movedOrigCoords[1]
					+ (imageY - tabData->movedOrigMousePtrCoords[1]);
			tabData->movedPointIndex = NONESELECTED;
			triggerUpdateDrawArea(tabData->drawing_area);
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
	gint i;
	gdouble imageX, imageY;
	gchar buf[32];
	struct PointValue CalcVal;
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
			if (fabs(dx) > 1.0 || fabs(dy) > 1.0)
				tabData->middlePanMoved = TRUE;

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

	if (imageX >= 0 && imageY >= 0 && imageX < tabData->XSize
			&& imageY < tabData->YSize) {
		if (MovePointMode && tabData->movedPointIndex != NONESELECTED) {
			i = tabData->movedPointIndex;
			tabData->points[i][0] = tabData->movedOrigCoords[0]
					+ (imageX - tabData->movedOrigMousePtrCoords[0]);
			tabData->points[i][1] = tabData->movedOrigCoords[1]
					+ (imageY - tabData->movedOrigMousePtrCoords[1]);
			tabData->mousePointerCoords[0] = tabData->points[i][0];
			tabData->mousePointerCoords[1] = tabData->points[i][1];

			triggerUpdateDrawArea(tabData->drawing_area);
		} else {
			tabData->mousePointerCoords[0] = imageX;
			tabData->mousePointerCoords[1] = imageY;
		}

		triggerUpdateDrawArea(tabData->zoom_area);

		if (tabData->valueset[0] && tabData->valueset[1] && tabData->valueset[2]
				&& tabData->valueset[3]) {
			CalcVal = calculatePointValue(imageX, imageY, tabData);

			sprintf(buf, "%16.10g", CalcVal.Xv);
			gtk_entry_set_text(GTK_ENTRY(tabData->xc_entry), buf); /* Put out coordinates in entries */
			sprintf(buf, "%16.10g", CalcVal.Yv);
			gtk_entry_set_text(GTK_ENTRY(tabData->yc_entry), buf);
			sprintf(buf, "%16.10g", CalcVal.Xerr);
			gtk_entry_set_text(GTK_ENTRY(tabData->xerr_entry), buf); /* Put out coordinates in entries */
			sprintf(buf, "%16.10g", CalcVal.Yerr);
			gtk_entry_set_text(GTK_ENTRY(tabData->yerr_entry), buf);
		} else {
			gtk_entry_set_text(GTK_ENTRY(tabData->xc_entry), ""); /* Else clear entries */
			gtk_entry_set_text(GTK_ENTRY(tabData->yc_entry), "");
			gtk_entry_set_text(GTK_ENTRY(tabData->xerr_entry), "");
			gtk_entry_set_text(GTK_ENTRY(tabData->yerr_entry), "");
		}
	} else {
		gtk_entry_set_text(GTK_ENTRY(tabData->xc_entry), ""); /* Else clear entries */
		gtk_entry_set_text(GTK_ENTRY(tabData->yc_entry), "");
		gtk_entry_set_text(GTK_ENTRY(tabData->xerr_entry), "");
		gtk_entry_set_text(GTK_ENTRY(tabData->yerr_entry), "");
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
	gint viewportX, viewportY;
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

		getImageCoords(tabData, event->x, event->y, &imageX, &imageY);
		if (imageX >= 0.0 && imageY >= 0.0 && imageX < tabData->XSize
				&& imageY < tabData->YSize) {
			if (gtk_widget_translate_coordinates(widget, tabData->ViewPort,
					(gint) event->x, (gint) event->y, &viewportX, &viewportY)) {
				focusX = viewportX;
				focusY = viewportY;
			} else {
				focusX = -1.0;
				focusY = -1.0;
			}
		} else {
			focusX = -1.0;
			focusY = -1.0;
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
	} else { /* If button is trying to get unpressed */
		if (tabData->setxypressed[index])
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE); /* Set button down */
	}
}

/****************************************************************/
/* Set type of ordering at output of data.			*/
/****************************************************************/
void setOutputOrdering(GtkWidget *widget, gpointer data) {
	gint ordering;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	ordering = buttonData->index;
	tabData = buttonData->tabData;
	tabData->ordering = ordering; /* Set ordering control variable */
}

/****************************************************************/
/****************************************************************/
void setOutputAction(GtkWidget *widget, gpointer data) {
	gint action;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	action = buttonData->index;
	tabData = buttonData->tabData;
	tabData->Action = action;
	setButtonSensitivity(tabData);
}

/****************************************************************/
/* Set whether to use error evaluation and printing or not.	*/
/****************************************************************/
void setPrintErrorUsage(GtkToggleButton *widget, gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;
	tabData->UseErrors = gtk_toggle_button_get_active(widget);
}

/****************************************************************/
/* When the value of the entry of any axis point is changed, 	*/
/* this function gets called.					*/
/****************************************************************/
void readXYEntryValues(GtkWidget *entry, gpointer data) {
	gchar *xy_text;
	gint index;
	struct ButtonData *buttonData;
	struct TabData *tabData;

	buttonData = (struct ButtonData *) data;
	index = buttonData->index;
	tabData = buttonData->tabData;

	xy_text = (gchar *) gtk_entry_get_text(GTK_ENTRY (entry));
	sscanf(xy_text, "%lf", &(tabData->realcoords[index]));
	if (tabData->logxy[index / 2] && tabData->realcoords[index] > 0)
		tabData->valueset[index] = TRUE;
	else if (tabData->logxy[index / 2])
		tabData->valueset[index] = FALSE;
	else
		tabData->valueset[index] = TRUE;

	setButtonSensitivity(tabData);
}

/****************************************************************/
/* If all the axispoints has been put out, values for these	*/
/* have been assigned and at least one point has been set on	*/
/* the graph activate the write to file button.			*/
/****************************************************************/
void readFileEntry(GtkWidget *entry, gpointer data) {
	struct TabData *tabData;

	tabData = (struct TabData *) data;

	tabData->file_name = (gchar *) gtk_entry_get_text(GTK_ENTRY (entry));
	tabData->file_name_length = strlen(tabData->file_name); /* Get length of string */

	if (tabData->bpressed[0] && tabData->bpressed[1] && tabData->bpressed[2]
			&& tabData->bpressed[3] && tabData->valueset[0]
			&& tabData->valueset[1] && tabData->valueset[2]
			&& tabData->valueset[3] && tabData->numpoints > 0
			&& tabData->file_name_length > 0) {
		gtk_widget_set_sensitive(tabData->exportbutton, TRUE);
	} else
		gtk_widget_set_sensitive(tabData->exportbutton, FALSE);

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
}

/****************************************************************/
/* This function removes the last inserted point or the point	*/
/* indexed by remthis (<0).					*/
/****************************************************************/
void removeLastPoint(GtkWidget *widget, gpointer data) {
	gint i;
	struct TabData *tabData;

	tabData = (struct TabData *) data;

	/* First redraw the drawing_area with the original image, to clean it. */

	if (tabData->numlastpoints > 0) { /* If points been put out, remove last one */
		tabData->numlastpoints--;
		for (i = 0; i < 4; i++)
			if (tabData->lastpoints[tabData->numlastpoints] == -(i + 1)) { /* If point to be removed is axispoint 1-4 */
				tabData->bpressed[i] = FALSE; /* Mark it unpressed.			*/
				gtk_widget_set_sensitive(tabData->xyentry[i], FALSE); /* Inactivate entry for point.		*/
				break;
			}
		if (i == 4)
			tabData->numpoints--; /* If its none of the X/Y markers then	*/
		setNumberOfPointsEntryValue(tabData->nump_entry, tabData->numpoints); /* its an ordinary marker, remove it.	 */
	}

	triggerUpdateDrawArea(tabData->drawing_area);

	setButtonSensitivity(tabData);
}

/****************************************************************/
/* This function sets the proper variables and then calls 	*/
/* remove_last, to remove all points except the axis points.	*/
/****************************************************************/
void removeAllPoints(GtkWidget *widget, gpointer data) {
	gint i, j, index;
	struct TabData *tabData;

	tabData = (struct TabData *) data;

	if (tabData->numlastpoints > 0 && tabData->numpoints > 0) {
		index = 0;
		for (i = 0; i < tabData->numlastpoints; i++)
			for (j = 0; j < 4; j++) { /* Search for axispoints and store them in */
				if (tabData->lastpoints[i] == -(j + 1)) { /* lastpoints at the first positions.      */
					tabData->lastpoints[index] = -(j + 1);
					index++;
				}
			}
		tabData->lastpoints[index] = 0;

		tabData->numlastpoints = index + 1;
		tabData->numpoints = 1;
		setNumberOfPointsEntryValue(tabData->nump_entry, tabData->numpoints);

		removeLastPoint(widget, data); /* Call remove_last() for housekeeping */
	} else if (tabData->numlastpoints > 0 && tabData->numpoints == 0) {
		tabData->numlastpoints = 0; /* Nullify amount of points */
		for (i = 0; i < 4; i++) {
			tabData->valueset[i] = FALSE;
			tabData->bpressed[i] = FALSE;
			gtk_entry_set_text((GtkEntry *) tabData->xyentry[i], "");
		}
		removeLastPoint(widget, data); /* Call remove_last() for housekeeping */
	}
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
			setMainImageZoom(tabData, tabData->viewZoom * MAIN_IMAGE_ZOOM_STEP,
					-1, -1);
		} else if (event->keyval == GDK_KEY_minus
				|| event->keyval == GDK_KEY_KP_Subtract) {
			setMainImageZoom(tabData, tabData->viewZoom / MAIN_IMAGE_ZOOM_STEP,
					-1, -1);
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
	G3DBG("addImageToTab: loaded '%s' image=%dx%d scale_arg=%.6f\n", filename,
			tabData->XSize, tabData->YSize, Scale);

	mScale = -1;
	if (maxX != -1 && maxY != -1) {
		if (tabData->XSize > maxX) {
			mScale = (double) maxX / tabData->XSize;
		}
		if (tabData->YSize > maxY && (double) maxY / tabData->YSize < mScale)
			mScale = (double) maxY / tabData->YSize;
	}

	if (Scale == -1 && mScale != -1)
		Scale = mScale;

	if (Scale != -1) {
		tabData->XSize *= Scale;
		tabData->YSize *= Scale;

		// flush to ensure all writing to the image was done
		cairo_surface_flush(tabData->image);

		cairo_t *cr;
		cr = cairo_create(tabData->image);

		cairo_surface_t *first;
		first = cairo_surface_create_similar(cairo_get_target(cr),
				CAIRO_CONTENT_COLOR, tabData->XSize, tabData->YSize);

		cairo_t *first_cr;
		first_cr = cairo_create(first);
		cairo_scale(first_cr, Scale, Scale);
		cairo_set_source_surface(first_cr, tabData->image, 0, 0);
		cairo_paint(first_cr);
		tabData->image = first;

		cairo_destroy(first_cr);
	}

	tabData->drawing_area = gtk_drawing_area_new(); /* Create new drawing area */
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

	tabData->pendingInitialZoomToFit = TRUE;
	g_idle_add(applyInitialZoomToFit, tabData);
	debugDumpViewportState("addImageToTab:scheduled_initial_fit", tabData);

	return 0;
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
	return (struct TabData *) malloc(sizeof(struct TabData));
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
	GtkWidget *nump_label, *ScrollWindow; /* Various widgets */
	GtkWidget *APlabel, *PIlabel, *ZAlabel, *Llabel, *tab_label;
	GtkWidget *alignment, *fixed;
	GtkWidget *x_label, *y_label, *tmplabel;
	GtkWidget *ordercheckb[3], *UseErrCheckB, *actioncheckb[2];
	GtkWidget *Olabel, *Elabel, *Alabel;
	GSList *group;
	GtkWidget *dialog;
	GtkWidget *pm_label, *pm_label2;
	GtkWidget *drawing_area_alignment;

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
		return -1;
	}

	g_object_set_data(G_OBJECT(table), DATA_STORE_NAME, (gpointer) tabData);

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
	if (UseError != NULL) {
		tabData->UseErrors = *UseError;
	} else {
		tabData->UseErrors = FALSE;
	}

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
	tabData->numlastpoints = 0;
	tabData->ordering = 0;

	tabData->mousePointerCoords[0] = -1.0;
	tabData->mousePointerCoords[1] = -1.0;
	tabData->viewZoom = 1.0;
	tabData->viewOrigin[0] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewOrigin[1] = MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[0] = 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->viewCanvasSize[1] = 2.0 * MAIN_IMAGE_CANVAS_MIN_PAD;
	tabData->XSize = 0;
	tabData->YSize = 0;

	tabData->logxy[0] = FALSE;
	tabData->logxy[1] = FALSE;

	tabData->MaxPoints = MAXPOINTS;

	tabData->setxypressed[0] = FALSE;
	tabData->setxypressed[1] = FALSE;
	tabData->setxypressed[2] = FALSE;
	tabData->setxypressed[3] = FALSE;

	tabData->lastpoints = NULL;

	tabData->movedPointIndex = NONESELECTED;
	tabData->middlePanning = FALSE;
	tabData->middlePanMoved = FALSE;
	tabData->pendingInitialZoomToFit = FALSE;
	tabData->pendingRecenterOnAdjust = FALSE;

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

	tabData->remlastbutton = gtk_button_new_with_mnemonic(RemLastBLabel); /* Create button */
	g_signal_connect(G_OBJECT (tabData->remlastbutton), "clicked", /* Connect button */
	G_CALLBACK (removeLastPoint), tabData);
	gtk_widget_set_sensitive(tabData->remlastbutton, FALSE);
	gtk_widget_set_tooltip_text(tabData->remlastbutton, removeltt);

	tabData->remallbutton = gtk_button_new_with_mnemonic(RemAllBLabel); /* Create button */
	g_signal_connect(G_OBJECT (tabData->remallbutton), "clicked", /* Connect button */
	G_CALLBACK (removeAllPoints), tabData);
	gtk_widget_set_sensitive(tabData->remallbutton, FALSE);
	gtk_widget_set_tooltip_text(tabData->remallbutton, removeatts);

	for (i = 0; i < 2; i++) {
		logcheckb[i] = gtk_check_button_new_with_mnemonic(loglabel[i]); /* Create check button */
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
	gtk_box_pack_start(GTK_BOX (bottomhbox), blvbox, FALSE, FALSE, ELEM_SEP);

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX (blvbox), subvbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (subvbox), tabData->remlastbutton, FALSE, FALSE,
			0); /* Pack button in vert. box */
	gtk_box_pack_start(GTK_BOX (subvbox), tabData->remallbutton, FALSE, FALSE,
			0); /* Pack button in vert. box */

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

	group = NULL;
	for (i = 0; i < ORDERBNUM; i++) {
		ordercheckb[i] = gtk_radio_button_new_with_label(group, orderlabel[i]); /* Create radio button */
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (ordercheckb[i]), "toggled", /* Connect button */
		G_CALLBACK (setOutputOrdering), buttonData);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON (ordercheckb[i])); /* Get buttons group */
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (ordercheckb[0]), TRUE); /* Set no ordering button active */

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	tabData->oppropbox = subvbox;
	gtk_box_pack_start(GTK_BOX (blvbox), subvbox, FALSE, FALSE, 0);
	Olabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (Olabel), Oheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, Olabel);
	gtk_box_pack_start(GTK_BOX (subvbox), alignment, FALSE, FALSE, 0);
	for (i = 0; i < ORDERBNUM; i++) {
		fixed = gtk_fixed_new();
		gtk_fixed_put((GtkFixed *) fixed, ordercheckb[i], FRAME_INDENT, 0);
		gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0); /* Pack radiobutton in vert. box */
	}

	UseErrCheckB = gtk_check_button_new_with_mnemonic(PrintErrCBLabel);
	g_signal_connect(G_OBJECT (UseErrCheckB), "toggled",
			G_CALLBACK (setPrintErrorUsage), tabData);
	gtk_widget_set_tooltip_text(UseErrCheckB, uetts);
	gtk_toggle_button_set_active((GtkToggleButton *) UseErrCheckB,
			tabData->UseErrors);

	Elabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (Elabel), Eheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, Elabel);
	gtk_box_pack_start(GTK_BOX (subvbox), alignment, FALSE, FALSE, 0);
	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, UseErrCheckB, FRAME_INDENT, 0);
	gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0);

	subvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ELEM_SEP);
	gtk_box_pack_start(GTK_BOX (blvbox), subvbox, FALSE, FALSE, 0);
	group = NULL;
	for (i = 0; i < ACTIONBNUM; i++) {
		actioncheckb[i] = gtk_radio_button_new_with_label(group,
				actionlabel[i]); /* Create radio button */
		struct ButtonData *buttonData;
		buttonData = malloc(sizeof(struct ButtonData));
		buttonData->tabData = tabData;
		buttonData->index = i;
		g_signal_connect(G_OBJECT (actioncheckb[i]), "toggled", /* Connect button */
		G_CALLBACK (setOutputAction), buttonData);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON (actioncheckb[i])); /* Get buttons group */
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (actioncheckb[0]), TRUE); /* Set no ordering button active */

	Alabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL (Alabel), Aheader);
	alignment = g3AlignmentNew(0, 1, 0, 0);
	gtk_container_add((GtkContainer *) alignment, Alabel);
	gtk_box_pack_start(GTK_BOX (subvbox), alignment, FALSE, FALSE, 0);
	for (i = 0; i < ACTIONBNUM; i++) {
		fixed = gtk_fixed_new();
		gtk_fixed_put((GtkFixed *) fixed, actioncheckb[i], FRAME_INDENT, 0);
		gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0);
	}

	tabData->file_entry = gtk_entry_new(); /* Create text entry */
	gtk_entry_set_max_length(GTK_ENTRY (tabData->file_entry), 256);
	gtk_editable_set_editable((GtkEditable *) tabData->file_entry, TRUE);
	g_signal_connect(G_OBJECT (tabData->file_entry), "changed", /* Init the entry to call */
	G_CALLBACK (readFileEntry), tabData);
	gtk_widget_set_tooltip_text(tabData->file_entry, filenamett);

	if (FileInCwd) {
		snprintf(buf2, 256, "%s.dat", basename(filename));
		strncpy(tabData->FileNames, basename(filename), 256);
	} else {
		snprintf(buf2, 256, "%s.dat", filename);
		strncpy(tabData->FileNames, filename, 256);
	}

	snprintf(buf, 256, Window_Title, tabData->FileNames); /* Print window title in buffer */
	gtk_window_set_title(GTK_WINDOW (window), buf); /* Set window title */

	fixed = gtk_fixed_new();
	gtk_fixed_put((GtkFixed *) fixed, tabData->file_entry, FRAME_INDENT, 0);
	gtk_box_pack_start(GTK_BOX (subvbox), fixed, FALSE, FALSE, 0);
	gtk_widget_set_sensitive(tabData->file_entry, FALSE);

	tabData->exportbutton = gtk_button_new_with_mnemonic(PrintBLabel); /* Create button */

	gtk_box_pack_start(GTK_BOX (subvbox), tabData->exportbutton, FALSE, FALSE,
			0);
	gtk_widget_set_sensitive(tabData->exportbutton, FALSE);

	g_signal_connect(G_OBJECT (tabData->exportbutton), "clicked",
			G_CALLBACK (outputResultset), tabData);
	gtk_widget_set_tooltip_text(tabData->exportbutton, printrestt);

	brvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, GROUP_SEP);
	gtk_box_pack_start(GTK_BOX (bottomhbox), brvbox, TRUE, TRUE, 0);

	gtk_entry_set_text(GTK_ENTRY (tabData->file_entry), buf2); /* Set text of text entry to filename */

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

	gtk_widget_show_all(window);

	gtk_notebook_set_current_page((GtkNotebook *) mainnotebook, TabNum);

	if (addImageToTab(drawing_area_alignment, filename, Scale, maxX, maxY,
			tabData) == -1) {
		gtk_notebook_remove_page((GtkNotebook *) mainnotebook, TabNum);
		return -1;
	}

	if (UsePreSetCoords) {
		allocatePointDataMemory(tabData);
		tabData->axiscoords[0][0] = 0;
		tabData->axiscoords[0][1] = tabData->YSize - 1;
		tabData->axiscoords[1][0] = tabData->XSize - 1;
		tabData->axiscoords[1][1] = tabData->YSize - 1;
		tabData->axiscoords[2][0] = 0;
		tabData->axiscoords[2][1] = tabData->YSize - 1;
		tabData->axiscoords[3][0] = 0;
		tabData->axiscoords[3][1] = 0;
		for (i = 0; i < 4; i++) {
			gtk_widget_set_sensitive(tabData->xyentry[i], TRUE);
			gtk_editable_set_editable((GtkEditable *) tabData->xyentry[i],
					TRUE);
			sprintf(buf, "%lf", tabData->realcoords[i]);
			gtk_entry_set_text((GtkEntry *) tabData->xyentry[i], buf);
			tabData->lastpoints[tabData->numlastpoints] = -(i + 1);
			tabData->numlastpoints++;
			tabData->valueset[i] = TRUE;
			tabData->bpressed[i] = TRUE;
			tabData->setxypressed[i] = FALSE;
		}
		gtk_widget_set_sensitive(tabData->exportbutton, TRUE);
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
	if (HideOpProp)
		if (tabData->oppropbox != NULL
		)
			gtk_widget_hide(tabData->oppropbox);

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
	gint i;
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

	//	printf("Freeing memory for tab %s\n", tabData->FileNames);

	if (tabData->lastpoints != NULL) {
		for (i = 0; i < tabData->MaxPoints; i++) {
			free(tabData->points[i]);
		}
		free(tabData->points);
		free(tabData->lastpoints);
	}
	free(tabData);

	if (countDataTabs() == 0)
		gtk_widget_set_sensitive(close_menu_item, FALSE);
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

void zoomView100(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		setMainImageZoom(tabData, 1.0, -1.0, -1.0);
}

void zoomView200(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		setMainImageZoom(tabData, 2.0, -1.0, -1.0);
}

void zoomViewToFit(GtkWidget *widget, gpointer data) {
	struct TabData *tabData;
	(void) widget;
	(void) data;
	tabData = getCurrentTabData();
	if (tabData != NULL)
		zoomToFitAndCenter(tabData);
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
/* This callback handles the hide output properties toggling.	*/
/****************************************************************/
GCallback hideOutputProperties(GtkWidget *widget, gpointer func_data) {
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
			gtk_widget_hide(tabData->oppropbox);
		} else {
			gtk_widget_show(tabData->oppropbox);
		}
	}
	HideOpProp = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

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
	} else {
		gtk_window_set_title(GTK_WINDOW(window), Window_Title_NoneOpen);
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
	GtkWidget *file_menu, *view_menu, *help_menu;
	GtkWidget *file_root_item, *view_root_item, *help_root_item;
	GtkWidget *open_item, *quit_item, *about_item;
	GtkWidget *zoom_area_item, *axis_settings_item, *output_properties_item;
	GtkWidget *fullscreen_item;
	GtkWidget *zoom100_item, *zoom200_item, *zoomfit_item;
	GtkWidget *separator_item, *separator_item2;
	GtkAccelGroup *accel_group;

	gtk_init(&argc, &argv); /* Init GTK */

	if (argc > 1)
		if (strcmp(argv[1], "-h") == 0 || /* If no parameters given, -h or --help */
		strcmp(argv[1], "--help") == 0) {
			printf("%s", HelpText); /* Print help */
			exit(0); /* and exit */
		}

	maxX = -1;
	maxY = -1;
	Scale = -1;
	UseError = FALSE;
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
				/*	    } else if (strcmp(argv[i],"-hidelog")==0) {
				 HideLog = TRUE;
				 } else if (strcmp(argv[i],"-hideza")==0) {
				 HideZoomArea = TRUE;
				 } else if (strcmp(argv[i],"-hideop")==0) {
				 HideOpProp = TRUE; */
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
	view_menu = gtk_menu_new();
	help_menu = gtk_menu_new();
	file_menu_widget = file_menu;
	recent_menu_items = g_ptr_array_new();

	file_root_item = gtk_menu_item_new_with_mnemonic("_File");
	view_root_item = gtk_menu_item_new_with_mnemonic("_View");
	help_root_item = gtk_menu_item_new_with_mnemonic("_Help");

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_root_item), file_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_root_item), view_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_root_item), help_menu);

	open_item = gtk_menu_item_new_with_mnemonic("_Open");
	close_menu_item = gtk_menu_item_new_with_mnemonic("_Close");
	quit_item = gtk_menu_item_new_with_mnemonic("_Quit");
	gtk_widget_set_sensitive(close_menu_item, FALSE);

	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_item);
	separator_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), close_menu_item);
	separator_item2 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), separator_item2);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);

	loadRecentFiles();
	rebuildRecentFileMenu();

	zoom_area_item = gtk_check_menu_item_new_with_label("Zoom area");
	axis_settings_item = gtk_check_menu_item_new_with_label("Axis settings");
	output_properties_item =
			gtk_check_menu_item_new_with_label("Output properties");
	fullscreen_item = gtk_check_menu_item_new_with_mnemonic("_Full Screen");
	zoom100_item = gtk_menu_item_new_with_label("Zoom 100%");
	zoom200_item = gtk_menu_item_new_with_label("Zoom 200%");
	zoomfit_item = gtk_menu_item_new_with_label("Zoom to fit");

	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom_area_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), axis_settings_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), output_properties_item);
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
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view_root_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_root_item);

	g_signal_connect(G_OBJECT(open_item), "activate", G_CALLBACK(menuFileOpen),
			NULL);
	g_signal_connect(G_OBJECT(close_menu_item), "activate",
			G_CALLBACK(menuTabClose), NULL);
	g_signal_connect(G_OBJECT(quit_item), "activate", G_CALLBACK(menuFileExit),
			NULL);
	g_signal_connect(G_OBJECT(about_item), "activate", G_CALLBACK(menuHelpAbout),
			NULL);

	g_signal_connect(G_OBJECT(zoom_area_item), "toggled",
			G_CALLBACK(hideZoomArea), NULL);
	g_signal_connect(G_OBJECT(axis_settings_item), "toggled",
			G_CALLBACK(hideAxisSettings), NULL);
	g_signal_connect(G_OBJECT(output_properties_item), "toggled",
			G_CALLBACK(hideOutputProperties), NULL);
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
	gtk_widget_add_accelerator(output_properties_item, "activate", accel_group,
			GDK_KEY_F7, 0, GTK_ACCEL_VISIBLE);
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

	return (0); /* Exit. */
}
