#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#include "datastore.h"

#define DATASTORE_SCHEMA_VERSION 1

struct Datastore {
	sqlite3 *db;
	gchar *path;
};

G_DEFINE_QUARK(g3data2-datastore-error-quark, datastore_error)

static const gchar *SCHEMA_SQL =
		"CREATE TABLE IF NOT EXISTS images ("
		" id INTEGER PRIMARY KEY,"
		" hash_algorithm TEXT NOT NULL DEFAULT 'sha256',"
		" content_hash TEXT NOT NULL,"
		" byte_size INTEGER NOT NULL,"
		" pixel_width INTEGER NOT NULL,"
		" pixel_height INTEGER NOT NULL,"
		" created_at TEXT NOT NULL,"
		" updated_at TEXT NOT NULL,"
		" UNIQUE(hash_algorithm, content_hash));"
		"CREATE TABLE IF NOT EXISTS image_paths ("
		" image_id INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,"
		" canonical_path TEXT NOT NULL,"
		" first_seen_at TEXT NOT NULL,"
		" last_seen_at TEXT NOT NULL,"
		" PRIMARY KEY(image_id, canonical_path));"
		"CREATE INDEX IF NOT EXISTS image_paths_by_path"
		" ON image_paths(canonical_path);"
		"CREATE TABLE IF NOT EXISTS calibrations ("
		" image_id INTEGER PRIMARY KEY REFERENCES images(id) ON DELETE CASCADE,"
		" x_log INTEGER NOT NULL DEFAULT 0 CHECK(x_log IN (0,1)),"
		" y_log INTEGER NOT NULL DEFAULT 0 CHECK(y_log IN (0,1)),"
		" updated_at TEXT NOT NULL);"
		"CREATE TABLE IF NOT EXISTS axis_points ("
		" image_id INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,"
		" role TEXT NOT NULL CHECK(role IN ('x1','x2','y1','y2')),"
		" source_x_px REAL, source_y_px REAL, axis_value REAL,"
		" updated_at TEXT NOT NULL,"
		" PRIMARY KEY(image_id, role),"
		" CHECK((source_x_px IS NULL AND source_y_px IS NULL) OR"
		" (source_x_px IS NOT NULL AND source_y_px IS NOT NULL)));"
		"CREATE TABLE IF NOT EXISTS series ("
		" id INTEGER PRIMARY KEY,"
		" image_id INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,"
		" label TEXT NOT NULL, marker_rgba TEXT NOT NULL,"
		" display_order INTEGER NOT NULL,"
		" visible INTEGER NOT NULL DEFAULT 1 CHECK(visible IN (0,1)),"
		" created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
		"CREATE INDEX IF NOT EXISTS series_by_image"
		" ON series(image_id, display_order);"
		"CREATE TABLE IF NOT EXISTS points ("
		" id INTEGER PRIMARY KEY,"
		" series_id INTEGER NOT NULL REFERENCES series(id) ON DELETE CASCADE,"
		" source_x_px REAL NOT NULL, source_y_px REAL NOT NULL,"
		" sample_order INTEGER NOT NULL,"
		" created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
		" UNIQUE(series_id, sample_order));"
		"CREATE INDEX IF NOT EXISTS points_by_series"
		" ON points(series_id, sample_order);"
		"CREATE TABLE IF NOT EXISTS image_state ("
		" image_id INTEGER PRIMARY KEY REFERENCES images(id) ON DELETE CASCADE,"
		" active_series_id INTEGER REFERENCES series(id) ON DELETE SET NULL,"
		" updated_at TEXT NOT NULL);";

static gchar *now_utc(void) {
	GDateTime *date_time;
	gchar *result;

	date_time = g_date_time_new_now_utc();
	result = g_date_time_format(date_time, "%Y-%m-%dT%H:%M:%SZ");
	g_date_time_unref(date_time);
	return result;
}

