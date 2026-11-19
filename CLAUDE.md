# LiveRack: how this project is run

LiveRack is a live plugin host (a SuperRack replacement) for mixing consoles,
used for church services. Native (the show and its audio on one computer),
Server (the same app on the audio computer), Client (the screens on another
computer) and the iPad / web remote. AGPLv3. See `docs/ARCHITECTURE.md`.

## The team

| Role | Who | Does |
|---|---|---|
| Product owner and tester | The repo owner | Priorities, what "done" means, approves releases, tests on the rig (Windows server, MacBook, iPad, Yamaha CL5, X32) |
| Head developer | Claude, the main session | Plans, writes the code, runs the process below, reports to the owner |
| Reviewer | `.claude/agents/reviewer.md`, run by Paperclip | Reads every pull request fresh before it merges; posts its review as a PR comment |
| QA / tester | `.claude/agents/qa-tester.md` | Adds and runs automated tests for a change |
| Release | `.claude/skills/release/` | Version, changelog, beta, the build on the rig, stable |
| Triage | `.claude/skills/triage/`, also run by Paperclip | Turns the owner's notes into issues; Paperclip labels issues the owner opens on GitHub |
| Nightly | Paperclip | Opens an issue when the build of `main` is red |

The Paperclip agents run on the owner's MacBook (`docs/PAPERCLIP.md`). They
only comment and label on GitHub; they never push, merge or approve. GitHub
issues and PRs are the record; Paperclip's own tasks are just their run log.

The owner is not a C++ developer and reads reports, not code. Write to them in
plain language: what changed for them, what to test, what you need from them.

## Working with the owner

- No screenshots or walkthroughs of UI changes in reports; just say what changed.
- When they send notes and say "no build yet", file them (Triage) and wait for "go".
- Don't probe their network or devices without asking.
- Versions stay 0.x until they say otherwise.
- Test requests are short numbered lists of things to try on the rig.
- They decide priorities; you decide how. Make the call on technical choices
  and say what you chose; ask only when a decision is theirs or can't be undone.

## How work flows

1. **Issue.** Every bug, idea and owner note is a GitHub issue with labels
   (type, area, priority) and a milestone (the next version, or `Later`).
2. **Branch.** `fix/<issue>-<slug>`, `feat/<issue>-<slug>` or `chore/<slug>` from `main`.
   `main` stays releasable.
3. **Build and test.** Every change gets an automated test (QA agent): a
   check in `Source/PreviewHooks.h` / `Source/ClientPreview.h` and a scenario
   in `tests/run.py`. A bug fix gets a test that would have caught it.
4. **Pull request.** Fill in the template. CI builds Mac and Windows and runs
   the tests ("Tests (Linux)"); it must be green.
5. **Review.** Wait for the "Paperclip reviewer for <commit>" comment on the
   PR's latest commit, or run the reviewer agent yourself if it hasn't come
   (the MacBook may be asleep). Fix what it confirms; note anything you
   decline and why in the PR. A fix pushes a new commit, which gets a new review.
6. **Merge** (squash). Add a line under **Unreleased** in `CHANGELOG.md` in the PR itself.
7. **Release** when the owner wants a build or a milestone is done: Release skill.
   A version bump on `main` makes a **beta**; the owner tests it on the rig;
   **Promote to stable** makes it the version for services.

**Freeze:** no promotions to stable and no updates to the rig from Friday to
Sunday unless the owner asks (a fix for a service problem is the exception).

**Hotfix:** a problem that would hurt a service goes first: fix branch,
test, review, patch version (0.x.y), beta, and say plainly what to install.

## Building and testing

- Mac / Windows apps: CI (`.github/workflows/build.yml`). Releases come only from CI.
- Linux test build: `cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=<JUCE 8.0.4> -DLIVERACK_PREVIEW=ON`
- Tests: `tests/README.md`; `python3 tests/run.py --app <LiveRack> --plugins <build-plugins>`.
  Add `--only <names>` while working; run all before a PR.
- Screenshots for your own checks: run the test build under Xvfb and capture
  the screen (not for reports to the owner).

## Code rules

- **The audio thread never waits.** Nothing in the audio callback allocates,
  locks a mutex, does file or network I/O, or calls into the message thread.
  Atomics and lock-free FIFOs only. Glitches during a service are the worst bug.
- **A crashing plugin must not take the show down** (isolation, the crash
  watcher, Server mode's restart). Keep those paths working.
- **Message thread for UI.** Network and worker threads hand results over with
  `MessageManager::callAsync` + a `SafePointer` (or a shared alive flag).
- **Never delete a component from inside its own callback** (a button's
  onClick removing its parent, as in 0.28.0): defer with `callAsync`.
- **Every screen reads the show through `LiveConnection`**, never the engine
  directly, so Native, Client and the iPad stay the same. New features add to
  `LiveConnection`, `LocalConnection` and `RemoteConnection` together.
- **The protocol only grows.** Add fields and commands; never change or
  remove what an older Client or iPad page uses. Unknown fields are ignored.
  A breaking change would need `protocol` bumped in `hello`, and the owner's OK.
- **Settings per role:** Native files as they are; LiveRack Client's own are
  `client-*` (its MIDI, shortcuts), so one Mac can be both.
- Comments say why, briefly. User-facing text is plain and short.

## Commits and pull requests

- Commit messages: a short summary line, then what and why.
- End commits and PR descriptions with the attribution lines your session gives.
- Push branches and open PRs with `gh`; merge after green CI and review.

## Where things are

- `Source/`: the app (see `docs/ARCHITECTURE.md`)
- `tests/`: automated tests and the plugins they host
- `docs/DECISIONS.md`: why things are the way they are; add to it when you decide something lasting
- `CHANGELOG.md`: what each version changed (becomes the release notes)
- `README.md`: the user guide and protocol notes; keep it current with each release
