#!/bin/sh
# Lists the submodule commits pinned by HEAD that no remote-tracking branch of that submodule
# contains, i.e. that a fresh clone could not fetch. Exit 1 if there is any. Run `git fetch` in the
# submodules first for an up-to-date answer; this reads only local refs.
set -eu
top=$(git rev-parse --show-toplevel)
status=0
git -C "$top" submodule status | while read -r sha path rest; do
	sha=${sha#[-+U]}
	sub="$top/$path"
	[ -e "$sub/.git" ] || continue
	[ -n "$(git -C "$sub" branch -r --contains "$sha" 2>/dev/null | grep -v 'bundle/')" ] && continue
	# Also fine: a published superproject branch pins the same commit, so its owners serve it.
	pinned=no
	for ref in $(git -C "$top" for-each-ref --format='%(refname)' refs/remotes); do
		if [ "$(git -C "$top" ls-tree "$ref" "$path" 2>/dev/null | awk '{print $3}')" = "$sha" ]; then
			pinned=yes
			break
		fi
	done
	if [ $pinned = no ]; then
		echo "$path $sha is on no remote branch$( [ -n "$(ls "$top/tools/submodule-commits/$(basename "$path")"/*.bundle 2>/dev/null)" ] && echo ' (bundled in tools/submodule-commits)')"
		echo x
	fi
done | { out=$(cat); [ -z "$out" ] && exit 0; echo "$out" | grep -v '^x$'; exit 1; }
