#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS notarization script can only run on macOS." >&2
    exit 1
fi

dmg_path="${1:-}"
notary_profile="${SDR9700_NOTARY_PROFILE:-}"
if [ -z "${dmg_path}" ] || [ ! -f "${dmg_path}" ]; then
    echo "Pass the signed SDR9700 DMG path as the first argument." >&2
    exit 1
fi
if [ -z "${notary_profile}" ]; then
    echo "Set SDR9700_NOTARY_PROFILE to a notarytool keychain profile." >&2
    exit 1
fi

xcrun notarytool submit "${dmg_path}" --keychain-profile "${notary_profile}" --wait
xcrun stapler staple "${dmg_path}"
xcrun stapler validate "${dmg_path}"

echo "Notarized and stapled disk image: ${dmg_path}"
