#!/bin/bash
# Build Raven from this checkout and install PDF-XChange Editor to run on it, for the current user.
#   contrib/pdfx/setup-host.sh [path/to/PXCEditor11_Portable_x64.zip]
# Run from a checkout of the pdfx-work branch (git switch --detach pdfx-work). Nothing outside this checkout's build-pdfx,
# components and components-pdfx folders, ~/.local/share/raven/pdfx-program, ~/.local/bin and ~/.local/share/applications is written.
#   JOBS=n       parallel build jobs (default: the number of CPUs, at most 8)
#   BUILD_DIR=d  where to build (default: build-pdfx in this checkout; a full build takes about 10 GB)
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
zip=${1:-$repo/pdfx/PXCEditor11_Portable_x64.zip}
jobs=${JOBS:-$(n=$(nproc); echo $(( n > 8 ? 8 : n )))}
components=$repo/components
# Pieces rebuilt for this branch may sit in components-pdfx beside the usual folder; each is used from there when it is.
part() { if [[ -e $repo/components-pdfx/$1 ]]; then echo "$repo/components-pdfx/$1"; else echo "$components/$1"; fi; }
installer=$(part installer); installer32=$(part installer32); controls=$(part controls); controls32=$(part controls32); libpng=$(part libpng)
exports() { x86_64-w64-mingw32-objdump -p "$1" 2>/dev/null || i686-w64-mingw32-objdump -p "$1" 2>/dev/null || true; }
build=${BUILD_DIR:-$repo/build-pdfx}
program=$HOME/.local/share/raven/pdfx-program
step() { printf '\n==> %s\n' "$*"; }
cd "$repo"

[[ -f $zip ]] || { echo "PDF-XChange portable zip not found: $zip" >&2; exit 1; }
[[ -f scripts/provision-installer.py && -f contrib/pdfx/pdfx ]] || { echo "run this from the pdfx-work checkout" >&2; exit 1; }

# The components the branch needs that an older provisioning lacks: the script hosts of the installer set, and the drop calls (DragQueryFileW) in the shell.
if [[ ! -f $installer/scrrun.dll || ! -f $installer/wshom.ocx ]]; then
    step "Building the 64-bit installer components (a few minutes)"
    python3 scripts/provision-installer.py --output "$installer"
fi
if [[ ! -f $installer32/scrrun.dll || ! -f $installer32/wshom.ocx ]]; then
    step "Building the 32-bit installer components (a few minutes)"
    python3 scripts/provision-installer.py --arch x86 --output "$installer32"
fi
if ! exports "$controls/raven-shell.dll" | grep -qw DragQueryFileW || ! exports "$controls32/raven-shell.dll" | grep -qw DragQueryFileW; then
    step "Rebuilding the shell component"
    python3 scripts/provision-controls.py --components shell --output "$controls" >/dev/null
    python3 scripts/provision-controls.py --arch x86 --components shell --output "$controls32" >/dev/null
fi
if [[ ! -f $libpng/install/lib/libpng16.a ]]; then
    step "Building libpng"
    python3 scripts/provision-libpng.py --output "$libpng" >/dev/null
fi
for needed in rtl/raven-rtl.dll text/cpu-fonts/libskia.a text/fonts graphics/dxvk/pe64 roots; do
    [[ -e $components/$needed ]] || { echo "missing component $components/$needed: provision it as the top-level README shows" >&2; exit 1; }
done

step "Configuring Raven in $build"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DRAVEN_RTL_COMPONENT_PATH="$components/rtl/raven-rtl.dll" \
    -DRAVEN_CONTROLS_COMPONENT_DIR="$controls" -DRAVEN_CONTROLS32_COMPONENT_DIR="$controls32" \
    -DRAVEN_INSTALLER_COMPONENT_DIR="$installer" -DRAVEN_INSTALLER32_COMPONENT_DIR="$installer32" \
    -DRAVEN_DXVK_PE64_DIR="$components/graphics/dxvk/pe64" -DRAVEN_DXVK_PE32_DIR="$components/graphics/dxvk/pe32" \
    -DRAVEN_VKD3D_PE64_DIR="$components/graphics/vkd3d-proton/pe64" -DRAVEN_ROOT_CERTIFICATE_DIRECTORY="$components/roots" \
    -DRAVEN_SKIA_SOURCE="$components/text/skia" -DRAVEN_SKIA_LIBRARY="$components/text/cpu-fonts/libskia.a" \
    -DRAVEN_FONT_DIRECTORY="$components/text/fonts" \
    -DPNG_LIBRARY_RELEASE="$libpng/install/lib/libpng16.a" -DPNG_PNG_INCLUDE_DIR="$libpng/install/include" >/dev/null

step "Building Raven with $jobs jobs (the first build takes a while)"
cmake --build "$build" -j"$jobs"

if [[ ! -f $program/PXCEditor.exe ]]; then
    step "Unpacking PDF-XChange Editor into $program"
    mkdir -p "$program"
    # The archive names its folders with backslashes, which unzip converts and warns about (status 1).
    unzip -q -o "$zip" -d "$program" || [[ $? -eq 1 ]]
    chmod -R u+rwX "$program"
fi

step "Installing the pdfx command and the menu entry"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
commit=$(git -C "$repo" log -1 --format='%h %cs' 2>/dev/null || echo unknown)
sed -e "s|^RAVEN=\${RAVEN:-.*}|RAVEN=\${RAVEN:-$build/native/raven}|" -e "s|^PDFX_BUILD=unknown|PDFX_BUILD='$commit'|" \
    -e "s|^log_dir=\${PDFX_LOG_DIR:-.*}|log_dir=\${PDFX_LOG_DIR:-$(dirname "$repo")/pdfx-logs}|" contrib/pdfx/pdfx > "$HOME/.local/bin/pdfx"
install -m755 contrib/pdfx/pdfx-print "$HOME/.local/bin/pdfx-print"
chmod 755 "$HOME/.local/bin/pdfx"
sed "s|^Exec=pdfx|Exec=$HOME/.local/bin/pdfx|" contrib/pdfx/pdfx.desktop > "$HOME/.local/share/applications/pdfx-raven.desktop"
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

step "Done"
echo "Start it from the menu (PDF-XChange Editor (Raven)), or run: $HOME/.local/bin/pdfx [file.pdf]"
echo "Each run logs to $(dirname "$repo")/pdfx-logs (latest.log is the newest)."
