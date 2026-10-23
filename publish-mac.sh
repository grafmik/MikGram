#!/bin/bash
# MikGram: publish the Mac build as a GitHub release of grafmik/MikGram.
# 9mp.org/mikgram/ links to releases/latest/download/MikGram-mac.zip, so the
# site never changes: a new release is enough.
#
# Usage: ./publish-mac.sh [path/to/Telegram.app]
# Default app: the last build of the Custom Rebase recipe "mikgram".
set -euo pipefail

REPO=grafmik/MikGram
ASSET=MikGram-mac.zip
BUILD_REPO=~/app/ayugram-build/ayugram
APP=${1:-$BUILD_REPO/out/Release/Telegram.app}

[ -d "$APP" ] || { echo "App introuvable : $APP" >&2; exit 1; }
codesign --verify --deep "$APP"

plist="$APP/Contents/Info.plist"
version=$(defaults read "$plist" CFBundleShortVersionString)
commit=$(git -C "$BUILD_REPO" rev-parse HEAD)
git -C "$BUILD_REPO" fetch -q origin
git -C "$BUILD_REPO" merge-base --is-ancestor "$commit" origin/dev \
	|| { echo "Le commit $commit n'est pas sur origin/dev : pousser d'abord." >&2; exit 1; }
tag="mac-$version-${commit:0:10}"

if gh release view "$tag" -R "$REPO" >/dev/null 2>&1; then
	echo "Déjà publié : $tag (aucun nouveau build)."
	exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ditto "$APP" "$work/MikGram.app"
ditto -c -k --sequesterRsrc --keepParent "$work/MikGram.app" "$work/$ASSET"
size=$(stat -f %z "$work/$ASSET")

notes="MikGram $version pour Mac (Apple Silicon, macOS $(defaults read "$plist" LSMinimumSystemVersion) ou plus récent).
Build du commit ${commit:0:10}. Installation : https://9mp.org/mikgram/"
gh release create "$tag" "$work/$ASSET" -R "$REPO" \
	--target "$commit" \
	--title "MikGram $version (Mac)" \
	--notes "$notes" \
	--latest

# Check the destination, not the upload: the stable link must serve this zip.
served=$(curl -sIL "https://github.com/$REPO/releases/latest/download/$ASSET" \
	| awk 'tolower($1)=="content-length:"{n=$2} END{gsub("\r","",n); print n}')
if [ "$served" != "$size" ]; then
	echo "Le lien stable sert $served octets au lieu de $size." >&2
	exit 1
fi
echo "Publié : $tag ($((size / 1048576)) Mo) — https://github.com/$REPO/releases/latest/download/$ASSET"
