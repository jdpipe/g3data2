---
title: Persistent images, calibration, and data series in G3Data2
number-sections: true
---

# Purpose {#sec:purpose}

This document describes the persistent data model and user interface for
G3Data2. The recommended first release described below is implemented on the
`datastore` branch; later extensions remain identified as such. The result is
that:

- calibration and sampled points survive tab closure and application exit;
- reopening the same image restores its data even if the file was moved;
- one image can contain several named and coloured data series;
- stored points can be found, inspected, moved, and removed on the image; and
- export has an unambiguous series scope.

The central design invariant is that sampled data coordinates are *derived*.
The database stores double-precision positions in the decoded source image's
pixel coordinate system and stores the calibration inputs, never the calculated
X/Y values. Changing the calibration therefore changes every displayed or
exported X/Y value without rewriting the sampled points.

# Previous implementation {#sec:current}

Before the datastore work, all document state lived in `struct TabData`:

- four calibration marker positions in `axiscoords[4][2]`;
- four calibration values in `realcoords[4]`;
- one growable array of double-precision image pixel positions in `points`;
- index-based state for remove-last and point dragging; and
- one colour, red, for every sampled point.

`outputResultset()` calculates data coordinates from the current calibration
when export is requested. This is the correct behaviour to preserve. Closing a
tab frees the point arrays, and closing the process discards all open-tab state.

The image loader can also resample an image in response to `-scale` or `-max`.
That operation is an internal, known transformation and must not redefine the
persistent coordinate system. Source-image pixels and working-raster pixels are
distinguished in [@sec:coordinates].

# Persistence strategies {#sec:strategies}

Three database-location strategies are plausible.

| Strategy | Advantages | Problems |
|---|---|---|
| One application database in the XDG data directory | Automatic, works with read-only image directories, follows moved files by hash, and is easy to migrate | The data does not naturally travel with a collection of images |
| A sidecar database beside each image | Portable with the image and easy to discover | Fails in read-only directories, becomes detached when only one file is moved, and litters source folders |
| A user-selected project database | Portable and suitable for a paper or digitisation project | Introduces project selection and “which database is active?” decisions into a currently simple application |

The recommended first implementation is one application database at:

```text
g_get_user_data_dir()/g3data2/g3data2.sqlite3
```

On a typical Linux installation this is
`~/.local/share/g3data2/g3data2.sqlite3`. The database stores metadata only, not
image bytes. A later `--database PATH` option can provide project databases and
make integration tests deterministic without changing the schema. A
`--no-store` option would be useful for explicitly ephemeral work.

Sidecar storage should not be the default. It could later be offered as an
explicit “Export project” operation, preferably using SQLite's `VACUUM INTO`
rather than copying a live database and its WAL files.

# Image identity {#sec:identity}

The filename is useful metadata but is not a durable identity: files are moved,
renamed, symlinked, or replaced in place. The primary identity should be a
SHA-256 digest of the original file bytes, together with the hash algorithm and
byte size. Each canonical filename seen for that hash is retained as a path
alias.

Opening an image would follow this sequence:

1. Canonicalise the local path and calculate its SHA-256 digest.
2. Decode the image and record its unscaled pixel dimensions.
3. Look up the digest and create an image record if it is new.
4. Add or refresh the path alias.
5. Load calibration, series, points, and the last active series.

This gives the desired cases:

- **Renamed or moved file:** the hash matches and the stored work is restored.
- **Same filename, changed contents:** the hash does not match. G3Data2 opens a
  new image record and warns that this path was previously associated with
  different content; it must not silently place old points on the new image.
- **Same visible image, re-encoded file:** the byte hash differs, so it is a new
  image. A later “Transfer data from stored image” command could support this
  only as an explicit operation with a user-confirmed coordinate transform.
- **Same image opened twice:** the application should focus the existing tab,
  rather than create two independent in-memory copies that can overwrite one
  another.

Always hashing on open is the simplest correct first version. A later cache may
reuse a digest when canonical path, byte size, and high-resolution modification
time all match, but that optimisation should not weaken content identity.

