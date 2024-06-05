#!/bin/bash -ex

# SPDX-FileCopyrightText: 2019 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

. .ci/scripts/common/pre-upload.sh

APPIMAGETOOL=$(wget -q https://api.github.com/repos/probonopd/go-appimage/releases -O - | sed 's/"/ /g; s/ /\n/g' | grep -o 'https.*continuous.*tool.*86_64.*mage$')
APPIMAGE_NAME="suyu-${RELEASE_NAME}-${GITDATE}-${GITREV}.AppImage"
BASE_NAME="suyu-linux"
REV_NAME="${BASE_NAME}-${GITDATE}-${GITREV}"
ARCHIVE_NAME="${REV_NAME}.tar.xz"
COMPRESSION_FLAGS="-cJvf"

if [ "${RELEASE_NAME}" = "mainline" ] || [ "${RELEASE_NAME}" = "early-access" ]; then
    DIR_NAME="${BASE_NAME}-${RELEASE_NAME}"
else
    DIR_NAME="${REV_NAME}-${RELEASE_NAME}"
fi

mkdir "$DIR_NAME"

cp build/bin/suyu-cmd "$DIR_NAME"
if [ "${RELEASE_NAME}" != "early-access" ] && [ "${RELEASE_NAME}" != "mainline" ]; then
    cp build/bin/suyu "$DIR_NAME"
fi

# Build an AppImage
cd build

wget -nc "$APPIMAGETOOL" -O ./appimagetool-x86_64.AppImage
chmod 755 appimagetool-x86_64.AppImage

# if FUSE is not available, then fallback to extract and run
if ! ./appimagetool-x86_64.AppImage --version; then
    export APPIMAGE_EXTRACT_AND_RUN=1
fi

if [ "${RELEASE_NAME}" = "mainline" ]; then
    # Generate update information if releasing to mainline
	# ./appimagetool-x86_64.AppImage -u "gh-releases-zsync|suyu-emu|suyu-${RELEASE_NAME}|latest|suyu-*.AppImage.zsync" AppDir "${APPIMAGE_NAME}"
	# Go appimagetool needs to have a VERSION and ARCH defined to make the appimage
    ARCH=x86_64 VERSION="${GITREV}" ./appimagetool-x86_64.AppImage -s ./*AppDir && mv ./*suyu*AppImage ./"$APPIMAGE_NAME"
else
	# Go appimagetool needs to have a VERSION and ARCH defined to make the appimage
    ARCH=x86_64 VERSION="${GITREV}" ./appimagetool-x86_64.AppImage -s ./*AppDir && mv ./*suyu*AppImage ./"$APPIMAGE_NAME"
fi
cd ..

# Copy the AppImage and update info to the artifacts directory and avoid compressing it
cp "build/${APPIMAGE_NAME}" "${ARTIFACTS_DIR}/"
if [ -f "build/${APPIMAGE_NAME}.zsync" ]; then
    cp "build/${APPIMAGE_NAME}.zsync" "${ARTIFACTS_DIR}/"
fi

# Copy the AppImage to the general release directory and remove git revision info
if [ "${RELEASE_NAME}" = "mainline" ] || [ "${RELEASE_NAME}" = "early-access" ]; then
    cp "build/${APPIMAGE_NAME}" "${DIR_NAME}/suyu-${RELEASE_NAME}.AppImage"
fi

# Copy debug symbols to artifacts
cd build/bin
tar $COMPRESSION_FLAGS "${ARTIFACTS_DIR}/${REV_NAME}-debug.tar.xz" *.debug
cd -

. .ci/scripts/common/post-upload.sh
