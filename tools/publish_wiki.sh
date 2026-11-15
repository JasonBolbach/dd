#!/bin/sh
# Publishes docs/wiki/*.md to the GitHub wiki of this repository.
# Prerequisite: the wiki exists (create its first page once in the browser). Needs git push access.
set -e
cd "$(dirname "$0")/.."
python3 tools/gen_docs.py
REMOTE=$(git remote get-url origin | sed -e 's/\.git$//').wiki.git
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git clone "$REMOTE" "$TMP/wiki"
cp docs/wiki/*.md "$TMP/wiki/"
cd "$TMP/wiki"
git add -A
if git diff --cached --quiet; then echo "wiki already up to date"; exit 0; fi
git commit -m "Update wiki from docs/wiki"
git push
echo "published to $REMOTE"
