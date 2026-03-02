# What is g3data2?

g3data2 is a tool for extracting data from scanned graphs. For graphs
published in scientific articles the actual data is usually not
explicitly given; g3data makes the process of extracting this data easy.

Original 'g3data' source (GTK2 version): http://github.com/pn2200/g3data/
Fork 'g3data2' for GTK3: https://github.com/jonasfrantz/g3data2
This fork: https://github.com/jdpipe/g3data2

# Building from source

Install GTK3 devel packages for your distro as well as SCons (www.scons.org). Then run 'scons' to build the tool. You can run it from your working directory.

```sh
scons
```

# Recent changes

* Added main-pane zoom to allow better precision when selecting points
* Added pixel-value calculations based double-precision arithmetic rather than integers
* Implements SCons build script instead of bare Makefile

# Roadmap

* Implement packaging for Ubuntu/elsewhere (help needed!)
* Ability to store multiple curves from a single plot without having to start again
* Ability to remove points selectively (graphically)
* Less clicking for initial x1/x2/y1/y2 selection
* Ability to force orthogonality (eg when no rotation of the image is possible due to its provenance)
* Implement smart curve tracing?

# License

g3data2 is distributed under the GNU General Public License (GPL), as
described in the 'COPYING' file.

John Pye, Feb 2026
