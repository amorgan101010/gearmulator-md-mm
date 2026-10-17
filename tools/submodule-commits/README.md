# Submodule commit publication record

A superproject commit pins each submodule to a commit. When that commit lives only in a local
clone, nobody else can check the superproject out. Until such a commit is pushed to a repository
the submodule URL can reach, it is kept here as a git bundle (exact objects, so the same hash) and
as a patch (for review).

The `source/mc68k` RingQueue commit `d423bfe9a89449608deaab967ceeea83712dbe8d` is now
published on `amorgan101010/mc68k-md-mm`, branch `daily/mm-trigger-guard`. Current `.gitmodules`
points to that fork. The bundle and patch used before publication are removed from this branch;
older commits retain them in their historical trees.

Keep the pre-push hook installed. It checks that new superproject commits pin submodule objects
that a fresh clone can fetch.

## Checking

```sh
tools/submodule-commits/check-fetchable.sh [COMMIT...]   # default HEAD
```

This asks each pinned submodule's URL over the network, through a commit-only fetch into a
temporary repository, whether one of its branches or tags contains the pin. It prints one line per
pin. Exit status: 0 when every pin can be fetched, 1 when one cannot, 2 when a URL could not be
reached. `url.<base>.insteadOf` settings apply, as they do for `git submodule update`.

`check-published.sh` is the older, offline variant. It reads only local remote-tracking refs, which
can be stale.

The pre-push hook runs `check-fetchable.sh` on the commits a push would publish, and refuses the
push when a pin is unavailable or a URL is unreachable:

```sh
tools/submodule-commits/install-hook.sh    # copies pre-push and the checker into the clone's hooks
git push --no-verify ...                   # bypass, when you know better
rm "$(git rev-parse --git-path hooks)"/pre-push "$(git rev-parse --git-path hooks)"/submodule-check-fetchable.sh   # uninstall
```

## Restoring an older unpublished history

```sh
git clone --recurse-submodules ...             # every submodule but the unpublished one checks out
tools/submodule-commits/restore.sh             # fetches the bundles into their submodules
git submodule update --init --recursive        # now the rest checks out
```

Before publication, this was proven on 2026-10-16 with a fresh clone of `daily/mm-trigger-guard` (`cc109c74`) at
`build/clean-room-20261016/fork-a`:
- seven submodules came from their public URLs;
- `source/mc68k` failed with "not our ref d423bfe";
- after `restore.sh`, it checked out `d423bfe` with tree `9a38f5f7`, the same as this clone's.

Older commits still name `joelanders/mc68k-md-mm` in `.gitmodules` and keep the bundle in their
tree. The current branch can fetch the pin directly from the owner's fork.

## Publication details

The owner fork was created with authorization, and the exact commit was pushed to its
`daily/mm-trigger-guard` branch. The superproject URL update and removal of the bundle, patch, and
publication row are kept together in one commit.

The layout was simulated on 2026-10-16 at `build/clean-room-20261016/fork-b`:
- a local bare repository stood in for the fork, reached through `url.insteadOf`;
- `.gitmodules` was changed in that temporary clone only;
- `git submodule update --init --recursive` checked out all eight pins, and `check-fetchable.sh`
  printed eight ok lines.

Offering the commit to Joe (a pull request to `joelanders/mc68k-md-mm`) can come later and is
independent of this. Until it is merged, the superproject keeps pointing at the owner's fork.

Changing the URL affects commits from the change on. Other clones and worktrees need a
`git submodule sync` after they pull the change.
