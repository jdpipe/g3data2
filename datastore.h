#ifndef G3DATA2_DATASTORE_H
#define G3DATA2_DATASTORE_H

#include <glib.h>

#include "model.h"

typedef struct Datastore Datastore;

#define DATASTORE_ERROR datastore_error_quark()

typedef enum {
	DATASTORE_ERROR_OPEN,
	DATASTORE_ERROR_SQL,
	DATASTORE_ERROR_IO,
	DATASTORE_ERROR_IDENTITY
} DatastoreError;

GQuark datastore_error_quark(void);

Datastore *datastore_open(const gchar *path, GError **error);
Datastore *datastore_open_default(GError **error);
void datastore_close(Datastore *datastore);
const gchar *datastore_get_path(const Datastore *datastore);

gchar *datastore_hash_file(const gchar *filename, gint64 *byte_size,
		GError **error);
gboolean datastore_resolve_image(Datastore *datastore, const gchar *filename,
		gint source_width, gint source_height, gint64 *image_id,
		gboolean *existing, GError **error);
gboolean datastore_load_document(Datastore *datastore, gint64 image_id,
		ImageDocument *document, CalibrationState *calibration, GError **error);

gboolean datastore_save_calibration(Datastore *datastore, gint64 image_id,
		const CalibrationState *calibration, GError **error);

gboolean datastore_insert_series(Datastore *datastore, gint64 image_id,
		DataSeries *series, GError **error);
gboolean datastore_update_series(Datastore *datastore,
		const DataSeries *series, GError **error);
gboolean datastore_delete_series(Datastore *datastore, gint64 series_id,
		GError **error);
gboolean datastore_set_active_series(Datastore *datastore, gint64 image_id,
		gint64 series_id, GError **error);

gboolean datastore_insert_point(Datastore *datastore, gint64 series_id,
		SamplePoint *point, GError **error);
gboolean datastore_update_point(Datastore *datastore,
		const SamplePoint *point, GError **error);
gboolean datastore_delete_point(Datastore *datastore, gint64 point_id,
		GError **error);
gboolean datastore_delete_series_points(Datastore *datastore,
		gint64 series_id, GError **error);

#endif
