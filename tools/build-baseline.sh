#!/usr/bin/env bash
set -euo pipefail
# Read the same pinned source used by experiment receipts before changing directory.
SWEET_REPO=$(python3 -c 'import json; print(json.load(open("sources.lock.json"))["iosSweet"]["repository"])')
SWEET_SHA=$(python3 -c 'import json; print(json.load(open("sources.lock.json"))["iosSweet"]["commit"])')
DXB_REPOSITORY=$(python3 -c 'import json; print(json.load(open("sources.lock.json"))["darwinXnuBuild"]["repository"])')
DXB_COMMIT=$(python3 -c 'import json; print(json.load(open("sources.lock.json"))["darwinXnuBuild"]["commit"])')
mkdir -p build/baseline
cd build/baseline
export DXB_REPO="https://github.com/$DXB_REPOSITORY"
export DXB_SHA="$DXB_COMMIT"
export MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET
git init -q sweet
git -C sweet fetch --depth 1 "https://github.com/$SWEET_REPO.git" "$SWEET_SHA"
git -C sweet checkout --detach FETCH_HEAD
ln -s sweet/xnu xnu
git init -q dxb
git -C dxb fetch -q --depth 1 "$DXB_REPO" "$DXB_SHA"
git -C dxb checkout -q FETCH_HEAD
# 12.5-14.3 use the top-level patches dir; ours go next to blacktop's
cp xnu/patches/*.patch xnu/patches/*.sh dxb/patches/
# libfirehose_kernel is built arm64e-only; the SWEET kernel is plain arm64
sed -i '' 's/ARCHS="x86_64 arm64e" VALID_ARCHS="x86_64 arm64e"/ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"/' dxb/build.sh
grep -c 'VALID_ARCHS="x86_64 arm64e arm64"' dxb/build.sh
ls dxb/patches
brew install jq gum bash cmake ninja
# build.sh only checks that xcodes exists (Xcode is preinstalled); no bottle on macOS 14
printf '#!/bin/sh\nexit 0\n' | sudo tee /usr/local/bin/xcodes >/dev/null
sudo chmod +x /usr/local/bin/xcodes
sw_vers
ls -d /Applications/Xcode*.app
XC=$(ls -d /Applications/Xcode_*.app | sort -V | head -1)
echo "DEVELOPER_DIR=$XC/Contents/Developer" >> "$GITHUB_ENV"
sudo xcode-select -s "$XC"
xcodebuild -version
xcrun --find iig
(cd dxb && bash build.sh)
mkdir -p ../../out-baseline
find dxb/fakeroot/System/Library/Kernels -maxdepth 1 -type f -name 'kernel.*' -exec cp {} ../../out-baseline/ \;
file ../../out-baseline/kernel.* > ../../out-baseline/file.txt
# Use the upstream static Mach-O/signature writer, not a host loader. The
# resulting executable itself is mockfs's RAM disk served as /sbin/launchd.
clang --target=arm64-apple-ios16.0 -c ../../guests/init.S -o ../../out-baseline/init.o
python3 sweet/tools/mkinit.py ../../out-baseline/init.o ../../out-baseline/init.macho
mkdir -p ../../out-baseline/reference-tools
for source in adt.py mkadt.py mkpayload.py qemu-sweet.py; do
    cp "sweet/tools/$source" "../../out-baseline/reference-tools/$source"
done
(cd ../../out-baseline && shasum -a 256 kernel.* init.macho reference-tools/*.py > SHA256SUMS)
