# What is g3data2?

![Screenshot](doc/screenshot-bg.png)

g3data2 is a tool for extracting data from scanned graphs. For graphs published in scientific articles the actual data is often not
explicitly given; g3data makes the process of extracting this data fairly easy and fairly accurate.

Original 'g3data' source (GTK2 version): http://github.com/pn2200/g3data/
Fork 'g3data2' for GTK3: https://github.com/jonasfrantz/g3data2
New fork, same terrible name: https://github.com/jdpipe/g3data2

# Building from source

Install the GTK3 and SQLite development packages for your distro, as well as
SCons (www.scons.org). Then run `scons` to build the tool. CUnit is optional;
when available, `scons test` builds and runs the datastore/model tests.

On Ubuntu 24.04, this would be

```sh
sudo apt install libgtk-3-dev libsqlite3-dev libcunit1-dev scons
git clone https://github.com/jdpipe/g3data2.git
cd g3data2
scons
./g3data2
```

# Using the program

It's fairly self-explanatory. But to be explicit:

* a new or partially calibrated image starts a guided axis workflow in the left pane. Pick X1, X2, Y1, and Y2 on the image and enter each axis value. You can choose **Sample data instead** and return to calibration later; raw sampled points do not require a completed calibration.
* next you click points in the graph, amassing a list of points that can be copied to the clipboard, output to stdout, or written to a `.dat` file chosen through a save dialog. The **File** menu exports the current or all series to stdout or a file; the **Edit** menu copies either scope to the clipboard. Point ordering and inclusion of error columns are persistent options grouped beneath the File export actions.
* sampled points are kept as double-precision image pixel coordinates. Data coordinates are recalculated from the current axis calibration whenever they are displayed or exported.
* once all four axis references and values are set, hold Alt briefly over the image to show the axis reader. It draws skew-aware guides from the cursor to both calibrated axes and shows the current X/Y values in a fixed-size yellow label. **View** → **Show uncertainty** adds the calculated uncertainty to that label. Pressing another key while Alt is held suppresses the reader, so menu shortcuts such as Alt+F behave normally.
* **View** → **Show positioning circle** adds a translucent black circle with white inner and outer edging at the image cursor. Ctrl+. enlarges the circle and Ctrl+, shrinks it in half-source-pixel diameter steps; the same actions are available in the View menu. Because its size is defined in original-image pixels, it follows image zoom and helps centre points on broad markers or thick lines. Diameter is remembered per image with its calibration, while visibility is remembered as an application preference.
* each image can have several named, coloured series. The five-row series list shows colour, label, point count, and visibility. Select the active series, use `+` to add one, and double-click or press Enter/F2 to rename. Right-click a series to delete it.
* a normal image click adds a point. Shift-click toggles a point in the active-series selection; Shift-drag selects several points with a marquee. Drag an already selected marker to move it. The **Edit** menu clears the current series or deletes the selected point(s); Delete and Backspace also delete the selection while the image has focus. Inactive visible series remain on screen in muted colours.
* content changes are immediately persistent and have a per-tab, memory-only undo/redo history. **Edit** → **Undo** (Ctrl+Z) and **Redo** (Ctrl+Shift+Z) cover point, series, and calibration changes. Closing the tab discards the history but retains its current state in SQLite. Export preferences and zoom/pan are not undoable.
* images initially use **Zoom to fit**. This remains active across window resizing and maximising until you manually zoom or pan, or choose another zoom level. You can zoom using ctrl-wheel, pan up and down with the mouse wheel, and pan left and right with shift-wheel. Smaller images can be enlarged to 16×; the maximum is reduced automatically for large images to keep the scrollable canvas within a safe size.
* zooming in is a good idea when selecting points. g3data2 has sub-pixel precision for identifying and working with point data.
* calibration, series, and sampled positions are saved automatically in `~/.local/share/g3data2/g3data2.sqlite3`. Reopening the same image content restores them, even after the image file is renamed or moved.
* clipboard exports advertise the `text/tab-separated-values` format explicitly, so they paste directly into spreadsheet cells in applications such as LibreOffice Calc without being mistaken for Markdown. Plain UTF-8 text remains available as a fallback.
* g3data2 will remember your recent files, which is useful because you never get this stuff right first time.
* once you have your data file, consider using fityk to do the curve fitting! one you get used to it, it's very powerful!

# Recent changes

* Added main-pane zoom to allow better precision when selecting points
* Added pixel-value calculations based double-precision arithmetic rather than integers
* Added persistent calibration and multi-series point storage using SQLite
* Added graphical point selection, movement, and deletion
* Added per-image in-memory undo/redo and a guided calibration workflow
* Implements SCons build script instead of bare Makefile

# Roadmap

* Implement packaging for Ubuntu/elsewhere (help needed!)
* Portable project databases and database snapshots
* Less clicking for initial x1/x2/y1/y2 selection
* Ability to force orthogonality (eg when no rotation of the image is possible due to its provenance)
* Implement smart curve tracing?

# License

g3data2 is distributed under the GNU General Public License (GPL), as
described in the 'COPYING' file.

John Pye, Feb 2026
