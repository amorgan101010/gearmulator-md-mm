#!/bin/sh
# check-fetchable.sh [COMMIT...]
#
# For every submodule commit pinned by the given superproject commits (default: HEAD), asks the
# submodule's URL, as that commit's .gitmodules names it, whether one of its branches or tags
# contains the pin. It asks the network, not local remote-tracking refs: a commit-only (treeless)
# fetch into a temporary repository, so nothing in this clone changes. Git's url.<base>.insteadOf
# settings apply, as they would for `git submodule update`.
#
# Prints one line per distinct pin. Exit 0 if every pin can be fetched, 1 if one cannot, 2 if a
# URL could not be reached (so nothing can be concluded). Used by the pre-push hook (pre-push).
set -eu
top=$(git rev-parse --show-toplevel)
# git exports GIT_DIR (and friends) to a hook. Left set, every "git -C $repo" below works on the
# real repository instead of the temporary one: it fetches the submodules' branches, forced tags
# and promisor remotes into it, and from a linked worktree it never finishes.
for var in $(git rev-parse --local-env-vars); do unset "$var"; done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-fetchable.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM
[ $# -gt 0 ] || set -- HEAD

# url<TAB>sha<TAB>path for every gitlink in the given commits, each once.
for commit in "$@"; do
	git -C "$top" show "$commit:.gitmodules" > "$tmp/gitmodules" 2>/dev/null || continue
	git -C "$top" ls-tree -r "$commit" | awk '$2 == "commit" { print $3 "\t" $4 }' |
	while IFS="$(printf '\t')" read -r sha path; do
		name=$(git config -f "$tmp/gitmodules" --get-regexp '^submodule\..*\.path$' |
			awk -v p="$path" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1; exit }')
		url=$(git config -f "$tmp/gitmodules" --get "submodule.$name.url" || true)
		[ -n "$url" ] || url="(no url for $path)"
		printf '%s\t%s\t%s\n' "$url" "$sha" "$path"
	done
done | sort -u > "$tmp/pins"

rc=0
n=0
for url in $(cut -f1 "$tmp/pins" | sort -u); do
	n=$((n + 1))
	repo="$tmp/r$n"
	git init -q --bare "$repo"
	if ! git -C "$repo" fetch -q --filter=tree:0 "$url" '+refs/heads/*:refs/remotes/o/*' '+refs/tags/*:refs/tags/*' 2>"$tmp/err$n"; then
		grep -F "$url" "$tmp/pins" | while IFS="$(printf '\t')" read -r _ sha path; do
			echo "UNREACHABLE  $path $sha: cannot fetch $url ($(tr '\n' ' ' < "$tmp/err$n" | cut -c1-120))"
		done
		[ $rc -eq 1 ] || rc=2
		continue
	fi
	while IFS="$(printf '\t')" read -r u sha path; do
		[ "$u" = "$url" ] || continue
		if git -C "$repo" cat-file -e "$sha^{commit}" 2>/dev/null &&
			[ -n "$(git -C "$repo" for-each-ref --contains "$sha" --count=1 refs/remotes/o refs/tags)" ]; then
			echo "ok           $path $sha on $(git -C "$repo" for-each-ref --contains "$sha" --count=1 --format='%(refname:short)' refs/remotes/o refs/tags | sed 's|^o/||') of $url"
		else
			echo "UNAVAILABLE  $path $sha: no branch or tag of $url contains it"
			rc=1
		fi
	done < "$tmp/pins"
done
exit $rc
