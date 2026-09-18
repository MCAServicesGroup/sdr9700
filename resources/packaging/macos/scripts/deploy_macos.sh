#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS deployment script can only run on macOS." >&2
    exit 1
fi

app_path="${1:-src/build/bin/SDR9700.app}"
contents_path="${app_path}/Contents"
plugins_path="${contents_path}/PlugIns"

if [ ! -x "${contents_path}/MacOS/SDR9700" ]; then
    echo "SDR9700 application bundle not found at ${app_path}" >&2
    exit 1
fi

deployqt_path="$(command -v macdeployqt || true)"
if [ -z "${deployqt_path}" ]; then
    echo "macdeployqt is required on the build machine." >&2
    exit 1
fi

# Let Qt's deployment tool select plugins for this Qt build. Keep an existing
# plugin directory when redeploying an already-rewritten bundle: macdeployqt
# cannot rediscover every plugin after the executable no longer references the
# original Qt installation. Clean release builds begin without this directory.
deploy_log="$(mktemp /tmp/sdr9700-macdeployqt.XXXXXX)"
run_macdeployqt()
{
    if "${deployqt_path}" -help 2>&1 | grep -q -- '-no-codesign'; then
        "${deployqt_path}" "${app_path}" \
            -verbose=0 \
            -always-overwrite \
            -no-codesign
    else
        # Qt 6.8's macdeployqt does not expose -no-codesign. Any signatures it
        # creates are intentionally invalidated below and replaced only after
        # the bundle's load commands have reached their final state.
        "${deployqt_path}" "${app_path}" \
            -verbose=0 \
            -always-overwrite
    fi
}

if ! run_macdeployqt >"${deploy_log}" 2>&1; then
    cat "${deploy_log}" >&2
    rm -f "${deploy_log}"
    exit 1
fi
rm -f "${deploy_log}"

frameworks_path="${contents_path}/Frameworks"
install_name_log="$(mktemp /tmp/sdr9700-install-name.XXXXXX)"
trap 'rm -f "${install_name_log}"' EXIT HUP INT TERM

run_install_name_tool()
{
    if ! install_name_tool "$@" 2>"${install_name_log}"; then
        cat "${install_name_log}" >&2
        return 1
    fi
    : >"${install_name_log}"
}

# Homebrew's Qt plugin tree can contain plugins for separately packaged Qt
# modules. macdeployqt copies those plugins even when their framework is not
# installed, leaving unusable code in the bundle and printing unresolved-rpath
# diagnostics. SDR9700 does not use these optional plugin families.
if [ ! -e "${frameworks_path}/QtVirtualKeyboard.framework" ]; then
    rm -f "${plugins_path}/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
fi
if [ ! -e "${frameworks_path}/QtSvg.framework" ]; then
    rm -f "${plugins_path}/iconengines/libqsvgicon.dylib"
fi
if [ ! -e "${frameworks_path}/QtPdf.framework" ]; then
    rm -f "${plugins_path}/imageformats/libqpdf.dylib"
fi

# macdeployqt copies every SQL driver from the Qt installation. SDR9700 uses
# only SQLite; the other drivers can retain load commands for database client
# libraries that are neither required nor bundled.
if [ -d "${plugins_path}/sqldrivers" ]; then
    for sql_driver in "${plugins_path}/sqldrivers"/*; do
        [ -e "${sql_driver}" ] || continue
        if [ "$(basename "${sql_driver}")" != "libqsqlite.dylib" ]; then
            rm -f "${sql_driver}"
        fi
    done
fi

# Copied Homebrew binaries can arrive with signatures and build-machine rpaths.
# Let install_name_tool invalidate those signatures while changing the load
# commands. Removing a signature first with the older codesign shipped on some
# GitHub macOS runners can leave newer Homebrew binaries with an unprocessable
# __LINKEDIT layout. The complete bundle is signed after all metadata is final.
while IFS= read -r binary_path; do
    if file "${binary_path}" | grep -q "Mach-O"; then
        architectures="$(lipo -archs "${binary_path}")"
        if [ "${architectures}" != "arm64" ]; then
            case " ${architectures} " in
            *" arm64 "*)
                thinned_path="${binary_path}.arm64"
                lipo "${binary_path}" -thin arm64 -output "${thinned_path}"
                chmod "$(stat -f '%Lp' "${binary_path}")" "${thinned_path}"
                mv "${thinned_path}" "${binary_path}"
                ;;
            *)
                echo "${binary_path} does not contain an arm64 slice." >&2
                exit 1
                ;;
            esac
        fi

        install_id="$(otool -D "${binary_path}" 2>/dev/null | tail -n +2 | head -n 1)"
        absolute_rpaths="$(otool -l "${binary_path}" | awk '
            $1 == "cmd" && $2 == "LC_RPATH" { reading_rpath = 1; next }
            reading_rpath && $1 == "path" {
                if ($2 ~ /^\//) {
                    print $2
                }
                reading_rpath = 0
            }
        ')"

        if [ -n "${install_id}" ] && echo "${install_id}" | grep -q '^/'; then
            case "${binary_path}" in
            "${frameworks_path}"/*)
                relative_path="${binary_path#"${frameworks_path}/"}"
                run_install_name_tool -id "@rpath/${relative_path}" "${binary_path}"
                ;;
            "${plugins_path}"/*)
                run_install_name_tool -id "@loader_path/$(basename "${binary_path}")" "${binary_path}"
                ;;
            esac
        fi

        if [ -n "${absolute_rpaths}" ]; then
            echo "${absolute_rpaths}" | while IFS= read -r rpath; do
                if [ -n "${rpath}" ]; then
                    run_install_name_tool -delete_rpath "${rpath}" "${binary_path}"
                fi
            done
        fi
    fi
done <<EOF
$(find "${frameworks_path}" "${plugins_path}" -type f)
EOF

# The main executable can also inherit Homebrew link directories from
# pkg-config dependencies. It has no install ID, but its absolute rpaths must
# be removed just like those in copied libraries.
main_executable="${contents_path}/MacOS/SDR9700"
main_absolute_rpaths="$(otool -l "${main_executable}" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { reading_rpath = 1; next }
    reading_rpath && $1 == "path" {
        if ($2 ~ /^\//) {
            print $2
        }
        reading_rpath = 0
    }
')"
if [ -n "${main_absolute_rpaths}" ]; then
    echo "${main_absolute_rpaths}" | while IFS= read -r rpath; do
        if [ -n "${rpath}" ]; then
            run_install_name_tool -delete_rpath "${rpath}" "${main_executable}"
        fi
    done
fi

# Reject missing bundled dependencies and every non-system absolute load path,
# install ID, or rpath before any signature can hide a packaging error.
"$(dirname "$0")/verify_macos_bundle.sh" "${app_path}"

# Sign only after all bundle contents and load commands are final. This ad-hoc
# signature is for local testing; release signing is a separate packaging step.
codesign --force --deep --sign - "${app_path}"
codesign --verify --deep --strict "${app_path}"

echo "Created self-contained Apple Silicon bundle: ${app_path}"