# Coordinate representation {#sec:coordinates}

Calibration markers and sampled points should be stored as double-precision
pixel coordinates in the decoded source raster: `source_x_px` and
`source_y_px`. These are raw fractional pixels, not normalized fractions of the
image dimensions. The stored `pixel_width` and `pixel_height` describe this
coordinate system and provide a validation check when an image is restored.

The current `-scale` and `-max` options physically resample the image before
interaction. Because G3Data2 creates that working raster, it knows the exact
source-to-working transform. Pointer positions are converted back through the
inverse transform before being stored; saved source positions are converted
through the forward transform for drawing on the working raster. For a simple
uniform scale $s$, the conversion is $x_s = x_w/s$ and $y_s = y_w/s$, where the
subscripts denote source and working pixels. The implementation should retain
the actual transform rather than infer it later from a filename or assume that
two arbitrary image dimensions are related.

A cleaner later simplification is to stop physically resampling the loaded
raster and use the existing viewport zoom exclusively. Calibration and sample
positions would then remain in source pixels throughout the model and drawing
code.

An externally resized, cropped, rotated, or otherwise modified file has a
different content hash and therefore a different image record. G3Data2 must not
automatically apply saved coordinates to it. A future transfer operation could
copy data only after the user explicitly identifies the source and target and
supplies or confirms an affine or crop transform.

This representation preserves sub-pixel precision and exact meaning for the
hashed source image. New and moved points should be constrained to that image's
pixel bounds.

The four calibration data values and the X/Y logarithmic flags are persisted.
Calculated X/Y values and calculated errors are not. They remain live results of
`calculatePointValue()`.

# Proposed schema {#sec:schema}

The following schema is deliberately small. Timestamps are UTC ISO 8601 strings
written by the application. The SQL is illustrative; exact constraint syntax
can be settled with the datastore tests.

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE images (
    id              INTEGER PRIMARY KEY,
    hash_algorithm  TEXT NOT NULL DEFAULT 'sha256',
    content_hash    TEXT NOT NULL,
    byte_size       INTEGER NOT NULL,
    pixel_width     INTEGER NOT NULL,
    pixel_height    INTEGER NOT NULL,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL,
    UNIQUE (hash_algorithm, content_hash)
);

CREATE TABLE image_paths (
    image_id       INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,
    canonical_path TEXT NOT NULL,
    first_seen_at  TEXT NOT NULL,
    last_seen_at   TEXT NOT NULL,
    PRIMARY KEY (image_id, canonical_path)
);

CREATE INDEX image_paths_by_path ON image_paths(canonical_path);

CREATE TABLE calibrations (
    image_id    INTEGER PRIMARY KEY REFERENCES images(id) ON DELETE CASCADE,
    x_log       INTEGER NOT NULL DEFAULT 0 CHECK (x_log IN (0, 1)),
    y_log       INTEGER NOT NULL DEFAULT 0 CHECK (y_log IN (0, 1)),
    updated_at  TEXT NOT NULL
);

CREATE TABLE axis_points (
    image_id    INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,
    role        TEXT NOT NULL CHECK (role IN ('x1', 'x2', 'y1', 'y2')),
    source_x_px REAL,
    source_y_px REAL,
    axis_value  REAL,
    updated_at  TEXT NOT NULL,
    PRIMARY KEY (image_id, role),
    CHECK (
        (source_x_px IS NULL AND source_y_px IS NULL) OR
        (source_x_px IS NOT NULL AND source_y_px IS NOT NULL)
    )
);

CREATE TABLE series (
    id             INTEGER PRIMARY KEY,
    image_id       INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,
    label          TEXT NOT NULL,
    marker_rgba    TEXT NOT NULL,
    display_order  INTEGER NOT NULL,
    visible        INTEGER NOT NULL DEFAULT 1 CHECK (visible IN (0, 1)),
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL
);

CREATE INDEX series_by_image ON series(image_id, display_order);

