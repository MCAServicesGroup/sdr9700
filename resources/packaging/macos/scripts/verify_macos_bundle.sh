#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS bundle verification script can only run on macOS." >&2
    exit 1
fi

app_path="${1:-src/build/bin/sdr9700.app}"
contents_path="${app_path}/Contents"
frameworks_path="${contents_path}/Frameworks"

if [ ! -x "${contents_path}/MacOS/sdr9700" ]; then
    echo "sdr9700 application bundle not found at ${app_path}" >&2
    exit 1
fi

errors_file="$(mktemp /tmp/sdr9700-bundle-errors.XXXXXX)"
trap 'rm -f "${errors_file}"' EXIT HUP INT TERM

for required_plugin in \
    "${contents_path}/PlugIns/platforms/libqcocoa.dylib" \
    "${contents_path}/PlugIns/multimedia/libdarwinmediaplugin.dylib" \
    "${contents_path}/PlugIns/iconengines/libqsvgicon.dylib"; do
    if [ ! -f "${required_plugin}" ]; then
        echo "Missing required Qt plugin: ${required_plugin}" >>"${errors_file}"
    fi
done

if [ ! -d "${frameworks_path}/QtSvg.framework" ]; then
    echo "Missing required Qt framework: ${frameworks_path}/QtSvg.framework" >>"${errors_file}"
fi

while IFS= read -r binary_path; do
    if ! file "${binary_path}" | grep -q "Mach-O"; then
        continue
    fi

    otool -L "${binary_path}" | awk 'NR > 1 { print $1 }' | while IFS= read -r dependency; do
        case "${dependency}" in
        /System/Library/* | /usr/lib/* | @loader_path/*)
            ;;
        @executable_path/../Frameworks/*)
            relative_path="${dependency#@executable_path/../Frameworks/}"
            if [ ! -e "${frameworks_path}/${relative_path}" ]; then
                echo "${binary_path}: missing ${dependency}" >>"${errors_file}"
            fi
            ;;
        @rpath/*)
            relative_path="${dependency#@rpath/}"
            if [ ! -e "${frameworks_path}/${relative_path}" ]; then
                echo "${binary_path}: missing ${dependency}" >>"${errors_file}"
            fi
            ;;
        /*)
            echo "${binary_path}: external dependency ${dependency}" >>"${errors_file}"
            ;;
        *)
            echo "${binary_path}: unsupported dependency ${dependency}" >>"${errors_file}"
            ;;
        esac
    done

    install_id="$(otool -D "${binary_path}" 2>/dev/null | tail -n +2 | head -n 1)"
    if [ -n "${install_id}" ]; then
        case "${install_id}" in
        @rpath/* | @loader_path/* | @executable_path/*)
            ;;
        *)
            echo "${binary_path}: external install ID ${install_id}" >>"${errors_file}"
            ;;
        esac
    fi

    otool -l "${binary_path}" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { reading_rpath = 1; next }
        reading_rpath && $1 == "path" {
            print $2
            reading_rpath = 0
        }
    ' | while IFS= read -r rpath; do
        case "${rpath}" in
        @loader_path/* | @executable_path/*)
            ;;
        *)
            echo "${binary_path}: external rpath ${rpath}" >>"${errors_file}"
            ;;
        esac
    done
done <<EOF
$(find "${contents_path}" -type f)
EOF

if [ -s "${errors_file}" ]; then
    echo "The application bundle is not self-contained:" >&2
    cat "${errors_file}" >&2
    exit 1
fi

echo "Verified self-contained application bundle: ${app_path}"
