#!/usr/bin/env bash

set -euo pipefail

version=$1
repo_root=$PWD
packaging_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
package="mp-${version}-macos-arm64"
app="$repo_root/dist/mp.app"
resources="$app/Contents/Resources"
iconset="$repo_root/build/mp.iconset"

strip -x build/mp
rm -rf "$app" "$iconset" build/dmg
mkdir -p "$app/Contents/MacOS" "$resources/licenses/mp" "$iconset"

cp build/mp "$app/Contents/MacOS/mp"
sed "s/@VERSION@/$version/g" "$packaging_dir/Info.plist" \
  > "$app/Contents/Info.plist"
cp README.md "$resources/README.md"
cp LICENSE assets/fonts/OFL-*.txt "$resources/licenses/mp/"

sips -z 16 16 assets/icon.png --out "$iconset/icon_16x16.png" >/dev/null
sips -z 32 32 assets/icon.png --out "$iconset/icon_16x16@2x.png" >/dev/null
sips -z 32 32 assets/icon.png --out "$iconset/icon_32x32.png" >/dev/null
sips -z 64 64 assets/icon.png --out "$iconset/icon_32x32@2x.png" >/dev/null
sips -z 128 128 assets/icon.png --out "$iconset/icon_128x128.png" >/dev/null
sips -z 256 256 assets/icon.png --out "$iconset/icon_128x128@2x.png" >/dev/null
sips -z 256 256 assets/icon.png --out "$iconset/icon_256x256.png" >/dev/null
sips -z 512 512 assets/icon.png --out "$iconset/icon_256x256@2x.png" >/dev/null
sips -z 512 512 assets/icon.png --out "$iconset/icon_512x512.png" >/dev/null
sips -z 1024 1024 assets/icon.png --out "$iconset/icon_512x512@2x.png" >/dev/null
iconutil --convert icns "$iconset" --output "$resources/mp.icns"

dylibbundler -od -b \
  -x "$app/Contents/MacOS/mp" \
  -d "$app/Contents/Frameworks" \
  -p @executable_path/../Frameworks

third_party="$resources/licenses/third-party"
mkdir -p "$third_party"
while IFS= read -r formula; do
  [[ -n $formula ]] || continue
  prefix=$(brew --prefix "$formula")
  destination="$third_party/${formula//\//-}"
  while IFS= read -r license; do
    mkdir -p "$destination"
    cp "$license" "$destination/$(basename "$license")"
  done < <(find -L "$prefix" -maxdepth 3 -type f \
    \( -iname 'COPYING*' -o -iname 'LICENSE*' -o -iname 'NOTICE*' \) \
    2>/dev/null)
done < <(printf '%s\n' glib freetype; brew deps --union glib freetype)

plutil -lint "$app/Contents/Info.plist"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
"$app/Contents/MacOS/mp" -v | grep -Fx "mp $version"

tar -czf "dist/$package.tar.gz" -C dist mp.app
mkdir -p build/dmg
cp -R "$app" build/dmg/
ln -s /Applications build/dmg/Applications
hdiutil create -volname mp -srcfolder build/dmg -ov -format UDZO \
  "dist/$package.dmg"
hdiutil verify "dist/$package.dmg"
