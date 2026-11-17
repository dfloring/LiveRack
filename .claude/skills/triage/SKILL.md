---
name: triage
description: Turn the owner's notes, screenshots and test results about LiveRack into GitHub issues - labelled, prioritised, on the right milestone, without duplicates. Use whenever the owner reports a bug, asks for something, or sends test notes.
---

# Triage

The owner reports in chat, often several notes at once, sometimes with
screenshots. Each distinct problem or request becomes one issue.

1. **Split** the message into separate items. One issue per problem.
2. **Look for duplicates:** `gh issue list --state all --search "<words>"`.
   A repeat gets a comment on the existing issue (reopen it if it was closed).
3. **Labels:**
   - type: `bug`, `feature`, `polish`, `question`
   - area: `native`, `client`, `ipad`, `server`, `console`, `midi`, `plugins`, `audio`, `build`
   - priority: `P1` (could hurt a service: fix before the next one),
     `P2` (next version), `P3` (later)
4. **Milestone:** the next version for P1 and P2 (create it if needed), `Later` for P3.
5. **Write it** so it reads on its own:
   - **What happened** (their words, quoted briefly) and **what should happen**
   - **Where:** which computer and role (Native, Server, Client, iPad), version, console
   - **Steps** if known
   - Screenshots: describe what they show (attachments in chat don't carry over)
   - For a bug, what you suspect, if anything, marked as a guess
6. **Reply** with a short list: issue number, title, priority. If they said
   "no build yet", stop there and wait for "go"; otherwise say what you'll start on.

A P1 found close to a service: tell the owner right away, plainly, with what
to avoid until it's fixed, and start the hotfix path (`CLAUDE.md`).
