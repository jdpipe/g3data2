import os


def _check_pkg_config(context, package_spec):
    context.Message("Checking for pkg-config package '%s'... " % package_spec)
    ok = context.TryAction("pkg-config --exists '%s'" % package_spec)[0]
    context.Result(ok)
    return ok


def _check_gtk_major_at_least(context, major):
    context.Message("Checking GTK major version >= %d... " % major)
    code = """
#include <gtk/gtk.h>
#if GTK_MAJOR_VERSION < %d
#error GTK major version is too old
#endif
int main(void) { return 0; }
""" % major
    ok = context.TryCompile(code, ".c")
    context.Result(ok)
    return ok


env = Environment(ENV=os.environ)

conf = Configure(
    env,
    custom_tests={
        "CheckPkgConfig": _check_pkg_config,
        "CheckGtkMajorAtLeast": _check_gtk_major_at_least,
    },
)

if not conf.CheckProg("pkg-config"):
    print("Error: pkg-config is required to locate GTK3 build flags.")
    Exit(1)

if not conf.CheckPkgConfig("gtk+-3.0 >= 3.0"):
    print("Error: gtk+-3.0 (version 3.0 or newer) is required.")
    Exit(1)

conf.env.ParseConfig("pkg-config --cflags --libs gtk+-3.0")

if not conf.CheckHeader("gtk/gtk.h"):
    print("Error: missing required header <gtk/gtk.h>.")
    Exit(1)

if not conf.CheckHeader("gdk/gdk.h"):
    print("Error: missing required header <gdk/gdk.h>.")
    Exit(1)

if not conf.CheckHeader("cairo.h"):
    print("Error: missing required header <cairo.h>.")
    Exit(1)

if not conf.CheckGtkMajorAtLeast(3):
    print("Error: headers resolve to GTK major version < 3.")
    Exit(1)

env = conf.Finish()

env.Append(CCFLAGS=["-Wall"])
env.Append(LIBS=["m"])

sources = ["main.c", "sort.c", "points.c", "drawing.c"]
env.Program(target="g3data2", source=sources)