static void set_sql_error(Datastore *datastore, GError **error,
		const gchar *context, gint result) {
	if (error == NULL || *error != NULL)
		return;
	g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_SQL, "%s: %s (%d)",
			context, datastore != NULL && datastore->db != NULL
					? sqlite3_errmsg(datastore->db) : "SQLite error", result);
}

static gboolean exec_sql(Datastore *datastore, const gchar *sql,
		GError **error) {
	gchar *message;
	gint result;

	message = NULL;
	result = sqlite3_exec(datastore->db, sql, NULL, NULL, &message);
	if (result != SQLITE_OK) {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_SQL, "%s",
				message != NULL ? message : sqlite3_errmsg(datastore->db));
		sqlite3_free(message);
		return FALSE;
	}
	return TRUE;
}

static gboolean prepare(Datastore *datastore, const gchar *sql,
		sqlite3_stmt **statement, GError **error) {
	gint result;

	result = sqlite3_prepare_v2(datastore->db, sql, -1, statement, NULL);
	if (result != SQLITE_OK) {
		set_sql_error(datastore, error, "Preparing SQL", result);
		return FALSE;
	}
	return TRUE;
}

static gboolean step_done(Datastore *datastore, sqlite3_stmt *statement,
		const gchar *context, GError **error) {
	gint result;

	result = sqlite3_step(statement);
	if (result != SQLITE_DONE) {
		set_sql_error(datastore, error, context, result);
		return FALSE;
	}
	return TRUE;
}

static gboolean initialize_schema(Datastore *datastore, GError **error) {
	gchar *version_sql;
	gboolean result;
	sqlite3_stmt *statement;
	gint schema_version, step_result;

	if (!exec_sql(datastore, "PRAGMA foreign_keys=ON;", error)
			|| !exec_sql(datastore, "PRAGMA busy_timeout=2000;", error))
		return FALSE;
	statement = NULL;
	if (!prepare(datastore, "PRAGMA user_version;", &statement, error))
		return FALSE;
	step_result = sqlite3_step(statement);
	schema_version = step_result == SQLITE_ROW ? sqlite3_column_int(statement, 0)
			: -1;
	if (step_result != SQLITE_ROW)
		set_sql_error(datastore, error, "Reading schema version", step_result);
	sqlite3_finalize(statement);
	if (schema_version < 0)
		return FALSE;
	if (schema_version > DATASTORE_SCHEMA_VERSION) {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_OPEN,
				"Database schema version %d is newer than this G3Data2 supports (%d)",
				schema_version, DATASTORE_SCHEMA_VERSION);
		return FALSE;
	}
	if (!exec_sql(datastore, "PRAGMA journal_mode=WAL;", error)
			|| !exec_sql(datastore, "BEGIN IMMEDIATE;", error))
		return FALSE;
	if (!exec_sql(datastore, SCHEMA_SQL, error)) {
		exec_sql(datastore, "ROLLBACK;", NULL);
		return FALSE;
	}
	version_sql = g_strdup_printf("PRAGMA user_version=%d;",
			DATASTORE_SCHEMA_VERSION);
	result = exec_sql(datastore, version_sql, error)
			&& exec_sql(datastore, "COMMIT;", error);
	g_free(version_sql);
	if (!result)
		exec_sql(datastore, "ROLLBACK;", NULL);
	return result;
}

Datastore *datastore_open(const gchar *path, GError **error) {
	Datastore *datastore;
	gint result;

	if (path == NULL || *path == '\0') {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_OPEN,
				"No database path was supplied");
		return NULL;
	}
	datastore = g_new0(Datastore, 1);
	datastore->path = g_strdup(path);
	result = sqlite3_open_v2(path, &datastore->db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
	if (result != SQLITE_OK) {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_OPEN,
				"Could not open database '%s': %s", path,
				datastore->db != NULL ? sqlite3_errmsg(datastore->db) : "unknown error");
		datastore_close(datastore);
		return NULL;
	}
	if (!initialize_schema(datastore, error)) {
		datastore_close(datastore);
		return NULL;
	}
	return datastore;
}

