#!/bin/sh
# Builds the Debian package from the AppDir linuxdeploy fills for the
# AppImage. Only Qt, its ICU and the FFmpeg it plays videos with come
# along, in /opt/cache: distributions ship a Qt too old for it. Every
# other library comes from the system and becomes a dependency, which
# dpkg-shlibdeps works out, so this has to run on the oldest system the
# package is for.
#
# Usage: build-deb.sh <AppDir> <version> <output.deb>
set -eu

if [ $# -ne 3 ]; then
    echo "Usage: $0 <AppDir> <version> <output.deb>" >&2
    exit 2
fi
appdir=$1
version=$2
output=$3
logo=$(cd "$(dirname "$0")/../../logo" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Under debian/ dpkg-shlibdeps takes the libraries in it as the package's own
root=$work/debian/cache
prefix=$root/opt/cache

mkdir -p "$prefix/lib" "$root/usr/bin" "$root/DEBIAN"
for dir in bin plugins qml translations; do
    if [ -d "$appdir/usr/$dir" ]; then
        cp -a "$appdir/usr/$dir" "$prefix/"
    fi
done
cp -a "$appdir"/usr/lib/libQt6* "$prefix/lib/"
for lib in libicu libavcodec libavformat libavutil libswresample libswscale; do
    cp -a "$appdir"/usr/lib/$lib* "$prefix/lib/" 2>/dev/null || true
done
ln -s /opt/cache/bin/cache "$root/usr/bin/cache"
install -Dm644 "$logo/cache.desktop" "$root/usr/share/applications/cache.desktop"
install -Dm644 "$logo/cache.svg" "$root/usr/share/icons/hicolor/scalable/apps/cache.svg"
chmod -R u+rwX,go+rX,go-w "$root"

printf 'Source: cache\n\nPackage: cache\nArchitecture: amd64\n' > "$work/debian/control"
libs=$(cd "$work" && find debian/cache/opt -type f \( -name '*.so*' -o -path '*/bin/cache' \) \
    -exec dpkg-shlibdeps -O -l"$prefix/lib" {} + | sed -n 's/^shlibs:Depends=//p')

cat > "$root/DEBIAN/control" <<CONTROL
Package: cache
Version: $version
Architecture: amd64
Maintainer: Henning Gruendl <henning.gruendl@qt.io>
Installed-Size: $(du -sk "$root" | cut -f1)
Depends: $libs
Section: editors
Priority: optional
Homepage: https://iam-peter.github.io/cache/
Description: Markdown notes on an infinite canvas
 Notes, images and videos on an infinite canvas, one project per folder
 and every note a plain Markdown file. Link notes into mindmaps, add
 checklists, code, shapes and sections, search all projects at once and
 copy a note with its images into Teams or mail.
CONTROL

dpkg-deb --root-owner-group -Zxz -b "$root" "$output"
