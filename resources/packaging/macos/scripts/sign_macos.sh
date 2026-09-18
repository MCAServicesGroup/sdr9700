#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS signing script can only run on macOS." >&2
    exit 1
fi

app_path="${1:-src/build/bin/SDR9700.app}"
sign_identity="${SDR9700_SIGN_IDENTITY:-}"
entitlements_path="resources/packaging/macos/SDR9700.entitlements"

if [ -z "${sign_identity}" ]; then
    echo "Set SDR9700_SIGN_IDENTITY to a Developer ID Application identity." >&2
    exit 1
fi
if [ ! -d "${app_path}" ]; then
    echo "SDR9700 application bundle not found at ${app_path}" >&2
    exit 1
fi

# Sign Mach-O code from the inside out. Framework bundles and the application
# bundle are signed after their contained code so later steps do not invalidate
# an enclosing signature.
while IFS= read -r binary_path; do
    if file "${binary_path}" | grep -q "Mach-O"; then
        codesign --force --options runtime --timestamp --sign "${sign_identity}" "${binary_path}"
    fi
done <<EOF
$(find "${app_path}/Contents/Frameworks" "${app_path}/Contents/PlugIns" -type f)
EOF

while IFS= read -r framework_path; do
    codesign --force --options runtime --timestamp --sign "${sign_identity}" "${framework_path}"
done <<EOF
$(find "${app_path}/Contents/Frameworks" -type d -name '*.framework')
EOF

codesign \
    --force \
    --options runtime \
    --timestamp \
    --entitlements "${entitlements_path}" \
    --sign "${sign_identity}" \
    "${app_path}"

codesign --verify --deep --strict --verbose=2 "${app_path}"
echo "Signed application with hardened runtime: ${app_path}"