Datastore *datastore_open_default(GError **error) {
	gchar *directory, *path;
	Datastore *datastore;

	directory = g_build_filename(g_get_user_data_dir(), "g3data2", NULL);
	if (g_mkdir_with_parents(directory, 0700) != 0) {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_OPEN,
				"Could not create database directory '%s': %s", directory,
				g_strerror(errno));
		g_free(directory);
		return NULL;
	}
	path = g_build_filename(directory, "g3data2.sqlite3", NULL);
	datastore = datastore_open(path, error);
	g_free(path);
	g_free(directory);
	return datastore;
}

void datastore_close(Datastore *datastore) {
	if (datastore == NULL)
		return;
	if (datastore->db != NULL)
		sqlite3_close(datastore->db);
	g_free(datastore->path);
	g_free(datastore);
}

const gchar *datastore_get_path(const Datastore *datastore) {
	return datastore != NULL ? datastore->path : NULL;
}

gchar *datastore_hash_file(const gchar *filename, gint64 *byte_size,
		GError **error) {
	FILE *file;
	GChecksum *checksum;
	guchar buffer[64 * 1024];
	size_t count;
	gint64 length;
	gchar *digest;

	file = g_fopen(filename, "rb");
	if (file == NULL) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
				"Could not read '%s': %s", filename, g_strerror(errno));
		return NULL;
	}
	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	length = 0;
	while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		g_checksum_update(checksum, buffer, count);
		length += (gint64) count;
	}
	if (ferror(file)) {
		gint read_error;
		read_error = errno != 0 ? errno : EIO;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(read_error),
				"Could not read '%s': %s", filename, g_strerror(read_error));
		fclose(file);
		g_checksum_free(checksum);
		return NULL;
	}
	fclose(file);
	digest = g_strdup(g_checksum_get_string(checksum));
	g_checksum_free(checksum);
	if (byte_size != NULL)
		*byte_size = length;
	return digest;
}

