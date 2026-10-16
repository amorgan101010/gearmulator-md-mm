# Submodule commits that exist only here

A superproject commit pins each submodule to a commit. When that commit lives only in a local
clone, nobody else can check the superproject out. Until such a commit is pushed to a repository
the submodule URL can reach, it is kept here as a git bundle (exact objects, so the same hash) and
as a patch (for review).

| Submodule | Commit | Based on (public) | First pinned by | Published |
|---|---|---|---|---|
| `source/mc68k` | `d423bfe9a89449608deaab967ceeea83712dbe8d` RingQueue for the HDI08 receive and interrupt queues | `ace95b3` on joelanders/mc68k-md-mm | `b6920936` | no: there is no mc68k fork of the owner's to push to yet |

Restore after a fresh clone, then update the submodules as usual:

```sh
tools/submodule-commits/restore.sh
git submodule update --init source/mc68k
```

Before pushing a superproject branch, check that every pinned submodule commit can be fetched:

```sh
tools/submodule-commits/check-published.sh   # exit 1 and a list when one cannot
```

Once a commit is pushed somewhere the `.gitmodules` URL reaches, delete its bundle and patch and
its row here.
