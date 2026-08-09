#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"

static const guint32 SERIES_PALETTE[] = {
	0xd62728ffu, 0x1f77b4ffu, 0x2ca02cffu, 0xff7f0effu,
	0x9467bdffu, 0x17becfffu, 0x8c564bffu, 0xe377c2ffu
};

SamplePoint *sample_point_new(gdouble x, gdouble y, gint64 sample_order) {
	SamplePoint *point;

	point = g_new0(SamplePoint, 1);
	point->source_x_px = x;
	point->source_y_px = y;
	point->sample_order = sample_order;
	return point;
}

void sample_point_free(gpointer data) {
	g_free(data);
}

DataSeries *data_series_new(const gchar *label, guint32 marker_rgba,
		gint display_order) {
	DataSeries *series;

	series = g_new0(DataSeries, 1);
	series->label = g_strdup(label != NULL ? label : "Series");
	series->marker_rgba = marker_rgba;
	series->visible = TRUE;
	series->display_order = display_order;
	series->points = g_ptr_array_new_with_free_func(sample_point_free);
	return series;
}

void data_series_free(gpointer data) {
	DataSeries *series;

	series = (DataSeries *) data;
	if (series == NULL)
		return;
	g_free(series->label);
	g_ptr_array_free(series->points, TRUE);
	g_free(series);
}

SamplePoint *data_series_add_point(DataSeries *series, gdouble x, gdouble y) {
	SamplePoint *point;
	gint64 next_order;

	if (series == NULL)
		return NULL;
	next_order = 0;
	if (series->points->len > 0) {
		SamplePoint *last;
		last = g_ptr_array_index(series->points, series->points->len - 1);
		next_order = last->sample_order + 1;
	}
	point = sample_point_new(x, y, next_order);
	g_ptr_array_add(series->points, point);
	return point;
}

void data_series_insert_point(DataSeries *series, SamplePoint *point,
		guint index) {
	if (series == NULL || point == NULL)
		return;
	if (index > series->points->len)
		index = series->points->len;
	g_ptr_array_insert(series->points, index, point);
}

SamplePoint *data_series_steal_point(DataSeries *series, guint index) {
	if (series == NULL || index >= series->points->len)
		return NULL;
	return g_ptr_array_steal_index(series->points, index);
}

gint data_series_index_of_point(const DataSeries *series,
		const SamplePoint *point) {
	guint i;

	if (series == NULL || point == NULL)
		return -1;
	for (i = 0; i < series->points->len; i++)
		if (g_ptr_array_index(series->points, i) == point)
			return (gint) i;
	return -1;
}

gboolean data_series_remove_point(DataSeries *series, SamplePoint *point) {
	gint index;

	index = data_series_index_of_point(series, point);
	if (index < 0)
		return FALSE;
	g_ptr_array_remove_index(series->points, (guint) index);
	return TRUE;
}

ImageDocument *image_document_new(gint64 image_id) {
	ImageDocument *document;

	document = g_new0(ImageDocument, 1);
	document->image_id = image_id;
	document->series = g_ptr_array_new_with_free_func(data_series_free);
	document->selected_points = g_ptr_array_new();
	return document;
}

void image_document_free(ImageDocument *document) {
	if (document == NULL)
		return;
	g_ptr_array_free(document->selected_points, TRUE);
	g_ptr_array_free(document->series, TRUE);
	g_free(document);
}

DataSeries *image_document_add_series(ImageDocument *document,
		const gchar *label, guint32 marker_rgba) {
	DataSeries *series;

	if (document == NULL)
		return NULL;
	series = data_series_new(label, marker_rgba, document->series->len);
	g_ptr_array_add(document->series, series);
	if (document->active_series == NULL)
		document->active_series = series;
	return series;
}

void image_document_insert_series(ImageDocument *document, DataSeries *series,
		guint index) {
	if (document == NULL || series == NULL)
		return;
	if (index > document->series->len)
		index = document->series->len;
	g_ptr_array_insert(document->series, index, series);
	if (document->active_series == NULL)
		document->active_series = series;
}

DataSeries *image_document_steal_series(ImageDocument *document, guint index) {
	DataSeries *series;

	if (document == NULL || index >= document->series->len)
		return NULL;
	series = g_ptr_array_index(document->series, index);
	if (document->selected_series == series || document->hovered_series == series)
		image_document_clear_selection(document);
	if (document->active_series == series)
		document->active_series = NULL;
	return g_ptr_array_steal_index(document->series, index);
}

gint image_document_index_of_series(const ImageDocument *document,
		const DataSeries *series) {
	guint i;

	if (document == NULL || series == NULL)
		return -1;
	for (i = 0; i < document->series->len; i++)
		if (g_ptr_array_index(document->series, i) == series)
			return (gint) i;
	return -1;
}

void image_document_clear_selection(ImageDocument *document) {
	if (document == NULL)
		return;
	g_ptr_array_set_size(document->selected_points, 0);
	document->selected_point = NULL;
	document->selected_series = NULL;
	document->hovered_point = NULL;
	document->hovered_series = NULL;
}

