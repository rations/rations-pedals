#!/usr/bin/env bash
# Build the editor's graphics dependencies for 64-bit Windows with MinGW-w64.
#
# WHY THIS EXISTS. On Linux, cairo/pixman/freetype/libpng/zlib are system shared
# libraries and CMakeLists.txt just asks pkg-config for them. Debian packages no
# MinGW build of any of them — the entire mingw-cross library set in the archive
# is zlib plus GnuPG's private dependencies — so a Windows build has to bring its
# own. This script produces that prefix.
#
# STATIC, ON PURPOSE. A VST3 bundle cannot ship these as sibling DLLs: the SDK
# loads the plug-in with a plain LoadLibraryW of the full path
# (public.sdk/source/vst/hosting/module_win32.cpp, loadAsPackage), and the
# default DLL search order does not include the loaded module's own directory,
# so a DLL next to Rations.vst3 would simply not be found. Everything is linked
# in instead, which is also why makedist-windows.sh can assert that the finished
# plug-in imports nothing but system DLLs.
#
# VERSIONS MATCH THE LINUX BUILD, and that is a test requirement rather than
# tidiness: makedist-windows.sh renders all four editor pages with the Windows
# panelrender under Wine and compares them pixel for pixel against the Linux
# render, and a different FreeType would move glyph rasterisation enough to make
# that comparison meaningless. Pinned against what this machine's Debian/Devuan
# actually ships:
#
#   zlib     1.3.1     (zlib1g-dev  1:1.3.dfsg+really1.3.1-1)
#   libpng   1.6.48    (libpng-dev  1.6.48-1)
#   pixman   0.44.0    (libpixman-1-dev 0.44.0-3)
#   freetype 2.13.3    (libfreetype-dev 2.13.3+dfsg-1)
#   cairo    1.18.4    (libcairo2-dev 1.18.4-1)
#
# WHAT IS DELIBERATELY LEFT OUT.
#   * fontconfig - cairo makes it optional on Windows, and Rations never calls
#     it: there is not one Fc* symbol in the tree. FontStack loads its two TTFs
#     by path. cairo-ft.h guards its fontconfig include behind CAIRO_HAS_FC_FONT,
#     so dropping it costs nothing.
#   * xlib / xcb   - the Windows editor blits a DIB section, not an X drawable.
#   * harfbuzz     - the system FreeType here is built without it too (checked
#     with ldd), so leaving it out is what keeps rasterisation identical.
#   * brotli / bzip2 / freetype's png - these only add font *container* formats
#     (WOFF2, compressed PCF, colour bitmap glyphs). Rations loads plain TTFs.
#   * glib, lzo, spectre, dwrite, quartz, tests, docs.
#
# CONSUMERS MUST DEFINE CAIRO_WIN32_STATIC_BUILD. cairo.h declares every entry
# point __declspec(dllimport) unless that macro is set, and cairo's own .pc file
# does not carry it even though this is a static build — so without it every
# cairo call fails to link as "undefined reference to __imp_cairo_*".
# CMakeLists.txt puts it on the PkgConfig::CAIRO imported target.
#
# THE PREFIX IS SHARED WITH THE PARENT PROJECT. The default root below is the
# same one the parent plug-in NAMp's identical script installs to, deliberately:
# the two want byte-identical dependencies, and building them twice would only
# create a way for them to differ. Override it with $RPEDALS_WIN_DEPS_ROOT if
# that is not wanted. The sharing is also why the FreeType patch below tests for
# a project-NEUTRAL sentinel — see the comment at that step.
set -euo pipefail

TRIPLE="${TRIPLE:-x86_64-w64-mingw32}"
ROOT="${RPEDALS_WIN_DEPS_ROOT:-$HOME/third_party/win-deps}"
SYSROOT="${RPEDALS_WIN_SYSROOT:-$ROOT/sysroot}"
DL="$ROOT/dl"
BUILD="$ROOT/build"
JOBS="$(nproc)"

ZLIB=zlib-1.3.1
LIBPNG=libpng-1.6.48
PIXMAN=pixman-0.44.0
FREETYPE=freetype-2.13.3
CAIRO=cairo-1.18.4

