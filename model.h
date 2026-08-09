#ifndef G3DATA2_MODEL_H
#define G3DATA2_MODEL_H

#include <glib.h>

#define G3_AXIS_POINT_COUNT 4
#define G3_COLOR_RED 0xd62728ffu

typedef struct SamplePoint {
	gint64 id;
	gdouble source_x_px;
	gdouble source_y_px;
	gint64 sample_order;
} SamplePoint;

typedef struct DataSeries {
	gint64 id;
	gchar *label;
	guint32 marker_rgba;
	gboolean visible;
	gint display_order;
	GPtrArray *points;
} DataSeries;

typedef struct CalibrationState {
	gdouble axis_x[G3_AXIS_POINT_COUNT];
	gdouble axis_y[G3_AXIS_POINT_COUNT];
	gdouble axis_value[G3_AXIS_POINT_COUNT];
	gboolean position_set[G3_AXIS_POINT_COUNT];
	gboolean value_set[G3_AXIS_POINT_COUNT];
	gboolean log_axis[2];
} CalibrationState;

typedef struct ImageDocument {
	gint64 image_id;
	GPtrArray *series;
	DataSeries *active_series;
	GPtrArray *selected_points;
	SamplePoint *selected_point;
	DataSeries *selected_series;
	SamplePoint *hovered_point;
	DataSeries *hovered_series;
} ImageDocument;

SamplePoint *sample_point_new(gdouble x, gdouble y, gint64 sample_order);
void sample_point_free(gpointer data);

DataSeries *data_series_new(const gchar *label, guint32 marker_rgba,
		gint display_order);
void data_series_free(gpointer data);
SamplePoint *data_series_add_point(DataSeries *series, gdouble x, gdouble y);
gboolean data_series_remove_point(DataSeries *series, SamplePoint *point);
gint data_series_index_of_point(const DataSeries *series,
		const SamplePoint *point);

ImageDocument *image_document_new(gint64 image_id);
void image_document_free(ImageDocument *document);
DataSeries *image_document_add_series(ImageDocument *document,
		const gchar *label, guint32 marker_rgba);
gboolean image_document_remove_series(ImageDocument *document,
		DataSeries *series);
void image_document_set_active_series(ImageDocument *document,
		DataSeries *series);
gint image_document_index_of_series(const ImageDocument *document,
		const DataSeries *series);
guint32 image_document_next_color(const ImageDocument *document);
gchar *image_document_next_series_label(const ImageDocument *document);
void image_document_clear_selection(ImageDocument *document);
guint image_document_selection_count(const ImageDocument *document);
gboolean image_document_point_is_selected(const ImageDocument *document,
		const SamplePoint *point);
void image_document_select_point(ImageDocument *document, DataSeries *series,
		SamplePoint *point, gboolean extend_selection);

void calibration_state_clear(CalibrationState *calibration);

void rgba_to_components(guint32 rgba, gdouble *red, gdouble *green,
		gdouble *blue, gdouble *alpha);
guint32 rgba_from_components(gdouble red, gdouble green, gdouble blue,
		gdouble alpha);
gchar *rgba_to_string(guint32 rgba);
gboolean rgba_from_string(const gchar *text, guint32 *rgba);

#endif