gboolean datastore_resolve_image(Datastore *datastore, const gchar *filename,
		gint source_width, gint source_height, gint64 *image_id,
		gboolean *existing, GError **error) {
	sqlite3_stmt *statement;
	gchar *digest, *canonical_path, *timestamp;
	gint64 size, found_id;
	gint stored_width, stored_height, result;
	gboolean found, success;

	digest = datastore_hash_file(filename, &size, error);
	if (digest == NULL)
		return FALSE;
	canonical_path = g_canonicalize_filename(filename, NULL);
	timestamp = now_utc();
	statement = NULL;
	found = FALSE;
	found_id = 0;
	stored_width = stored_height = 0;
	success = exec_sql(datastore, "BEGIN IMMEDIATE;", error);
	if (success)
		success = prepare(datastore,
				"SELECT id,pixel_width,pixel_height FROM images"
				" WHERE hash_algorithm='sha256' AND content_hash=?1;",
				&statement, error);
	if (success) {
		sqlite3_bind_text(statement, 1, digest, -1, SQLITE_TRANSIENT);
		result = sqlite3_step(statement);
		if (result == SQLITE_ROW) {
			found = TRUE;
			found_id = sqlite3_column_int64(statement, 0);
			stored_width = sqlite3_column_int(statement, 1);
			stored_height = sqlite3_column_int(statement, 2);
		} else if (result != SQLITE_DONE) {
			set_sql_error(datastore, error, "Looking up image", result);
			success = FALSE;
		}
	}
	sqlite3_finalize(statement);
	statement = NULL;
	if (success && found
			&& (stored_width != source_width || stored_height != source_height)) {
		g_set_error(error, DATASTORE_ERROR, DATASTORE_ERROR_IDENTITY,
				"Image hash matched but decoded dimensions changed (%dx%d vs %dx%d)",
				stored_width, stored_height, source_width, source_height);
		success = FALSE;
	}
	if (success && !found) {
		success = prepare(datastore,
				"INSERT INTO images(hash_algorithm,content_hash,byte_size,"
				"pixel_width,pixel_height,created_at,updated_at)"
				" VALUES('sha256',?1,?2,?3,?4,?5,?5);", &statement, error);
		if (success) {
			sqlite3_bind_text(statement, 1, digest, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(statement, 2, size);
			sqlite3_bind_int(statement, 3, source_width);
			sqlite3_bind_int(statement, 4, source_height);
			sqlite3_bind_text(statement, 5, timestamp, -1, SQLITE_TRANSIENT);
			success = step_done(datastore, statement, "Creating image", error);
			if (success)
				found_id = sqlite3_last_insert_rowid(datastore->db);
		}
		sqlite3_finalize(statement);
		statement = NULL;
	}
	if (success) {
		success = prepare(datastore,
				"INSERT INTO image_paths(image_id,canonical_path,first_seen_at,last_seen_at)"
				" VALUES(?1,?2,?3,?3) ON CONFLICT(image_id,canonical_path)"
				" DO UPDATE SET last_seen_at=excluded.last_seen_at;", &statement, error);
		if (success) {
			sqlite3_bind_int64(statement, 1, found_id);
			sqlite3_bind_text(statement, 2, canonical_path, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(statement, 3, timestamp, -1, SQLITE_TRANSIENT);
			success = step_done(datastore, statement, "Recording image path", error);
		}
		sqlite3_finalize(statement);
	}
	if (success)
		success = exec_sql(datastore, "COMMIT;", error);
	else
		exec_sql(datastore, "ROLLBACK;", NULL);
	if (success) {
		if (image_id != NULL)
			*image_id = found_id;
		if (existing != NULL)
			*existing = found;
	}
	g_free(digest);
	g_free(canonical_path);
	g_free(timestamp);
	return success;
}

static DataSeries *find_series_by_id(ImageDocument *document, gint64 id) {
	guint i;
	for (i = 0; i < document->series->len; i++) {
		DataSeries *series = g_ptr_array_index(document->series, i);
		if (series->id == id)
			return series;
	}
	return NULL;
}

static gboolean load_points(Datastore *datastore, DataSeries *series,
		GError **error) {
	sqlite3_stmt *statement;
	gint result;
	gboolean success;

	statement = NULL;
	success = prepare(datastore,
			"SELECT id,source_x_px,source_y_px,sample_order FROM points"
			" WHERE series_id=?1 ORDER BY sample_order;", &statement, error);
	if (!success)
		return FALSE;
	sqlite3_bind_int64(statement, 1, series->id);
	while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
		SamplePoint *point;
		point = sample_point_new(sqlite3_column_double(statement, 1),
				sqlite3_column_double(statement, 2),
				sqlite3_column_int64(statement, 3));
		point->id = sqlite3_column_int64(statement, 0);
		g_ptr_array_add(series->points, point);
	}
	if (result != SQLITE_DONE) {
		set_sql_error(datastore, error, "Loading points", result);
		success = FALSE;
	}
	sqlite3_finalize(statement);
	return success;
}

gboolean datastore_load_document(Datastore *datastore, gint64 image_id,
		ImageDocument *document, CalibrationState *calibration, GError **error) {
	sqlite3_stmt *statement;
	gint result;
	gint64 active_series_id;
	gboolean success;
	static const gchar *roles[] = {"x1", "x2", "y1", "y2"};

	calibration_state_clear(calibration);
	g_ptr_array_set_size(document->series, 0);
	document->image_id = image_id;
	document->active_series = NULL;
	image_document_clear_selection(document);
	active_series_id = 0;
	statement = NULL;
	success = prepare(datastore,
			"SELECT x_log,y_log FROM calibrations WHERE image_id=?1;",
			&statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		result = sqlite3_step(statement);
		if (result == SQLITE_ROW) {
			calibration->log_axis[0] = sqlite3_column_int(statement, 0) != 0;
			calibration->log_axis[1] = sqlite3_column_int(statement, 1) != 0;
		} else if (result != SQLITE_DONE) {
			set_sql_error(datastore, error, "Loading calibration", result);
			success = FALSE;
		}
	}
	sqlite3_finalize(statement);
	statement = NULL;
	if (success)
		success = prepare(datastore,
				"SELECT role,source_x_px,source_y_px,axis_value FROM axis_points"
				" WHERE image_id=?1;", &statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
			const gchar *role = (const gchar *) sqlite3_column_text(statement, 0);
			gint i;
			for (i = 0; i < G3_AXIS_POINT_COUNT; i++)
				if (g_strcmp0(role, roles[i]) == 0)
					break;
			if (i == G3_AXIS_POINT_COUNT)
				continue;
			if (sqlite3_column_type(statement, 1) != SQLITE_NULL) {
				calibration->axis_x[i] = sqlite3_column_double(statement, 1);
				calibration->axis_y[i] = sqlite3_column_double(statement, 2);
				calibration->position_set[i] = TRUE;
			}
			if (sqlite3_column_type(statement, 3) != SQLITE_NULL) {
				calibration->axis_value[i] = sqlite3_column_double(statement, 3);
				calibration->value_set[i] = TRUE;
			}
		}
		if (result != SQLITE_DONE) {
			set_sql_error(datastore, error, "Loading axis points", result);
			success = FALSE;
		}
	}
	sqlite3_finalize(statement);
	statement = NULL;
	if (success)
		success = prepare(datastore,
				"SELECT active_series_id FROM image_state WHERE image_id=?1;",
				&statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		result = sqlite3_step(statement);
		if (result == SQLITE_ROW)
			active_series_id = sqlite3_column_int64(statement, 0);
		else if (result != SQLITE_DONE) {
			set_sql_error(datastore, error, "Loading active series", result);
			success = FALSE;
		}
	}
	sqlite3_finalize(statement);
	statement = NULL;
	if (success)
		success = prepare(datastore,
				"SELECT id,label,marker_rgba,display_order,visible FROM series"
				" WHERE image_id=?1 ORDER BY display_order,id;", &statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
			DataSeries *series;
			guint32 color;
			const gchar *color_text;
			color_text = (const gchar *) sqlite3_column_text(statement, 2);
			if (!rgba_from_string(color_text, &color))
				color = G3_COLOR_RED;
			series = data_series_new(
					(const gchar *) sqlite3_column_text(statement, 1), color,
					sqlite3_column_int(statement, 3));
			series->id = sqlite3_column_int64(statement, 0);
			series->visible = sqlite3_column_int(statement, 4) != 0;
			g_ptr_array_add(document->series, series);
			if (!load_points(datastore, series, error)) {
				success = FALSE;
				break;
			}
		}
		if (result != SQLITE_DONE && success) {
			set_sql_error(datastore, error, "Loading series", result);
			success = FALSE;
		}
	}
	sqlite3_finalize(statement);
	if (success && document->series->len > 0) {
		document->active_series = find_series_by_id(document, active_series_id);
		if (document->active_series == NULL)
			document->active_series = g_ptr_array_index(document->series, 0);
	}
	if (!success) {
		g_ptr_array_set_size(document->series, 0);
		document->active_series = NULL;
		image_document_clear_selection(document);
	}
	return success;
}