# SHA-256 OF EACH RELEASE TARBALL, and the reason the download step is not just a curl.
#
# These five trees ARE the Windows editor. The build gate that compares the Windows render of
# each pedal's face against the Linux one pixel for pixel is only meaningful because both sides
# rasterise through the same FreeType and composite through the same cairo and pixman; a mirror
# that served a different tarball would move glyph rasterisation and turn that comparison into a
# comparison of nothing, or worse, ship altered code inside a plug-in that is otherwise entirely
# auditable from source. Two of the five downloads reach SourceForge, which redirects to
# community-run mirrors, so "it came over HTTPS" says only that some mirror was authentic.
#
# Verified on 2026-09-04, not transcribed from memory: cairo and pixman against the .sha256sum
# and .sha512 files published beside the releases on cairographics.org, and zlib, libpng and
# freetype by re-downloading each from its canonical host (github.com/madler, SourceForge's
# libpng and freetype projects) and comparing the bytes. FreeType additionally publishes a
# detached GPG signature on Savannah, which is the stronger check when that host is reachable;
# it was returning 502 at the time and the re-download stood in for it.
ZLIB_SHA256=38ef96b8dfe510d42707d9c781877914792541133e1870841463bfa73f883e32
LIBPNG_SHA256=46fd06ff37db1db64c0dc288d78a3f5efd23ad9ac41561193f983e20937ece03
PIXMAN_SHA256=89a4c1e1e45e0b23dffe708202cb2eaffde0fe3727d7692b2e1739fec78a7dac
FREETYPE_SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
CAIRO_SHA256=445ed8208a6e4823de1226a74ca319d3600e83f6369f99b14265006599c32ccb

mkdir -p "$DL" "$BUILD" "$SYSROOT/lib/pkgconfig" "$SYSROOT/include"

command -v "$TRIPLE-gcc" >/dev/null || { echo "error: $TRIPLE-gcc not found" >&2; exit 1; }
command -v meson >/dev/null || { echo "error: meson not found" >&2; exit 1; }

# pkg-config must see the sysroot and nothing of the host's.
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_PATH=""

