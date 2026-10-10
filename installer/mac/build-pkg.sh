#!/bin/bash
# Builds BroadcastMaster-macOS-Installer.pkg from the built VST3 (run on macOS, from the repo root).
#   installer/mac/build-pkg.sh <version> <path to BroadcastMaster.vst3> <output.pkg>
set -euo pipefail
version="$1"; vst3="$2"; out="$3"
work=$(mktemp -d)
mkdir -p "$work/root"
ditto "$vst3" "$work/root/BroadcastMaster.vst3"
xattr -cr "$work/root/BroadcastMaster.vst3" || true

# install exactly into /Library/Audio/Plug-Ins/VST3 (never "relocate" to wherever an old copy lives)
pkgbuild --analyze --root "$work/root" "$work/components.plist"
echo "components.plist:"; cat "$work/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$work/components.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$work/components.plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$work/components.plist"
    i=$((i + 1))
done
pkgbuild --root "$work/root" --component-plist "$work/components.plist" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         --scripts "installer/mac/scripts" \
         --identifier "com.broadcastmaster.broadcastmaster.vst3" --version "$version" \
         "$work/BroadcastMaster-component.pkg"

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>BroadcastMaster $version</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default" title="BroadcastMaster VST3"><pkg-ref id="com.broadcastmaster.broadcastmaster.vst3"/></choice>
    <pkg-ref id="com.broadcastmaster.broadcastmaster.vst3" version="$version">BroadcastMaster-component.pkg</pkg-ref>
</installer-gui-script>
XML
mkdir -p "$work/resources"
cat > "$work/resources/welcome.html" <<HTML
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>BroadcastMaster $version</h2>
<p>Installs the BroadcastMaster VST3 plug-in into <b>/Library/Audio/Plug-Ins/VST3</b> for all users.</p>
<p>Any older BroadcastMaster copies (including duplicates such as "BroadcastMaster 2.vst3") are removed first, so your host always loads this version.</p>
<p>Quit LiveProfessor, OBS or any other host before continuing, then rescan plug-ins afterwards.</p>
</body></html>
HTML
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" \
             --package-path "$work" "$out"
rm -rf "$work"
echo "Built $out"
