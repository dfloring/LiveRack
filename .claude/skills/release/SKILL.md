---
name: release
description: Make a LiveRack release - a beta for the owner to test on the rig, or promote a tested beta to stable. Use when the owner wants a build, a milestone's work is merged, or the owner says a beta is good.
---

# Releasing LiveRack

Releases come only from CI. A version number change on `main` makes a **beta**
(pre-release) once Mac, Windows and the tests have passed; **Promote to stable**
makes it the version for services.

## A beta

1. **Ready?** `main` is green, the work for it is merged, and `CHANGELOG.md`
   has entries under **Unreleased**. (The Friday-to-Sunday freeze is paused
   by the owner since 20 Nov; it will return later.)
2. **Number.** 0.x.0 for new features, 0.x.y for fixes only. Stays 0.x.
3. **Branch `release/<version>`:**
   - `CMakeLists.txt`: `project(LiveRack VERSION <version>)`
   - **Companion module:** if anything in `companion-module-liverack-host/` changed since the last
     release, bump `version` in its `package.json` (0.1.x). Companion refuses to import a module
     whose version it already has, so an unchanged number means the owner can't update it.
   - `CHANGELOG.md`: rename `## Unreleased` to `## <version> (<date>)`, add a
     fresh empty `## Unreleased` above it. Tidy the entries into plain words
     for the owner (what they'll notice, not how it was done).
   - `README.md`: `# User guide (v0.x)` and any guide changes not yet made.
4. **PR, green CI, merge.** CI then makes the beta (watch the run:
   `gh run list --branch main`).
5. **On the rig.** Download the Windows build
   (`gh release download v<version> -p '*windows*'`, unzip) and copy
   `LiveRack.exe` to the Windows server at `Documents\LiveRack\LiveRack.exe`
   (always that path: Windows Firewall remembers the program location). If
   the computer isn't connected, say so and do it when it is.
6. **Track it.** Open an issue "Test <version> on the rig" with the test list
   as checkboxes, on the version's milestone, label `testing`.
7. **Tell the owner:** what's new (from the changelog, plain), where it is,
   and the numbered test list. No UI walkthroughs.

## Stable

When the owner says the beta is good:
1. (The weekend freeze is paused for now; see CLAUDE.md.)
2. `gh workflow run promote.yml -f version=<version>`, then check it finished.
3. Close the "Test <version>" issue and the milestone.
4. Tell the owner it's the stable version and which file runs on the rig.

## If a beta has a problem

File it (Triage), fix it on a branch, and release the next patch beta. The
broken beta stays a pre-release; never promote it.