gboolean datastore_save_calibration(Datastore *datastore, gint64 image_id,
		const CalibrationState *calibration, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp;
	gboolean success;
	gint i;
	static const gchar *roles[] = {"x1", "x2", "y1", "y2"};

	timestamp = now_utc();
	statement = NULL;
	success = exec_sql(datastore, "BEGIN IMMEDIATE;", error)
			&& prepare(datastore,
					"INSERT INTO calibrations(image_id,x_log,y_log,updated_at)"
					" VALUES(?1,?2,?3,?4) ON CONFLICT(image_id) DO UPDATE SET"
					" x_log=excluded.x_log,y_log=excluded.y_log,"
					" updated_at=excluded.updated_at;", &statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		sqlite3_bind_int(statement, 2, calibration->log_axis[0]);
		sqlite3_bind_int(statement, 3, calibration->log_axis[1]);
		sqlite3_bind_text(statement, 4, timestamp, -1, SQLITE_TRANSIENT);
		success = step_done(datastore, statement, "Saving calibration", error);
	}
	sqlite3_finalize(statement);
	for (i = 0; success && i < G3_AXIS_POINT_COUNT; i++) {
		statement = NULL;
		success = prepare(datastore,
				"INSERT INTO axis_points(image_id,role,source_x_px,source_y_px,"
				"axis_value,updated_at) VALUES(?1,?2,?3,?4,?5,?6)"
				" ON CONFLICT(image_id,role) DO UPDATE SET"
				" source_x_px=excluded.source_x_px,source_y_px=excluded.source_y_px,"
				" axis_value=excluded.axis_value,updated_at=excluded.updated_at;",
				&statement, error);
		if (!success)
			break;
		sqlite3_bind_int64(statement, 1, image_id);
		sqlite3_bind_text(statement, 2, roles[i], -1, SQLITE_STATIC);
		if (calibration->position_set[i]) {
			sqlite3_bind_double(statement, 3, calibration->axis_x[i]);
			sqlite3_bind_double(statement, 4, calibration->axis_y[i]);
		} else {
			sqlite3_bind_null(statement, 3);
			sqlite3_bind_null(statement, 4);
		}
		if (calibration->value_set[i])
			sqlite3_bind_double(statement, 5, calibration->axis_value[i]);
		else
			sqlite3_bind_null(statement, 5);
		sqlite3_bind_text(statement, 6, timestamp, -1, SQLITE_TRANSIENT);
		success = step_done(datastore, statement, "Saving axis point", error);
		sqlite3_finalize(statement);
	}
	if (success)
		success = exec_sql(datastore, "COMMIT;", error);
	else
		exec_sql(datastore, "ROLLBACK;", NULL);
	g_free(timestamp);
	return success;
}