say()  { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
die()  { echo "error: $*" >&2; exit 1; }
done_stamp() { [ -f "$BUILD/.$1.stamp" ]; }
mark()       { touch "$BUILD/.$1.stamp"; }

# A CACHED tarball is checked as well as a freshly fetched one. What has to be true is that the
# bytes being built are the pinned ones, and a stale or edited $DL is as good a way to lose that
# as a substituted mirror; skipping the check on a cache hit would mean the digest only ever
# guarded the very first build on a machine.
fetch() { # url filename sha256
    if [ ! -f "$DL/$2" ]; then
        echo "fetching $2"
        curl -sSL --retry 3 -o "$DL/$2.part" "$1" || die "could not download $2 from $1"
        mv "$DL/$2.part" "$DL/$2"
    fi
    GOT="$(sha256sum "$DL/$2" | awk '{ print $1 }')"
    if [ "$GOT" != "$3" ]; then
        echo "error: $2 does not match the SHA-256 this script pins." >&2
        echo "  expected $3" >&2
        echo "  got      $GOT" >&2
        echo "Delete $DL/$2 and re-run to rule out a truncated download. If it still differs," >&2
        die "a mirror is serving something other than the pinned release; do not build on it."
    fi
}

unpack() { # tarball dirname
    [ -d "$ROOT/$2" ] && return 0
    tar -xf "$DL/$1" -C "$ROOT"
}

#---------------------------------------------------------------------------
# The meson cross file. Generated rather than checked in: every value in it is
# derived from $TRIPLE and $SYSROOT, so a stale copy could only ever be wrong.
#---------------------------------------------------------------------------
CROSS="$BUILD/cross-$TRIPLE.ini"
cat > "$CROSS" <<EOF
[binaries]
c          = '$TRIPLE-gcc'
cpp        = '$TRIPLE-g++'
ar         = '$TRIPLE-ar'
ranlib     = '$TRIPLE-ranlib'
strip      = '$TRIPLE-strip'
windres    = '$TRIPLE-windres'
pkg-config = 'pkg-config'
exe_wrapper = 'wine'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = '$SYSROOT/lib/pkgconfig'

[host_machine]
system     = 'windows'
cpu_family = 'x86_64'
cpu        = 'x86_64'
endian     = 'little'
EOF

MESON_COMMON=(
    --cross-file "$CROSS"
    --prefix "$SYSROOT"
    --libdir lib
    --buildtype release
    --default-library static
)

#---------------------------------------------------------------------------
# zlib. Built through its own win32/Makefile.gcc, which is the path upstream
# documents for MinGW; its CMake build insists on producing a shared library
# too. Installed by hand, including a .pc file, because that makefile has no
# install target that knows about pkg-config.
#---------------------------------------------------------------------------
if ! done_stamp zlib; then
    say "zlib $ZLIB"
    fetch https://github.com/madler/zlib/releases/download/v1.3.1/$ZLIB.tar.xz $ZLIB.tar.xz "$ZLIB_SHA256"
    unpack $ZLIB.tar.xz $ZLIB
    make -C "$ROOT/$ZLIB" -f win32/Makefile.gcc clean >/dev/null 2>&1 || true
    make -C "$ROOT/$ZLIB" -f win32/Makefile.gcc -j"$JOBS" \
         PREFIX="$TRIPLE-" CFLAGS="-O3 -DNDEBUG" libz.a
    install -m644 "$ROOT/$ZLIB/zlib.h" "$ROOT/$ZLIB/zconf.h" "$SYSROOT/include/"
    install -m644 "$ROOT/$ZLIB/libz.a" "$SYSROOT/lib/"
    cat > "$SYSROOT/lib/pkgconfig/zlib.pc" <<EOF
prefix=$SYSROOT
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zlib
Description: zlib compression library
Version: 1.3.1
Libs: -L\${libdir} -lz
Cflags: -I\${includedir}
EOF
    mark zlib
fi

#---------------------------------------------------------------------------
# libpng. Needed by cairo for PNG surfaces (gfx/image.cpp loads every panel
# layer through cairo_image_surface_create_from_png*). Not needed by FreeType,
# whose png option only covers colour bitmap glyphs.
#---------------------------------------------------------------------------
if ! done_stamp libpng; then
    say "libpng $LIBPNG"
    fetch https://downloads.sourceforge.net/project/libpng/libpng16/1.6.48/$LIBPNG.tar.xz $LIBPNG.tar.xz "$LIBPNG_SHA256"
    unpack $LIBPNG.tar.xz $LIBPNG
    cmake -S "$ROOT/$LIBPNG" -B "$BUILD/$LIBPNG" -G Ninja \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER="$TRIPLE-gcc" \
        -DCMAKE_RC_COMPILER="$TRIPLE-windres" \
        -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_FIND_ROOT_PATH="$SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DZLIB_INCLUDE_DIR="$SYSROOT/include" \
        -DZLIB_LIBRARY="$SYSROOT/lib/libz.a" \
        -DPNG_SHARED=OFF -DPNG_STATIC=ON \
        -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_EXECUTABLES=OFF \
        -DPNG_FRAMEWORK=OFF
    cmake --build "$BUILD/$LIBPNG" --parallel "$JOBS"
    cmake --install "$BUILD/$LIBPNG"
    mark libpng
fi

#---------------------------------------------------------------------------
# pixman. cairo's software rasteriser. No external dependencies at all once
# gtk/libpng/tests/demos are off.
#---------------------------------------------------------------------------
if ! done_stamp pixman; then
    say "pixman $PIXMAN"
    fetch https://cairographics.org/releases/$PIXMAN.tar.gz $PIXMAN.tar.gz "$PIXMAN_SHA256"
    unpack $PIXMAN.tar.gz $PIXMAN
    meson setup "$BUILD/$PIXMAN" "$ROOT/$PIXMAN" "${MESON_COMMON[@]}" \
        -Dgtk=disabled -Dlibpng=disabled -Dtests=disabled -Ddemos=disabled \
        -Dopenmp=disabled
    meson compile -C "$BUILD/$PIXMAN" -j "$JOBS"
    meson install -C "$BUILD/$PIXMAN"
    mark pixman
fi

#---------------------------------------------------------------------------
# FreeType. harfbuzz off to match the system build (verified with ldd on
# libfreetype.so.6, which links zlib/bz2/png/brotli but NOT harfbuzz), so glyph
# rasterisation stays identical to the Linux render.
#---------------------------------------------------------------------------
if ! done_stamp freetype; then
    say "freetype $FREETYPE"
    fetch https://downloads.sourceforge.net/project/freetype/freetype2/2.13.3/$FREETYPE.tar.xz $FREETYPE.tar.xz "$FREETYPE_SHA256"
    unpack $FREETYPE.tar.xz $FREETYPE

    # freetype's meson.build defines DLL_EXPORT on Windows unconditionally:
    #     if host_machine.system() == 'windows'
    #       ft2_defines += ['-DDLL_EXPORT=1']
    # with no test of default_library. That makes FT_EXPORT expand to
    # __declspec(dllexport) even in a STATIC build, so every object in
    # libfreetype.a carries a .drectve section full of -export:"FT_..."
    # directives, and the linker honours those in whatever links the archive:
    # the parent project's bundle came out re-exporting 151 FreeType symbols
    # beside its own three entry points. -Wl,--exclude-all-symbols does not
    # help, because a .drectve export is explicit rather than automatic
    # (verified by trying it). makedist-windows.sh's exports gate is what
    # detects a regression here.
    #
    # Removing the define is the fix at the cause. Guarded so that a future
    # freetype release which drops or reshapes these lines fails here loudly
    # rather than silently going back to exporting everything, and idempotent
    # because unpack() keeps an already-extracted tree.
    #
    # THE SENTINEL IS PROJECT-NEUTRAL, AND THAT IS LOAD-BEARING. This prefix is
    # shared with the parent plug-in NAMp, whose copy of this script wrote its
    # own "# NAMp: ..." marker into the very same meson.build. A sentinel naming
    # this project would match neither branch on such a tree — the marker is
    # absent AND the DLL_EXPORT line it would look for has already been
    # replaced — so the script would die on a prefix that is in fact correctly
    # patched. Test for the substring both markers share.
    FT_MESON="$ROOT/$FREETYPE/meson.build"
    FT_PATCH_MARK="# DLL_EXPORT removed by build-win-deps.sh, this is a static build"
    if grep -q "DLL_EXPORT removed" "$FT_MESON"; then
        : # already patched, by this script or by the parent project's copy
    elif grep -q "^  ft2_defines += \['-DDLL_EXPORT=1'\]$" "$FT_MESON"; then
        sed -i "s|^  ft2_defines += \['-DDLL_EXPORT=1'\]$|  $FT_PATCH_MARK|" "$FT_MESON"
    else
        die "$FREETYPE/meson.build no longer has the DLL_EXPORT line this script patches; re-check it"
    fi

    meson setup "$BUILD/$FREETYPE" "$ROOT/$FREETYPE" "${MESON_COMMON[@]}" \
        -Dharfbuzz=disabled -Dbrotli=disabled -Dbzip2=disabled \
        -Dpng=disabled -Dzlib=disabled -Dtests=disabled
    meson compile -C "$BUILD/$FREETYPE" -j "$JOBS"
    meson install -C "$BUILD/$FREETYPE"
    mark freetype
fi

#---------------------------------------------------------------------------
# cairo. freetype must be requested EXPLICITLY: cairo's meson.build does
#     freetype_required = host_machine.system() not in ['windows', 'darwin']
#     freetype_option = freetype_option.disable_auto_if(not freetype_required)
# so the default 'auto' resolves to DISABLED when cross-compiling to Windows,
# and a cairo without cairo-ft would fail to build gfx/fontstack.cpp.
#---------------------------------------------------------------------------
if ! done_stamp cairo; then
    say "cairo $CAIRO"
    fetch https://cairographics.org/releases/$CAIRO.tar.xz $CAIRO.tar.xz "$CAIRO_SHA256"
    unpack $CAIRO.tar.xz $CAIRO
    meson setup "$BUILD/$CAIRO" "$ROOT/$CAIRO" "${MESON_COMMON[@]}" \
        -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled \
        -Dfontconfig=disabled -Ddwrite=disabled -Dquartz=disabled \
        -Dxlib=disabled -Dxcb=disabled -Dxlib-xcb=disabled \
        -Dtee=disabled -Dglib=disabled -Dspectre=disabled -Dlzo=disabled \
        -Dsymbol-lookup=disabled -Dtests=disabled -Dgtk2-utils=disabled \
        -Dgtk_doc=false
    meson compile -C "$BUILD/$CAIRO" -j "$JOBS"
    meson install -C "$BUILD/$CAIRO"
    mark cairo
fi

#---------------------------------------------------------------------------
say "sysroot summary"
echo "prefix: $SYSROOT"
MISSING=0
for m in zlib libpng16 pixman-1 freetype2 cairo cairo-ft; do
    v="$(pkg-config --modversion "$m" 2>/dev/null || echo 'MISSING')"
    printf '  %-12s %s\n' "$m" "$v"
    [ "$v" = "MISSING" ] && MISSING=1
done
echo
echo "static libraries:"
ls -1 "$SYSROOT/lib"/*.a 2>/dev/null | sed 's|^|  |'
if [ "$MISSING" = "1" ]; then
    echo
    die "the sysroot is incomplete - a module above did not install a .pc file"
fi