guint image_document_selection_count(const ImageDocument *document) {
	if (document == NULL || document->selected_points == NULL)
		return 0;
	return document->selected_points->len;
}

gboolean image_document_point_is_selected(const ImageDocument *document,
		const SamplePoint *point) {
	guint i;

	if (document == NULL || document->selected_points == NULL || point == NULL)
		return FALSE;
	for (i = 0; i < document->selected_points->len; i++)
		if (g_ptr_array_index(document->selected_points, i) == point)
			return TRUE;
	return FALSE;
}

void image_document_select_point(ImageDocument *document, DataSeries *series,
		SamplePoint *point, gboolean extend_selection) {
	guint i;

	if (document == NULL)
		return;
	if (!extend_selection || series != document->selected_series)
		image_document_clear_selection(document);
	if (point == NULL)
		return;
	if (extend_selection && image_document_point_is_selected(document, point)) {
		for (i = 0; i < document->selected_points->len; i++) {
			if (g_ptr_array_index(document->selected_points, i) == point) {
				g_ptr_array_remove_index(document->selected_points, i);
				break;
			}
		}
		if (document->selected_points->len == 0) {
			document->selected_point = NULL;
			document->selected_series = NULL;
		} else {
			document->selected_point = g_ptr_array_index(document->selected_points,
					document->selected_points->len - 1);
		}
		return;
	}
	document->selected_series = series;
	document->selected_point = point;
	g_ptr_array_add(document->selected_points, point);
}

void image_document_set_active_series(ImageDocument *document,
		DataSeries *series) {
	if (document == NULL || image_document_index_of_series(document, series) < 0)
		return;
	document->active_series = series;
	if (document->selected_series != series)
		image_document_clear_selection(document);
}

gboolean image_document_remove_series(ImageDocument *document,
		DataSeries *series) {
	gint index;

	if (document == NULL || document->series->len <= 1)
		return FALSE;
	index = image_document_index_of_series(document, series);
	if (index < 0)
		return FALSE;
	if (document->selected_series == series || document->hovered_series == series)
		image_document_clear_selection(document);
	if (document->active_series == series) {
		if ((guint) index + 1 < document->series->len)
			document->active_series = g_ptr_array_index(document->series, index + 1);
		else
			document->active_series = g_ptr_array_index(document->series, index - 1);
	}
	g_ptr_array_remove_index(document->series, (guint) index);
	return TRUE;
}

guint32 image_document_next_color(const ImageDocument *document) {
	guint index;

	index = document != NULL ? document->series->len : 0;
	return SERIES_PALETTE[index % G_N_ELEMENTS(SERIES_PALETTE)];
}

gchar *image_document_next_series_label(const ImageDocument *document) {
	guint candidate, i;
	gboolean used;
	gchar *label;

	for (candidate = 1; ; candidate++) {
		label = g_strdup_printf("Series %u", candidate);
		used = FALSE;
		if (document != NULL) {
			for (i = 0; i < document->series->len; i++) {
				DataSeries *series = g_ptr_array_index(document->series, i);
				if (g_strcmp0(series->label, label) == 0) {
					used = TRUE;
					break;
				}
			}
		}
		if (!used)
			return label;
		g_free(label);
	}
}

void calibration_state_clear(CalibrationState *calibration) {
	if (calibration != NULL) {
		memset(calibration, 0, sizeof(*calibration));
		calibration->positioning_circle_diameter =
				G3_DEFAULT_POSITIONING_CIRCLE_DIAMETER;
	}
}

void rgba_to_components(guint32 rgba, gdouble *red, gdouble *green,
		gdouble *blue, gdouble *alpha) {
	if (red != NULL)
		*red = ((rgba >> 24) & 0xff) / 255.0;
	if (green != NULL)
		*green = ((rgba >> 16) & 0xff) / 255.0;
	if (blue != NULL)
		*blue = ((rgba >> 8) & 0xff) / 255.0;
	if (alpha != NULL)
		*alpha = (rgba & 0xff) / 255.0;
}

static guint8 component_to_byte(gdouble component) {
	component = CLAMP(component, 0.0, 1.0);
	return (guint8) floor(component * 255.0 + 0.5);
}

guint32 rgba_from_components(gdouble red, gdouble green, gdouble blue,
		gdouble alpha) {
	return ((guint32) component_to_byte(red) << 24)
			| ((guint32) component_to_byte(green) << 16)
			| ((guint32) component_to_byte(blue) << 8)
			| component_to_byte(alpha);
}

gchar *rgba_to_string(guint32 rgba) {
	return g_strdup_printf("#%08X", rgba);
}

gboolean rgba_from_string(const gchar *text, guint32 *rgba) {
	gchar *end;
	guint64 value;

	if (text == NULL || strlen(text) != 9 || text[0] != '#')
		return FALSE;
	value = g_ascii_strtoull(text + 1, &end, 16);
	if (*end != '\0' || value > G_MAXUINT32)
		return FALSE;
	if (rgba != NULL)
		*rgba = (guint32) value;
	return TRUE;
}
