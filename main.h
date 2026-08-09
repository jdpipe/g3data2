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
#include <gtk/gtk.h>					/* Include gtk library */
#include "model.h"
#include "history.h"

#define VERSION "1.0.0"					/* Version number */

#define ZOOMPIXSIZE 200					/* Size of zoom in window */
#define ZOOMFACTOR 4					/* Zoom factor of zoom window */
#define MARKERSIZE 3					/* Size of point marker red outer square */
#define MARKERLENGTH 6					/* Axis marker length */
#define MARKERTHICKNESS 2				/* Marker line thickness */
#define MAXNUMFILES 256
#define GRABTRESHOLD MARKERSIZE*2

#define ORDERBNUM 3
#define LOGBNUM 2

#define URI_IDENTIFIER "file://"

struct TabData;

struct PointValue {
	double Xv, Yv, Xerr, Yerr;
};

struct PointValue calculatePointValue(gdouble x, gdouble y,
		struct TabData *tabData);
gboolean calculateAxisPosition(gdouble x, gdouble y,
		const struct TabData *tabData, gdouble *x_fraction,
		gdouble *y_fraction);
gboolean calculateAxisGuides(gdouble x, gdouble y,
		const struct TabData *tabData, gdouble x_axis_intersection[2],
		gdouble y_axis_intersection[2]);
GString *formatResultset(struct TabData *tabData, gboolean all_series,
		gboolean tab_separated);

typedef enum {
	URI_LIST, DROP_TARGET_NUM_DEFS
} UI_DROP_TARGET_INFO;

struct TabData {
	GtkWidget *drawing_area;
	GtkWidget *zoom_area; 					// Drawing areas
	GtkWidget *xyentry[4];
	GtkWidget *setxybutton[4];
	GtkWidget *logcheckbutton[2];
	GtkWidget *logbox;
	GtkWidget *zoomareabox;
	GtkWidget *ViewPort;
	GtkWidget *series_view;
	GtkListStore *series_store;
	GtkWidget *series_color_button;
	GtkWidget *series_visible_check;
	GtkWidget *selected_point_label;
	GtkWidget *calibration_status_label;
	GtkWidget *calibration_cancel_button;

	cairo_surface_t *image;

	gdouble axiscoords[4][2]; 				// X,Y coordinates of axispoints
	gint numpoints;
	gint ordering; 							// Various control variables
	gint XSize, YSize;
	gint sourceXSize, sourceYSize;
	gdouble imageScale;
	gdouble positioningCircleDiameter;
	gdouble realcoords[4]; 					// X,Y coords on graph
	gboolean UseErrors;
	gboolean setxypressed[4];
	gboolean bpressed[4]; 					// What axispoints have been set out ?
	gboolean valueset[4];
	gboolean logxy[2]; // = { FALSE, FALSE };
	gchar FileNames[256];

	gdouble mousePointerCoords[2];
	gdouble viewZoom;
	gdouble viewOrigin[2];
	gdouble viewCanvasSize[2];

	gdouble movedOrigCoords[2];
	gdouble movedOrigMousePtrCoords[2];

	gboolean middlePanning;
	gboolean middlePanMoved;
	gdouble middlePanStartMouse[2];
	gdouble middlePanStartAdj[2];
	gboolean zoomedToFit;
	gint fittedViewportWidth;
	gint fittedViewportHeight;
	gboolean pendingRecenterOnAdjust;
	gboolean pendingZoomScrollOnAdjust;
	gdouble pendingZoomScrollTarget[2];
	gdouble pendingZoomScrollCanvasSize[2];

	ImageDocument *document;
	History *history;
	CalibrationState committedCalibration;
	gboolean axisWorkflowDismissed;
	gboolean loadingStore;
	SamplePoint *movedPoint;
	DataSeries *movedSeries;
	gboolean leftPressPending;
	gdouble leftPressWidget[2];
	gboolean marqueeActive;
	gdouble marqueeStart[2];
	gdouble marqueeEnd[2];
};

struct ButtonData {
	struct TabData *tabData;
	gint index;
};

struct GtkSelectionData_x {
  GdkAtom       selection;
  GdkAtom       target;
  GdkAtom       type;
  gint          format;
  guchar       *data;
  gint          length;
  GdkDisplay   *display;
};