gboolean datastore_insert_series(Datastore *datastore, gint64 image_id,
		DataSeries *series, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp, *color;
	gboolean success;

	timestamp = now_utc();
	color = rgba_to_string(series->marker_rgba);
	statement = NULL;
	success = prepare(datastore,
			"INSERT INTO series(image_id,label,marker_rgba,display_order,visible,"
			"created_at,updated_at) VALUES(?1,?2,?3,?4,?5,?6,?6);",
			&statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		sqlite3_bind_text(statement, 2, series->label, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(statement, 3, color, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(statement, 4, series->display_order);
		sqlite3_bind_int(statement, 5, series->visible);
		sqlite3_bind_text(statement, 6, timestamp, -1, SQLITE_TRANSIENT);
		success = step_done(datastore, statement, "Creating series", error);
		if (success)
			series->id = sqlite3_last_insert_rowid(datastore->db);
	}
	sqlite3_finalize(statement);
	g_free(timestamp);
	g_free(color);
	return success;
}

gboolean datastore_update_series(Datastore *datastore,
		const DataSeries *series, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp, *color;
	gboolean success;

	timestamp = now_utc();
	color = rgba_to_string(series->marker_rgba);
	statement = NULL;
	success = prepare(datastore,
			"UPDATE series SET label=?1,marker_rgba=?2,display_order=?3,visible=?4,"
			"updated_at=?5 WHERE id=?6;", &statement, error);
	if (success) {
		sqlite3_bind_text(statement, 1, series->label, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(statement, 2, color, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(statement, 3, series->display_order);
		sqlite3_bind_int(statement, 4, series->visible);
		sqlite3_bind_text(statement, 5, timestamp, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(statement, 6, series->id);
		success = step_done(datastore, statement, "Updating series", error);
	}
	sqlite3_finalize(statement);
	g_free(timestamp);
	g_free(color);
	return success;
}

static gboolean delete_by_id(Datastore *datastore, const gchar *sql,
		gint64 id, const gchar *context, GError **error) {
	sqlite3_stmt *statement;
	gboolean success;

	statement = NULL;
	success = prepare(datastore, sql, &statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, id);
		success = step_done(datastore, statement, context, error);
	}
	sqlite3_finalize(statement);
	return success;
}

gboolean datastore_delete_series(Datastore *datastore, gint64 series_id,
		GError **error) {
	return delete_by_id(datastore, "DELETE FROM series WHERE id=?1;", series_id,
			"Deleting series", error);
}

gboolean datastore_set_active_series(Datastore *datastore, gint64 image_id,
		gint64 series_id, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp;
	gboolean success;

	timestamp = now_utc();
	statement = NULL;
	success = prepare(datastore,
			"INSERT INTO image_state(image_id,active_series_id,updated_at)"
			" VALUES(?1,?2,?3) ON CONFLICT(image_id) DO UPDATE SET"
			" active_series_id=excluded.active_series_id,updated_at=excluded.updated_at;",
			&statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, image_id);
		sqlite3_bind_int64(statement, 2, series_id);
		sqlite3_bind_text(statement, 3, timestamp, -1, SQLITE_TRANSIENT);
		success = step_done(datastore, statement, "Selecting series", error);
	}
	sqlite3_finalize(statement);
	g_free(timestamp);
	return success;
}

gboolean datastore_insert_point(Datastore *datastore, gint64 series_id,
		SamplePoint *point, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp;
	gboolean success;

	timestamp = now_utc();
	statement = NULL;
	success = prepare(datastore,
			"INSERT INTO points(series_id,source_x_px,source_y_px,sample_order,"
			"created_at,updated_at) VALUES(?1,?2,?3,?4,?5,?5);",
			&statement, error);
	if (success) {
		sqlite3_bind_int64(statement, 1, series_id);
		sqlite3_bind_double(statement, 2, point->source_x_px);
		sqlite3_bind_double(statement, 3, point->source_y_px);
		sqlite3_bind_int64(statement, 4, point->sample_order);
		sqlite3_bind_text(statement, 5, timestamp, -1, SQLITE_TRANSIENT);
		success = step_done(datastore, statement, "Creating point", error);
		if (success)
			point->id = sqlite3_last_insert_rowid(datastore->db);
	}
	sqlite3_finalize(statement);
	g_free(timestamp);
	return success;
}

gboolean datastore_update_point(Datastore *datastore,
		const SamplePoint *point, GError **error) {
	sqlite3_stmt *statement;
	gchar *timestamp;
	gboolean success;

	timestamp = now_utc();
	statement = NULL;
	success = prepare(datastore,
			"UPDATE points SET source_x_px=?1,source_y_px=?2,updated_at=?3"
			" WHERE id=?4;", &statement, error);
	if (success) {
		sqlite3_bind_double(statement, 1, point->source_x_px);
		sqlite3_bind_double(statement, 2, point->source_y_px);
		sqlite3_bind_text(statement, 3, timestamp, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(statement, 4, point->id);
		success = step_done(datastore, statement, "Moving point", error);
	}
	sqlite3_finalize(statement);
	g_free(timestamp);
	return success;
}

gboolean datastore_delete_point(Datastore *datastore, gint64 point_id,
		GError **error) {
	return delete_by_id(datastore, "DELETE FROM points WHERE id=?1;", point_id,
			"Deleting point", error);
}

gboolean datastore_delete_series_points(Datastore *datastore,
		gint64 series_id, GError **error) {
	return delete_by_id(datastore, "DELETE FROM points WHERE series_id=?1;",
			series_id, "Clearing series", error);
}
