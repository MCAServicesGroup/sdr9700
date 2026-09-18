#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS packaging script can only run on macOS." >&2
    exit 1
fi

app_path="${1:-src/build/bin/SDR9700.app}"
output_directory="${2:-src/build/package}"
if [ ! -d "${app_path}" ]; then
    echo "SDR9700 application bundle not found at ${app_path}" >&2
    exit 1
fi

version="$(plutil -extract CFBundleShortVersionString raw -o - "${app_path}/Contents/Info.plist")"
output_path="${output_directory}/SDR9700-${version}-macOS-apple-silicon.dmg"
staging_path="$(mktemp -d /tmp/sdr9700-dmg.XXXXXX)"
trap 'rm -rf "${staging_path}"' EXIT HUP INT TERM

mkdir -p "${output_directory}"
/usr/bin/ditto "${app_path}" "${staging_path}/SDR9700.app"
ln -s /Applications "${staging_path}/Applications"

rm -f "${output_path}"
hdiutil create \
    -volname "SDR9700" \
    -srcfolder "${staging_path}" \
    -format UDZO \
    -ov \
    "${output_path}"

if [ -n "${SDR9700_SIGN_IDENTITY:-}" ]; then
    codesign --force --timestamp --sign "${SDR9700_SIGN_IDENTITY}" "${output_path}"
    codesign --verify --verbose=2 "${output_path}"
fi

echo "Created Apple Silicon disk image: ${output_path}"
