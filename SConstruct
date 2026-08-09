import os
from SCons.Variables import BoolVariable


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


vars = Variables()
vars.Add(BoolVariable("DEBUG", "Enable debug logging (G3DATA2_DEBUG)", False))

build_environment = dict(os.environ)
user_pkgconfig = os.path.expanduser("~/.local/lib/pkgconfig")
if os.path.isdir(user_pkgconfig):
    current_pkgconfig = build_environment.get("PKG_CONFIG_PATH", "")
    build_environment["PKG_CONFIG_PATH"] = (
        user_pkgconfig
        if not current_pkgconfig
        else user_pkgconfig + os.pathsep + current_pkgconfig
    )

env = Environment(ENV=build_environment, variables=vars)

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

if not conf.CheckPkgConfig("sqlite3"):
    print("Error: sqlite3 development files are required.")
    Exit(1)

conf.env.ParseConfig("pkg-config --cflags --libs sqlite3")

have_cunit = conf.CheckPkgConfig("cunit")

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

if env["DEBUG"]:
    env.Append(CPPDEFINES=["G3DATA2_DEBUG"])
    print("Build mode: DEBUG (G3DATA2_DEBUG enabled)")

sources = ["main.c", "sort.c", "points.c", "drawing.c", "model.c", "datastore.c", "history.c"]
application = env.Program(target="g3data2", source=sources)
Default(application)

if have_cunit:
    test_env = env.Clone()
    test_env.ParseConfig("pkg-config --cflags --libs cunit")
    if test_env.get("LIBPATH"):
        test_env.Append(RPATH=test_env["LIBPATH"])
    test_sources = [
        test_env.Object(target="tests/test_datastore.o", source="tests/test_datastore.c"),
        test_env.Object(target="tests/model.o", source="model.c"),
        test_env.Object(target="tests/datastore.o", source="datastore.c"),
        test_env.Object(target="tests/points.o", source="points.c"),
        test_env.Object(target="tests/sort.o", source="sort.c"),
        test_env.Object(target="tests/history.o", source="history.c"),
    ]
    datastore_tests = test_env.Program(target="test_datastore", source=test_sources)
    test_action = test_env.Command(
        target=".test-datastore-passed",
        source=datastore_tests,
        action="./$SOURCE && touch $TARGET",
    )
    AlwaysBuild(test_action)
    Alias("test", test_action)
else:
    print("Warning: CUnit was not found; the 'test' target is unavailable.")
