#!/usr/bin/env bash

set -euo pipefail

version=$1
repo_root=$PWD
packaging_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
package="mp-${version}-windows-x86_64"
package_dir="$repo_root/dist/$package"

strip build/mp.exe
mkdir -p "$package_dir/licenses/mp" "$package_dir/licenses/third-party"
cp build/mp.exe LICENSE README.md assets/fonts/OFL-*.txt "$package_dir/"
cp LICENSE assets/fonts/OFL-*.txt "$package_dir/licenses/mp/"
magick assets/icon.png -define icon:auto-resize=256,128,64,48,32,16 \
  "$package_dir/mp.ico"

dll_count=0
while IFS= read -r dll; do
  dll=$(cygpath -u "$dll")
  case $dll in
    "$MINGW_PREFIX"/bin/*.dll)
      cp "$dll" "$package_dir/"
      owner=$(pacman -Qoq "$dll")
      destination="$package_dir/licenses/third-party/$owner"
      while IFS= read -r license; do
        mkdir -p "$destination"
        cp "$license" "$destination/$(basename "$license")"
      done < <(pacman -Qlq "$owner" | \
        grep -E '/share/licenses/[^/]+/[^/]+$' || true)
      dll_count=$((dll_count + 1))
      ;;
  esac
done < <(ntldd --recursive build/mp.exe | \
  sed -n 's/.*=> \(.*\) (0x[0-9A-Fa-f]*).*/\1/p' | sort -u)

if ((dll_count == 0)); then
  echo 'no MSYS2 runtime DLLs found' >&2
  exit 1
fi

"$package_dir/mp.exe" -v | grep -Fx "mp $version"

package_windows=$(cygpath -w "$package_dir")
zip_windows=$(cygpath -w "$repo_root/dist/$package.zip")
powershell.exe -NoProfile -Command \
  "Compress-Archive -Path '$package_windows' -DestinationPath '$zip_windows'"

iscc=$(command -v ISCC.exe || true)
if [[ -z $iscc ]]; then
  iscc='/c/Program Files (x86)/Inno Setup 6/ISCC.exe'
fi

"$iscc" \
  "/DAppVersion=$version" \
  "/DSourceDir=$(cygpath -w "$package_dir")" \
  "/DOutputDir=$(cygpath -w "$repo_root/dist")" \
  "$(cygpath -w "$packaging_dir/mp.iss")"

test -f "dist/$package-setup.exe"
