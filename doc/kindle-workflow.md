# Kindle workflow

How the Kindle port is developed without getting in the way of reading. This is the fork's own working practice,
not part of the application. See [Kindle](kindle.md) for the port itself.

The aim: reading never depends on what is being built, and building never has to wait for a book to be put down.

## Two installations

| Folder | Library entry | Purpose |
| --- | --- | --- |
| `/mnt/us/kobalt` | Kobalt | Reading. Changes only on `promote` and `rollback`. |
| `/mnt/us/kobalt-dev` | Kobalt Dev (badged icon) | Development builds, with their own state and log. |
| `/mnt/us/kobalt-prev` | none | The app files `promote` last replaced, for `rollback`. |

Both read `/mnt/us/books`; Kobalt never modifies a book. Each install keeps its own `state/`, so a development
build cannot damage reading positions, bookmarks or settings. Reading progress made in Kobalt Dev is not carried
back. Only one Kobalt can run at a time, since they share the display lock.

`platforms/kindle/deploy.sh` does the installing over SSH (run it without arguments for its usage):

```sh
platforms/kindle/deploy.sh init-dev                            # once: create Kobalt Dev from the reading install
KINDLE_PACKAGE=<build>/package platforms/kindle/deploy.sh dev  # install a build into Kobalt Dev
platforms/kindle/deploy.sh seed-dev                            # give Kobalt Dev a fresh copy of the reading state
platforms/kindle/deploy.sh promote                             # reading install takes the development build
platforms/kindle/deploy.sh rollback                            # and back, or forward again
```

Nothing is installed while a Kobalt runs. `promote` copies before it swaps, so an interrupted run leaves the reading
install as it was, and it never touches `state/` or the log.

## Branches

```
upstream-develop ──────────────────────────────►  mirror of nicoverbruggen/kobalt `develop`; never committed on
                                  \ merged into dev when a stable build is cut
feature/<name> ──► dev ───────────┴──► stable-N tags  ──►  promote
```

- `dev` is the integration branch. Every change reaches it by merging a feature branch (`git merge --no-ff`).
- `feature/<name>` branches start from `dev`. Keep changes to shared code (`src/`, `assets/`) in commits of their
  own, apart from `platforms/kindle/`, so they can be offered upstream or dropped without untangling.
- `upstream-develop` follows upstream's `develop` and nothing else. Update it with:
  ```sh
  git fetch origin && git checkout upstream-develop && git merge --ff-only origin/develop && git push fork upstream-develop
  ```
- Upstream's changes enter `dev` only when a stable build is cut: merge `upstream-develop` into `dev`, build, try it in
  Kobalt Dev, and when it holds up, tag `stable-N` and `promote`. Conflicts are settled once, in that merge.
- `main` mirrors upstream's `main` and is left alone. Never push to `origin`; everything is pushed to `fork`.
- `develop-kindle-platform` and `develop-shared-changes` are frozen copies kept for the upstream maintainer to read.
  The tags `archive/*` keep the earlier main-based branches. Delete them when nobody needs them.

## Cutting a stable build

1. Sync `upstream-develop` and merge it into `dev`.
2. Build, `deploy.sh dev`, and read in Kobalt Dev for a few days.
3. `git tag stable-N` on the commit that was tried, push it, then `deploy.sh promote`.

## Reading, and building

- While reading, anything noticed goes in a one-line inbox (a "Bugs and ideas" list), not into a fix. Fixes happen
  in the next development session, in Kobalt Dev.
- While developing, checks run as dry runs (`KOBALT_KINDLEFB_DRYRUN`) wherever possible; they never touch the panel.
  A live test means exiting the book that is open.
- The reading install is not changed except by `promote` or `rollback`, including to fix a bug found mid-book.
