# Submodule commits that exist only here

A superproject commit pins each submodule to a commit. When that commit lives only in a local
clone, nobody else can check the superproject out. Until such a commit is pushed to a repository
the submodule URL can reach, it is kept here as a git bundle (exact objects, so the same hash) and
as a patch (for review).

| Submodule | Commit | Based on (public) | First pinned by | Published |
|---|---|---|---|---|
| `source/mc68k` | `d423bfe9a89449608deaab967ceeea83712dbe8d` RingQueue for the HDI08 receive and interrupt queues | `ace95b3` on joelanders/mc68k-md-mm | `b6920936` | no: there is no mc68k fork of the owner's to push to yet |

**Until every row here is published, do not push a superproject branch that pins one of these
commits** (today: `daily/mm-trigger-guard` from `b6920936` on). The pre-push hook below refuses
such a push.

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

## Restoring after a fresh clone

```sh
git clone --recurse-submodules ...             # every submodule but the unpublished one checks out
tools/submodule-commits/restore.sh             # fetches the bundles into their submodules
git submodule update --init --recursive        # now the rest checks out
```

Proven on 2026-10-16 with a fresh clone of `daily/mm-trigger-guard` (`cc109c74`) at
`build/clean-room-20261016/fork-a`:
- seven submodules came from their public URLs;
- `source/mc68k` failed with "not our ref d423bfe";
- after `restore.sh`, it checked out `d423bfe` with tree `9a38f5f7`, the same as this clone's.

## Publishing (recommended; needs the owner's go-ahead for each push)

The recommended layout matches `source/dsp56300`, which already points at the owner's fork.

1. Create `amorgan101010/mc68k-md-mm` on GitHub as a fork of `joelanders/mc68k-md-mm`.
2. Push the commit as `daily/mm-trigger-guard`, the same branch name the dsp56300 fork uses:
   ```sh
   git -C source/mc68k remote add mine git@github.com:amorgan101010/mc68k-md-mm.git
   git -C source/mc68k push mine d423bfe9a89449608deaab967ceeea83712dbe8d:refs/heads/daily/mm-trigger-guard
   ```
3. Point the superproject at it, in one commit on `daily/mm-trigger-guard`:
   ```sh
   git config -f .gitmodules submodule.source/mc68k.url https://github.com/amorgan101010/mc68k-md-mm.git
   git submodule sync source/mc68k
   tools/submodule-commits/check-fetchable.sh    # must print only ok lines
   ```
4. Delete the bundle, the patch and the row above in the same commit.
5. Only then push the superproject branch, as a separate decision.

The layout was simulated on 2026-10-16 at `build/clean-room-20261016/fork-b`:
- a local bare repository stood in for the fork, reached through `url.insteadOf`;
- `.gitmodules` was changed in that temporary clone only;
- `git submodule update --init --recursive` checked out all eight pins, and `check-fetchable.sh`
  printed eight ok lines.

Offering the commit to Joe (a pull request to `joelanders/mc68k-md-mm`) can come later and is
independent of this. Until it is merged, the superproject keeps pointing at the owner's fork.

Changing the URL affects only the commits from the change on. Older commits keep pointing at
`joelanders/mc68k-md-mm`, where their pins are. Other clones and worktrees need a
`git submodule sync` after they pull the change.
