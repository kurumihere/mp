#!/usr/bin/env bash

set -euo pipefail

version=$1
repo_root=$PWD
packaging_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
package="mp-${version}-linux-x86_64"
appdir="$repo_root/build/AppDir"

export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git show -s --format=%ct HEAD)}

strip build/mp
desktop-file-validate "$packaging_dir/io.github.kurumihere.mp.desktop"
appstreamcli validate --no-net \
  "$packaging_dir/io.github.kurumihere.mp.metainfo.xml"
cp "$packaging_dir/io.github.kurumihere.mp.desktop" build/
cp "$packaging_dir/io.github.kurumihere.mp.metainfo.xml" build/

mkdir -p "dist/$package"
cp build/mp LICENSE README.md assets/fonts/OFL-*.txt "dist/$package/"
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 \
  --numeric-owner -czf "dist/$package.tar.gz" -C dist "$package"

nfpm_version=2.47.0
nfpm_archive="build/nfpm_${nfpm_version}_Linux_x86_64.tar.gz"
curl --fail --location --retry 3 \
  "https://github.com/goreleaser/nfpm/releases/download/v${nfpm_version}/nfpm_${nfpm_version}_Linux_x86_64.tar.gz" \
  --output "$nfpm_archive"
echo "0660ca602b2d2d2ae4781a06c692b3eeb9d437ffea05b831d76e41f4a3188783  $nfpm_archive" | \
  sha256sum --check
tar -xzf "$nfpm_archive" -C build nfpm

MP_VERSION=$version build/nfpm pkg --config "$packaging_dir/nfpm.yaml" \
  --packager deb --target "dist/$package.deb"
MP_VERSION=$version build/nfpm pkg --config "$packaging_dir/nfpm.yaml" \
  --packager rpm --target "dist/$package.rpm"

rm -rf "$appdir"
mkdir -p \
  "$appdir/usr/share/doc/mp-player" \
  "$appdir/usr/share/licenses/mp-player" \
  "$appdir/usr/share/licenses/third-party" \
  "$appdir/usr/share/metainfo"
cp README.md "$appdir/usr/share/doc/mp-player/"
cp LICENSE assets/fonts/OFL-*.txt "$appdir/usr/share/licenses/mp-player/"
cp "$packaging_dir/io.github.kurumihere.mp.metainfo.xml" \
  "$appdir/usr/share/metainfo/io.github.kurumihere.mp.appdata.xml"

for dependency in \
  libasound2 libfreetype6 libglib2.0-0 libgl1 libx11-6 libxcursor1 \
  libxext6 libxi6 libxinerama1 libxrandr2; do
  copyright="/usr/share/doc/$dependency/copyright"
  if [[ -f $copyright ]]; then
    mkdir -p "$appdir/usr/share/licenses/third-party/$dependency"
    cp -L "$copyright" \
      "$appdir/usr/share/licenses/third-party/$dependency/"
  fi
done

linuxdeploy="build/linuxdeploy-x86_64.AppImage"
appimage_plugin="build/linuxdeploy-plugin-appimage-x86_64.AppImage"
appimage_runtime="build/runtime-x86_64"
curl --fail --location --retry 3 \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
  --output "$linuxdeploy"
curl --fail --location --retry 3 \
  https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-appimage-x86_64.AppImage \
  --output "$appimage_plugin"
curl --fail --location --retry 3 \
  https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
  --output "$appimage_runtime"
echo "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d  $linuxdeploy" | \
  sha256sum --check
echo "992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3  $appimage_plugin" | \
  sha256sum --check
echo "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d  $appimage_runtime" | \
  sha256sum --check
chmod +x "$linuxdeploy" "$appimage_plugin"
convert assets/icon.png -resize 512x512 build/mp.png

export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT="$repo_root/dist/$package.AppImage"
export LDAI_RUNTIME_FILE="$repo_root/$appimage_runtime"
export LINUXDEPLOY_OUTPUT_VERSION=$version
export NO_STRIP=1
export PATH="$repo_root/build:$PATH"

"$linuxdeploy" \
  --appdir "$appdir" \
  --executable "$repo_root/build/mp" \
  --desktop-file "$packaging_dir/io.github.kurumihere.mp.desktop" \
  --icon-file "$repo_root/build/mp.png" \
  --output appimage

"$LDAI_OUTPUT" -v | grep -Fx "mp $version"
test -s "dist/$package.deb"
test -s "dist/$package.rpm"
