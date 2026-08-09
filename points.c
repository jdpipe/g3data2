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

#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include "main.h"

/* Extern functions */

extern void orderPoints(struct PointValue *RealPos, gint left, gint right,
		gint ordering);

/****************************************************************/
/* This function returns the integer with the lesser value.	*/
/****************************************************************/
gint min(gint x, gint y) {
	if (x < y)
		return x;
	else
		return y;
}

/****************************************************************/
/* This function calculates the true value of the point based	*/
/* on the coordinates of the point on the bitmap.		*/
/****************************************************************/
gboolean calculateAxisPosition(gdouble x, gdouble y,
		const struct TabData *tabData, gdouble *x_fraction,
		gdouble *y_fraction) {
	gdouble ux, uy, vx, vy, determinant, axis_scale;
	gdouble px, py;

	ux = tabData->axiscoords[1][0] - tabData->axiscoords[0][0];
	uy = tabData->axiscoords[1][1] - tabData->axiscoords[0][1];
	vx = tabData->axiscoords[3][0] - tabData->axiscoords[2][0];
	vy = tabData->axiscoords[3][1] - tabData->axiscoords[2][1];
	determinant = ux * vy - uy * vx;
	axis_scale = hypot(ux, uy) * hypot(vx, vy);
	if (axis_scale == 0.0 || fabs(determinant) <= 1e-12 * axis_scale)
		return FALSE;

	px = x - tabData->axiscoords[0][0];
	py = y - tabData->axiscoords[0][1];
	*x_fraction = (px * vy - py * vx) / determinant;
	px = x - tabData->axiscoords[2][0];
	py = y - tabData->axiscoords[2][1];
	*y_fraction = (ux * py - uy * px) / determinant;
	return TRUE;
}

gboolean calculateAxisGuides(gdouble x, gdouble y,
		const struct TabData *tabData, gdouble x_axis_intersection[2],
		gdouble y_axis_intersection[2]) {
	gdouble x_fraction, y_fraction;
	gdouble ux, uy, vx, vy;

	if (!calculateAxisPosition(x, y, tabData, &x_fraction, &y_fraction))
		return FALSE;
	ux = tabData->axiscoords[1][0] - tabData->axiscoords[0][0];
	uy = tabData->axiscoords[1][1] - tabData->axiscoords[0][1];
	vx = tabData->axiscoords[3][0] - tabData->axiscoords[2][0];
	vy = tabData->axiscoords[3][1] - tabData->axiscoords[2][1];
	x_axis_intersection[0] = tabData->axiscoords[0][0] + x_fraction * ux;
	x_axis_intersection[1] = tabData->axiscoords[0][1] + x_fraction * uy;
	y_axis_intersection[0] = tabData->axiscoords[2][0] + y_fraction * vx;
	y_axis_intersection[1] = tabData->axiscoords[2][1] + y_fraction * vy;
	return TRUE;
}

struct PointValue calculatePointValue(gdouble Xpos, gdouble Ypos,
		struct TabData *tabData) {
	double x_fraction, y_fraction, rlc[4];
	double x_plus, y_plus, x_minus, y_minus;
	struct PointValue pointValue;

	pointValue.Xv = pointValue.Yv = NAN;
	pointValue.Xerr = pointValue.Yerr = NAN;
	if (!calculateAxisPosition(Xpos, Ypos, tabData, &x_fraction,
			&y_fraction))
		return pointValue;

	if (tabData->logxy[0]) { /* If x axis is logarithmic, store	*/
		rlc[0] = log(tabData->realcoords[0]); /* recalculated values in rlc.		*/
		rlc[1] = log(tabData->realcoords[1]);
	} else {
		rlc[0] = tabData->realcoords[0]; /* Else store old values in rlc.	*/
		rlc[1] = tabData->realcoords[1];
	}

	if (tabData->logxy[1]) {
		rlc[2] = log(tabData->realcoords[2]); /* If y axis is logarithmic, store      */
		rlc[3] = log(tabData->realcoords[3]); /* recalculated values in rlc.          */
	} else {
		rlc[2] = tabData->realcoords[2]; /* Else store old values in rlc.        */
		rlc[3] = tabData->realcoords[3];
	}

	if (tabData->logxy[0])
		pointValue.Xv = exp(x_fraction * (rlc[1] - rlc[0]) + rlc[0]);
	else
		pointValue.Xv = x_fraction * (rlc[1] - rlc[0]) + rlc[0];

	if (tabData->logxy[1])
		pointValue.Yv = exp(y_fraction * (rlc[3] - rlc[2]) + rlc[2]);
	else
		pointValue.Yv = y_fraction * (rlc[3] - rlc[2]) + rlc[2];

	if (!calculateAxisPosition(Xpos + 1.0, Ypos + 1.0, tabData,
			&x_plus, &y_plus)
			|| !calculateAxisPosition(Xpos - 1.0, Ypos - 1.0, tabData,
					&x_minus, &y_minus))
		return pointValue;

	if (tabData->logxy[0])
		pointValue.Xerr = exp(x_plus * (rlc[1] - rlc[0]) + rlc[0]);
	else
		pointValue.Xerr = x_plus * (rlc[1] - rlc[0]) + rlc[0];

	if (tabData->logxy[1])
		pointValue.Yerr = exp(y_plus * (rlc[3] - rlc[2]) + rlc[2]);
	else
		pointValue.Yerr = y_plus * (rlc[3] - rlc[2]) + rlc[2];

	if (tabData->logxy[0])
		pointValue.Xerr -= exp(x_minus * (rlc[1] - rlc[0]) + rlc[0]);
	else
		pointValue.Xerr -= x_minus * (rlc[1] - rlc[0]) + rlc[0];

	if (tabData->logxy[1])
		pointValue.Yerr -= exp(y_minus * (rlc[3] - rlc[2]) + rlc[2]);
	else
		pointValue.Yerr -= y_minus * (rlc[3] - rlc[2]) + rlc[2];

	pointValue.Xerr = fabs(pointValue.Xerr / 4.0);
	pointValue.Yerr = fabs(pointValue.Yerr / 4.0);

	return pointValue;
}

