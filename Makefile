.PHONY: all release debug clean check check-format check-cppcheck run bundle verify-bundle sign dmg release-dmg notarize install

BUILD_DIR := src/build
GENERATOR := Ninja
BUILD_JOBS := $(shell \
	if command -v getconf >/dev/null 2>&1; then n=$$(getconf _NPROCESSORS_ONLN); \
	elif command -v nproc >/dev/null 2>&1; then n=$$(nproc); \
	else n=1; fi; \
	echo $$(( n / 2 > 0 ? n / 2 : 1 )))

all: release

release:
	rm -rf $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(BUILD_JOBS)

debug:
	rm -rf $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(BUILD_JOBS)

clean:
	rm -rf $(BUILD_DIR)

check: check-format check-cppcheck

check-format:
	@clang-format-23 --version | grep -E 'clang-format version 23\.'
	find src -path src/build -prune -o \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0 \
	    | xargs -0 clang-format-23 --dry-run --Werror

check-cppcheck:
	@cppcheck --version | grep -F 'Cppcheck 2.21.0'
	cppcheck --error-exitcode=1 --enable=all --inconclusive --std=c++20 \
	    --library=qt \
	    --suppress=missingIncludeSystem \
	    --suppress=missingInclude \
	    --suppress=normalCheckLevelMaxBranches \
	    --suppress=checkersReport \
	    --suppressions-list=.cppcheck_suppressions \
	    -I src -i src/build src

run:
	@if [ "$$(uname -s)" = "Darwin" ]; then \
	    open $(BUILD_DIR)/bin/sdr9700.app; \
	else \
	    ./$(BUILD_DIR)/bin/sdr9700; \
	fi

bundle:
	@if [ "$$(uname -s)" != "Darwin" ]; then \
	    echo "The bundle target is only available on macOS."; \
	    exit 1; \
	fi
	./resources/packaging/macos/scripts/deploy_macos.sh $(BUILD_DIR)/bin/sdr9700.app

verify-bundle:
	./resources/packaging/macos/scripts/verify_macos_bundle.sh $(BUILD_DIR)/bin/sdr9700.app

sign:
	./resources/packaging/macos/scripts/sign_macos.sh $(BUILD_DIR)/bin/sdr9700.app

dmg: bundle
	./resources/packaging/macos/scripts/package_macos.sh $(BUILD_DIR)/bin/sdr9700.app $(BUILD_DIR)/package

release-dmg: bundle sign
	./resources/packaging/macos/scripts/package_macos.sh $(BUILD_DIR)/bin/sdr9700.app $(BUILD_DIR)/package

notarize:
	@if [ -z "$(DMG)" ]; then \
	    echo "Usage: make notarize DMG=src/build/package/sdr9700-<version>-macOS-apple-silicon.dmg"; \
	    exit 1; \
	fi
	./resources/packaging/macos/scripts/notarize_macos.sh "$(DMG)"

install:
	@if [ "$$(uname -s)" = "Darwin" ]; then \
	    echo "The macOS install/package target is not implemented yet."; \
	    exit 1; \
	else \
	    mkdir -p ~/.local/share/applications; \
	    mkdir -p ~/.local/share/icons/hicolor/256x256/apps; \
	    mkdir -p ~/.local/share/icons/hicolor/512x512/apps; \
	    sed 's|^Exec=.*|Exec=$(abspath $(BUILD_DIR)/bin/sdr9700)|' resources/packaging/linux/sdr9700.desktop \
	        > ~/.local/share/applications/sdr9700.desktop; \
	    chmod 644 ~/.local/share/applications/sdr9700.desktop; \
	    install -m 644 resources/images/icons/sdr9700_app_icon_256x256.png \
	        ~/.local/share/icons/hicolor/256x256/apps/sdr9700.png; \
	    install -m 644 resources/images/icons/sdr9700_app_icon_512x512.png \
	        ~/.local/share/icons/hicolor/512x512/apps/sdr9700.png; \
	    update-desktop-database -q ~/.local/share/applications 2>/dev/null || true; \
	    gtk-update-icon-cache -q -f -t ~/.local/share/icons/hicolor 2>/dev/null || true; \
	    kbuildsycoca6 --noincremental 2>/dev/null || true; \
	    privilege=""; \
	    if [ "$$(id -u)" -ne 0 ]; then \
	        if ! command -v sudo >/dev/null 2>&1; then \
	            echo "Installing the RC-28 udev rule requires root access or sudo."; \
	            exit 1; \
	        fi; \
	        privilege="sudo"; \
	    fi; \
	    $$privilege install -d -m 0755 /etc/udev/rules.d; \
	    $$privilege install -m 0644 resources/packaging/linux/60_sdr9700_rc28.rules \
	        /etc/udev/rules.d/60_sdr9700_rc28.rules; \
	    if command -v udevadm >/dev/null 2>&1; then \
	        $$privilege udevadm control --reload-rules; \
	        $$privilege udevadm trigger --subsystem-match=hidraw; \
	    else \
	        echo "Installed the RC-28 udev rule; reconnect the controller after udev rules are reloaded."; \
	    fi; \
	fi