CREATE TABLE points (
    id            INTEGER PRIMARY KEY,
    series_id     INTEGER NOT NULL REFERENCES series(id) ON DELETE CASCADE,
    source_x_px   REAL NOT NULL,
    source_y_px   REAL NOT NULL,
    sample_order  INTEGER NOT NULL,
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    UNIQUE (series_id, sample_order)
);

CREATE INDEX points_by_series ON points(series_id, sample_order);

CREATE TABLE image_state (
    image_id          INTEGER PRIMARY KEY REFERENCES images(id) ON DELETE CASCADE,
    active_series_id  INTEGER REFERENCES series(id) ON DELETE SET NULL,
    updated_at        TEXT NOT NULL
);
```

An absent axis-point row means that neither position nor value has been set.
Null position or value fields permit G3Data2 to persist a partially completed
calibration. The application must ensure that `active_series_id` belongs to the
same image.

`sample_order` is monotonic within a series. Deleting a point leaves a gap;
renumbering every later point is unnecessary. Marker colour is stored in an
unambiguous form such as `#RRGGBBAA` and parsed with `gdk_rgba_parse()`.

Schema creation and migration should run in one transaction and use
`PRAGMA user_version`. Every connection should set:

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 2000;
```

The WAL supports short, reliable write transactions. G3Data2 should never hold
a transaction while waiting for user input.

# In-memory model and datastore boundary {#sec:model}

Adding database calls directly to the current `gdouble **points` manipulation
would be quick, but it would spread persistence logic across drawing, mouse,
button, and export callbacks. It also leaves point identity dependent on an
array index, which becomes awkward as soon as points can be selected or belong
to different series.

The recommended approach is a modest model refactor:

```c
struct SamplePoint {
    gint64 id;
    gdouble source_x_px;
    gdouble source_y_px;
    gint64 sample_order;
};

struct DataSeries {
    gint64 id;
    gchar *label;
    GdkRGBA marker_color;
    gboolean visible;
    GPtrArray *points;
};

