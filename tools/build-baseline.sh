#!/usr/bin/env bash
set -euo pipefail
mkdir -p build/baseline
cd build/baseline
export DXB_REPO=https://github.com/blacktop/darwin-xnu-build
export DXB_SHA=663906528d1cd5ea8fde7de6f16de73db9511549
export MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET
git init -q sweet
git -C sweet fetch --depth 1 https://github.com/dipa-studio/ios-sweet.git fdad0de8dc0e75c5ea84762adc03f7232dc91f19
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
cp dxb/fakeroot/System/Library/Kernels/kernel.* ../../out-baseline/
file ../../out-baseline/kernel.* > ../../out-baseline/file.txt
(cd ../../out-baseline && shasum -a 256 kernel.* > SHA256SUMS)