static gboolean appendSeries(GString *output, struct TabData *tabData,
		DataSeries *series, gboolean include_label, gboolean tab_separated) {
	gint i;
	gint point_count;
	struct PointValue *realPositions, calculatedValue;

	if (series == NULL || series->points->len == 0)
		return FALSE;
	point_count = (gint) series->points->len;

	realPositions = (struct PointValue *) malloc(
			sizeof(struct PointValue) * point_count);
	if (realPositions == NULL)
		return FALSE;

	/* Next up is recalculating the positions of the points by solving a 2*2 matrix */

	for (i = 0; i < point_count; i++) {
		SamplePoint *point;
		point = g_ptr_array_index(series->points, i);
		calculatedValue = calculatePointValue(point->source_x_px,
				point->source_y_px, tabData);
		realPositions[i].Xv = calculatedValue.Xv;
		realPositions[i].Yv = calculatedValue.Yv;
		realPositions[i].Xerr = calculatedValue.Xerr;
		realPositions[i].Yerr = calculatedValue.Yerr;
	}

	if (tabData->ordering != 0) {
		orderPoints(realPositions, 0, point_count - 1, tabData->ordering);
	}

	if (include_label) {
		gchar *safe_label;
		safe_label = g_strdup(series->label);
		g_strdelimit(safe_label, "\r\n", ' ');
		g_string_append_printf(output, "# %s\n", safe_label);
		g_free(safe_label);
	}

	for (i = 0; i < point_count; i++) {
		if (tab_separated) {
			g_string_append_printf(output, "%.12g\t%.12g", realPositions[i].Xv,
					realPositions[i].Yv);
			if (tabData->UseErrors)
				g_string_append_printf(output, "\t%.12g\t%.12g",
						realPositions[i].Xerr, realPositions[i].Yerr);
		} else {
			g_string_append_printf(output, "%.12g  %.12g", realPositions[i].Xv,
					realPositions[i].Yv);
			if (tabData->UseErrors)
				g_string_append_printf(output, "\t%.12g  %.12g",
						realPositions[i].Xerr, realPositions[i].Yerr);
		}
		g_string_append_c(output, '\n');
	}
	free(realPositions);
	return TRUE;
}

GString *formatResultset(struct TabData *tabData, gboolean all_series,
		gboolean tab_separated) {
	GString *output;
	gboolean appended;
	guint i;

	if (tabData == NULL || tabData->document == NULL)
		return NULL;
	output = g_string_new(NULL);
	appended = FALSE;
	if (all_series) {
		for (i = 0; i < tabData->document->series->len; i++) {
			DataSeries *series;
			series = g_ptr_array_index(tabData->document->series, i);
			if (appendSeries(output, tabData, series, TRUE, tab_separated))
				appended = TRUE;
		}
	} else {
		appended = appendSeries(output, tabData,
				tabData->document->active_series, FALSE, tab_separated);
	}
	if (!appended) {
		g_string_free(output, TRUE);
		return NULL;
	}
	return output;
}
