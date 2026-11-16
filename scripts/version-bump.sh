#!/usr/bin/env bash
# Frequent semantic-version bump for Agent EJ (core) and EJ Manager (android).
#
#   scripts/version-bump.sh <feat|fix|major|breaking> "<summary>" [--android] [--same-day]
#
# One release per line (core / android) per day; a second one the same day is
# refused unless --same-day is given.
#
# feat  -> minor  (x.n.x)   changelog section: Added
# fix   -> patch  (x.x.n)   changelog section: Fixed
# major -> major  (1.x.x)   changelog section: Changed   (LTS / architectural)
#
# The bump writes the changelog section, updates the app version when --android
# is given, commits as `chore(release): vX.Y.Z` and creates the matching tag.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

KIND="${1:-}"
SUMMARY="${2:-}"
TARGET="core"

if [ -z "$KIND" ] || [ -z "$SUMMARY" ]; then
    echo "usage: $0 <feat|fix|major|breaking> \"<summary>\" [--android] [--same-day]" >&2
    exit 2
fi
SAME_DAY=0
for a in "$@"; do
    case "$a" in
        --android) TARGET="android" ;;
        --same-day) SAME_DAY=1 ;;
    esac
done

if [ "$TARGET" = "android" ]; then
    CHANGELOG="android/CHANGELOG.md"
    GRADLE="android/app/build.gradle.kts"
    TAG_PREFIX="android-v"
else
    CHANGELOG="CHANGELOG.md"
    GRADLE=""
    TAG_PREFIX="v"
fi

if [ ! -f "$CHANGELOG" ]; then
    {
        echo "# Changelog"
        echo
        echo "All notable changes to this project will be documented in this file."
        echo "The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)."
        echo
        echo "---"
        echo
        echo "## [New Released]"
        echo
    } > "$CHANGELOG"
fi

if [ -n "$GRADLE" ]; then
    CUR="$(grep -m1 -oE 'versionName = "[0-9]+\.[0-9]+\.[0-9]+"' "$GRADLE" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
else
    CUR="$(grep -m1 -oE '^## \[[0-9]+\.[0-9]+\.[0-9]+\]' "$CHANGELOG" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
fi
CUR="${CUR:-0.0.0}"

IFS=. read -r MA MI PA <<< "$CUR"
case "$KIND" in
    major|breaking|1) MA=$((MA + 1)); MI=0; PA=0; SECTION="Changed" ;;
    minor|feat|2)     MI=$((MI + 1)); PA=0;     SECTION="Added" ;;
    patch|fix|3)      PA=$((PA + 1));           SECTION="Fixed" ;;
    *) echo "unknown kind: $KIND" >&2; exit 2 ;;
esac
NEW="$MA.$MI.$PA"
TODAY="$(date +%Y-%m-%d)"

# At most one release per line per day: the day's work is one batch. A second
# bump the same day is refused (pass --same-day only for an urgent hotfix).
PREV="$(git for-each-ref --format='%(creatordate:short) %(refname:short)' "refs/tags/${TAG_PREFIX}[0-9]*" \
    | awk -v d="$TODAY" '$1 == d { print $2 }' | head -1)"
if [ -n "$PREV" ] && [ "$SAME_DAY" = 0 ]; then
    echo "already released ${PREV} today: batch the rest of today's work into tomorrow's release" >&2
    echo "(or pass --same-day for an urgent hotfix)" >&2
    exit 3
fi

python3 - "$CHANGELOG" "$NEW" "$TODAY" "$SECTION" "$SUMMARY" <<'PY'
import sys
path, ver, day, section, summary = sys.argv[1:6]
lines = open(path, encoding="utf-8").read().splitlines()
block = [f"## [{ver}] - {day}", "", f"### {section}", f"- {summary}", ""]
out, inserted = [], False
for line in lines:
    out.append(line)
    if not inserted and line.strip() == "## [New Released]":
        out.append("")
        out.extend(block)
        inserted = True
if not inserted:
    out.extend(["", "## [New Released]", ""] + block)
open(path, "w", encoding="utf-8").write("\n".join(out).rstrip() + "\n")
PY

if [ -n "$GRADLE" ]; then
    CODE="$(grep -m1 -oE 'versionCode = [0-9]+' "$GRADLE" | grep -oE '[0-9]+')"
    CODE=$(( ${CODE:-0} + 1 ))
    sed -i -E "s/versionName = \"[0-9.]+\"/versionName = \"$NEW\"/; s/versionCode = [0-9]+/versionCode = $CODE/" "$GRADLE"
fi

git add "$CHANGELOG"
[ -n "$GRADLE" ] && git add "$GRADLE"
git commit -q -m "chore(release): ${TAG_PREFIX}${NEW} — ${SUMMARY}"
git tag -a "${TAG_PREFIX}${NEW}" -m "${TAG_PREFIX}${NEW}: ${SUMMARY}"
echo "bumped ${CUR} -> ${NEW} (${SECTION}); tagged ${TAG_PREFIX}${NEW}"