struct ImageDocument {
    gint64 image_id;
    struct Calibration calibration;
    GPtrArray *series;
    struct DataSeries *active_series;
    gint64 selected_point_id;
};
```

`TabData` can own an `ImageDocument` while retaining widget and viewport state.
Database IDs give series and points stable identities independent of array
ordering. GLib containers with destroy functions would simplify ownership and
allow tab teardown through one document destructor.

Suggested source modules are:

- `model.c` and `model.h`: documents, calibration, series, points, selection,
  source/working coordinate transforms, and ownership;
- `datastore.c` and `datastore.h`: SQLite opening, migrations, prepared
  statements, hash/path resolution, loading, and write operations;
- `export.c` and `export.h`: formatting a selected export scope; and
- the existing `drawing.c`: colour-aware marker drawing and selection halos.

The database should not be queried from a draw or pointer-motion callback. Load
one image document into memory, render and hit-test that model, and write each
completed mutation through to SQLite.

# Save and restore lifecycle {#sec:lifecycle}

The recommended policy is immediate persistence rather than a Save command:

| User action | Persistence point |
|---|---|
| Add point | Insert after the click is accepted |
| Drag point | Update once on button release, not on every motion event |
| Delete point or series | Delete in one transaction after any confirmation |
| Move calibration marker | Update on button release or completed placement |
| Edit calibration value | Validate immediately; debounce the write or commit on focus-out/Enter |
| Rename or recolour series | Commit on focus-out/Enter or colour selection |
| Change active series/visibility | Persist immediately |

Every multi-row operation, such as deleting a series or replacing all four
calibration points, should use a transaction. A status icon near the series
panel can distinguish “saved”, “saving”, and “database error”. On a write error,
the in-memory edit should remain available and the UI should show a persistent,
non-modal warning with Retry. G3Data2 must not silently discard the change.

On load, widget callbacks should be temporarily blocked, or a `loading` flag
should suppress writes while restored values are being applied. On normal exit,
pending debounced text edits are flushed, prepared statements are finalized,
and the database is closed. Point edits themselves will already be durable, so
an abnormal exit loses at most an unfinished drag or uncommitted text edit.

An unexpected database corruption error must not cause automatic deletion or
replacement of the database. G3Data2 should continue in memory-only mode and
report the database path so the user can recover or back it up.

# User interface concept {#sec:ui}

The existing left-hand controls are already dense. A collapsible **Series and
points** section should be added, while the image remains the dominant area.
An indicative layout is:

```text
+-- Controls ----------------------+------------------------------+
| Axis points                      |                              |
|   X1 ... X2 ... Y1 ... Y2 ...    |          image canvas        |
|                                  |                              |
| Series and points                |   coloured points for every  |
|   ● Curve A             18  👁   |   visible series             |
|   ● Curve B              9  👁   |                              |
|   [ + ] [ Rename ] [ Delete ]    |   selected point has a halo  |
|   Label  [Curve A___________]    |                              |
|   Colour [■]                     |                              |
|                                  |                              |
|   Mode   (● Add) (○ Select/edit) |                              |
|   Selected: Curve A, point 7     |                              |
|   X 12.34   Y 56.78              |                              |
+----------------------------------+------------------------------+
```

The series list shows a colour swatch, editable label, point count, and
visibility toggle. Selecting a row makes it the active series. **Add series**
creates `Series 1`, `Series 2`, and so on with a colour chosen from a
colour-blind-friendly palette; the label can be changed immediately. A
`GtkColorButton` permits an arbitrary marker colour.

The interaction modes should be explicit:

- **Add mode:** a normal click adds a point to the active series. Calibration
  placement buttons temporarily override this mode as they do now.
- **Select/edit mode:** clicking near a visible marker selects it; dragging
  moves it. `Shift`-click adds or removes markers from a multi-point selection.
  `Delete`, Backspace, or **Edit** → **Delete selected point(s)** removes the
  selection.

An explicit mode is easier to discover than relying only on the current
hold-Control-to-move behaviour. Keyboard shortcuts such as `A` for Add, `S` for
Select/edit, and `Delete` for removal can supplement the controls.

Hit testing must use a screen-space radius, for example 7 pixels, so selecting a
point feels the same at every zoom level. Search the active series first, then
other visible series from front to back. If markers overlap, repeated clicks or
a small chooser can cycle through candidates.

Newly opened images use a sticky **Zoom to fit** view. Window-size changes
recalculate that fit until the user manually zooms or pans, or selects a fixed
zoom level.

All visible series are drawn in their stored colours. Active-series markers are
fully opaque; inactive series may be slightly muted. A hovered point gets a
thin halo, and the selected point gets a larger high-contrast double halo that
is visible against both light and dark images. The inspector displays its
series, sample order, current calculated X/Y values, and optionally source-pixel
coordinates. Those calculated values refresh whenever the calibration changes.

The existing removal semantics should become less surprising:

- **Edit** → **Remove last point** removes the highest `sample_order` in the
  current series;
- **Edit** → **Clear current series** asks for confirmation; and
- clearing calibration is a separate action, not the second effect of clicking
  **Remove all points** twice.

# Export behaviour {#sec:export}

The **File** menu provides separate **Export current series** and **Export all
series** submenus. Each scope can be written to stdout, saved through a file
chooser, or copied to the clipboard. Current-series file and stdout output
retains the existing headerless two- or four-column numeric format.
Point ordering and inclusion of value-error columns are persistent check/radio
options in the **File** menu and apply to every export destination.

All-series output uses the same numeric records with a comment line before each
non-empty series:

```text
# Curve A
1.2  3.4
2.3  4.5
# Curve B
5.6  7.8
```

Clipboard output uses tabs between every numeric field and newlines between
records. This makes the plain-text clipboard representation paste directly as
rows and columns in spreadsheet applications such as LibreOffice Calc. Series
comment lines occupy a single cell when all-series output is pasted.

Ordering is applied independently within each series at export time. The
exporter calculates every point from its stored source-image position and the
current calibration immediately before formatting.

# Failure cases and policy decisions {#sec:failure}

The implementation should make the following cases explicit:

- **Incomplete calibration:** restore and display the markers/values that exist,
  but keep coordinate display and export disabled until calibration is valid.
- **Invalid logarithmic calibration:** retain the user's text in the widget only
  while editing; do not commit an invalid non-positive value.
- **Image dimensions disagree for a matching hash:** treat this as an internal
  or decoder error and do not restore points silently.
- **Series deletion:** confirm when non-empty and rely on foreign-key cascade to
  remove its points atomically.
- **Path unavailable:** keep the image and point records. Reopening a moved copy
  by hash restores them.
- **Two application processes:** WAL and a busy timeout prevent common locking
  failures, but version one may document single-process editing. A later
  revision counter can detect that an image document changed externally and
  offer Reload rather than allowing stale state to overwrite it.
- **Explicit command-line calibration:** explicit `-coords`, `-lnx`, or `-lny`
  values take precedence for that opening. Whether they overwrite persisted
  calibration should be confirmed before the feature ships; `--no-store`
  provides an unambiguous scripting path.

# Implementation plan {#sec:plan}

The feature is safest as a sequence of independently testable changes.

1. **Datastore foundation.** Add the `sqlite3` pkg-config dependency to SCons
   and Make, implement database location/opening, schema version 1, prepared
   statements, and temporary-database tests.
2. **Model extraction.** Replace index-only point ownership with stable
   `SamplePoint`, `DataSeries`, and `ImageDocument` objects while presenting one
   default red series. Preserve current drawing and export behaviour.
3. **Single-series persistence.** Hash images, restore/save calibration and the
   default series, preserve source-pixel coordinates across internal load
   scales, and verify persistence over tab and process closure.
4. **Series UI and colour rendering.** Add create, rename, delete, activate,
   visibility, and colour operations. Draw all visible series and make new
   clicks target the active series.
5. **Visual point editing.** Add screen-space hit testing, hover and selected
   states, the point inspector, drag persistence on release, and deletion by
   stable point ID.
6. **Export scope.** Provide current- and all-series actions through the File
   menu; label each series with a comment line in combined output.
7. **Recovery and polish.** Add database-error status, retry, optional project
   database selection, database snapshot/export, and external-change detection.

The model extraction in step 2 is important. A minimal patch that adds a
`series_id` parallel array and scattered SQL calls would reduce initial work but
would make selection, deletion, and error recovery substantially harder.

# Verification plan {#sec:verification}

Automated tests should use a database under a temporary directory and cover:

- schema creation, reopening, migration, foreign keys, and cascade deletion;
- SHA-256 identity across rename, and separation when a path's content changes;
- source-pixel coordinate round trips at several internal image load scales;
- refusal to restore implicitly onto resized, cropped, or otherwise changed
  image content;
- restoration of partial and complete calibration;
- immediate X/Y recalculation after calibration changes;
- independent insert, move, delete, ordering, colour, and visibility per series;
- active-series export compatibility and all-series CSV quoting;
- failure injection for locked or unwritable databases; and
- reopening after every individual mutation, simulating an application crash.

Pure functions should cover hit testing, nearest-point selection, coordinate
conversion, and export formatting without requiring a GTK display. A smaller
set of GTK integration tests can verify widget/model synchronization and mode
switching.

# Recommended first release {#sec:recommendation}

The smallest coherent release is not merely “save the current arrays.” It is:

1. one global XDG SQLite database keyed by SHA-256 image content;
2. raw fractional source-pixel calibration and point positions;
3. a model containing one or more stable-ID series;
4. write-through persistence after completed user actions;
5. a series list with label, colour, visibility, and active selection;
6. explicit Add and Select/edit modes with selected-point deletion; and
7. export of the active series, retaining the existing numeric format.

This scope delivers the persistence and multi-series workflow without forcing a
new interchange format. “All visible series” export and portable project
databases can then be added without changing the core schema or coordinate
model.
