# Paperclip: the LiveRack team

The day-to-day team runs in [Paperclip](https://github.com/paperclipai/paperclip)
on the owner's MacBook, in the **LiveRack** project. The head developer (Claude,
in its own session) runs Paperclip from the dashboard: it plans, assigns work,
and is the only one who merges and releases. The owner is the board.

| Agent | Model | Does | Background (from [The Agency](https://github.com/msitarzewski/agency-agents)) |
|---|---|---|---|
| COO | Sonnet | Syncs GitHub issues into tasks, assigns ready work, routes PRs to review, chases what's stuck, daily status | Chief of Staff |
| Engineer 1 | Sonnet | One assigned issue → one pull request | Minimal Change Engineer |
| Engineer 2 | Sonnet | Same | Embedded Firmware Engineer (real-time discipline) |
| Head of Design | Opus | Specs UI changes, design-reviews UI PRs, small UI-only PRs | UI Designer + UI Finish-Gate Reviewer |
| Head of Review | Opus | Reviews every PR (`.claude/agents/reviewer.md`) and posts the report on GitHub | Code Reviewer + Reality Checker |

**GitHub is the record.** Every task names its GitHub issue; every change is a
pull request with green CI and a Head of Review comment; the head developer merges.

**When they run:** heartbeats are off. Agents wake when they're assigned a task
or a comment. The COO also runs at 8:00, 12:00 and 17:00 (Denver). The MacBook
must be awake.

**What makes work "ready":** the head developer adds the `ready` label to a
GitHub issue (P1/P2 on the next milestone). The COO assigns ready work to a free
engineer. `Later` issues stay parked.

## Instructions

Each agent's `AGENTS.md` in Paperclip is `paperclip/team.md` + its role file
(`coo.md`, `engineer.md`, `design.md`, `review.md`) + its Agency background
file(s). To change an agent, change these files by pull request; the head
developer then uploads them to Paperclip.

The earlier single "Head Developer" agent is paused. Its work (PRs #28–#31)
went in without review; Head of Review is reviewing it.
