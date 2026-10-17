#!/bin/sh
# Fetches the bundled submodule commits (see README.md) into their submodules' object stores, so
# `git submodule update` can check them out. Safe to run again; changes no checkout.
set -eu
top=$(git rev-parse --show-toplevel)
here="$top/tools/submodule-commits"
for bundle in "$here"/*/*.bundle; do
	name=$(basename "$(dirname "$bundle")")
	path="source/$name"
	if [ ! -e "$top/$path/.git" ]; then
		git -C "$top" submodule update --init --no-fetch "$path" 2>/dev/null || git -C "$top" submodule init "$path"
		git -C "$top" submodule update --init "$path" 2>/dev/null || true
	fi
	git -C "$top/$path" bundle verify "$bundle" >/dev/null
	git -C "$top/$path" fetch --quiet "$bundle" '+refs/heads/*:refs/remotes/bundle/*'
	echo "$path: fetched $(git -C "$top/$path" bundle list-heads "$bundle" | cut -c1-12)"
done
