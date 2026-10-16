#!/bin/sh
# Installs tools/submodule-commits/pre-push as this clone's pre-push hook (shared by its
# worktrees), with check-fetchable.sh beside it as submodule-check-fetchable.sh. They are copies,
# not links, so the hook keeps working on branches that lack these files; run this again after
# changing them. Refuses to replace a different pre-push hook that is already there.
set -eu
top=$(git rev-parse --show-toplevel)
hooks=$(git rev-parse --git-path hooks)
case $hooks in /*) ;; *) hooks="$top/$hooks" ;; esac
mkdir -p "$hooks"
here="$top/tools/submodule-commits"
dst="$hooks/pre-push"
if [ -e "$dst" ] || [ -L "$dst" ]; then
	if ! grep -q 'check-fetchable' "$dst" 2>/dev/null && [ "$(readlink "$dst" 2>/dev/null || true)" != "$here/pre-push" ]; then
		echo "$dst exists and is another hook; not replacing it" >&2
		exit 1
	fi
	rm -f "$dst"
fi
cp "$here/pre-push" "$dst"
cp "$here/check-fetchable.sh" "$hooks/submodule-check-fetchable.sh"
chmod +x "$dst" "$hooks/submodule-check-fetchable.sh"
echo "installed $dst and $hooks/submodule-check-fetchable.sh"
