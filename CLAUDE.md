# Tau Alpha: project instructions

## 1. Who does what
- **Claude (this session):** does the work in this repo: design, firmware, RTL, host tests, tools, docs, card installs and Quartus fits on the VM, following the rules below.
- **Codex (optional second opinion):** reads `AGENTS.md` (which points it at the `analogue-pocket-dev` skill). `tools/triad/` and `docs/DEVELOPMENT_WORKFLOW.md` describe an experimental Codex-plans, local-Qwen-writes-a-patch workflow; it is not part of the day-to-day path. There is no Aider and no automatic adversarial-review step.
- **The owner** decides priorities, approves card writes and VM fits, and tests on the Pocket.

## 2. Start of a session
1. Read `docs/CURRENT_STATUS.md` (one page: what is shipped, what is installed, what is in flight), then `docs/ROADMAP.md` (the single "what is next" list; the owner sets the order), then the newest file in `docs/handoffs/`.
2. `docs/AUDIT_TRAIL.md` is the evidence log (A-NNN SDRAM/UI era, B-NNN since; log every hardware result with an evidence label). `docs/DECISIONS.md` holds recorded owner decisions; `docs/issues/` the open issues. Feature specs are in `docs/features/`; prefer the subdirectory copy when a file exists twice.
3. Commit only your own files; never stage another session's uncommitted work. Ask before any SD-card write and follow the backup / clear-catalog / verify-SHA-256 / eject procedure in `docs/CARD_INSTALL_PROCEDURE.md`. **Do the install with `tools/install_dev_core.py` (dry run first, `--yes` after approval), never hand-typed; pass `--card /Volumes/Pock` (the tools default to CARDWRITE). When the procedure changes, change the script, its test (`sim/test_install_dev_core.py`) and the doc together.**
4. **One status format:** `docs/ROADMAP.md` is the ORDER of work (the owner sets it), `docs/CURRENT_STATUS.md` is where things stand NOW (one page), `docs/handoffs/` hold the detail of one session. Do not add "newest" pointers or a log to this file; the old chain is archived in `docs/archive/CLAUDE_SESSION_START_CHAIN_2026-10-14.md`, the old log is `docs/EXECUTION_LOG.md`.

## 3. Rules
@docs/PROJECT_RULES.md

## 4. Testing rules (catch the bug before a human can see it)
The Pocket answers only what a desk cannot: timing margin of the real fit, electrical and display behaviour, the SD card and the host's own behaviour. Everything else is proven on the host first, and a change is not done until its tests have run.
1. **Every block has a reference and mutants.** A bit-exact reference test (Python or C model, or frozen vectors) AND a mutation sweep: each hand-written mutant must make a test fail. A first run that passes is a smell, not a result: run the mutants. A surviving mutant is either a weak test (add the test) or an equivalent mutant (prove it by enumeration, say so in the audit entry, drop it). Every new feature gets a test that fails when the feature is removed (the ring's `hold` and the FIFO clear were found untested only by the sweep).
2. **Clock-domain crossings are tested with unrelated clocks.** A dedicated testbench per crossing, odd phases and ratios, random traffic, clears and errors; only toggles, Gray counters and data that is stable before the pointer move may cross; never reset a toggle with the system reset.
3. **Timing is a test, and it runs before the full fit** (1h35m to 1h45m). A new or changed block gets a probe (`python3 tools/block_probe.py launch NAME unit|ent`, minutes, needs the owner's yes and a stated finish time like any VM run); integrate only a block whose probe meets setup at its real clock at the slow 85C corner with at least 0.5 ns to spare; put the Fmax in the audit entry. The m1c fit missed by 5 ns on a block that was bit-exact in simulation.
4. **Whole-core simulation before hardware or a fit** (STATE 2026-11-12: built and booting the host protocol, but the CPU does not yet run the firmware, docs/issues/026; until that is fixed it can only show wiring and the host handshake, say so in the audit entry) when a change touches `core_top`, `tau_soc`, `core_game.vh`, a PLL, the SDC, the bridge wiring or the SDRAM path: `python3 sim/fullcore/run.py` (the real VexRiscv, the SDRAM PHY against the timing-checked chip model, scan-out, a model of the Pocket's host; docs/FULLCORE_SIM.md). Per-block tests do not catch wiring (an unconnected input put X into the CPU). Intel's `altera_mf.v` is read from `work/quartus_sim_lib/` and is never committed.
5. **No known-failing test stays in the suite.** Fix it, or move it out with an entry in `docs/issues/` the same session (owner, date, reason). `make test` is expected green; run the target you touched before every commit and say in the audit entry what ran and what did not. Makefile rules must work with spaces in the path (quote `"$(RV_BIN)/..."`).
6. **A bug found on the Pocket becomes a test first.** Reproduce it on the host (or in the whole-core simulation), watch the test fail, then fix; keep the screenshot or report beside the audit entry.
7. **Frozen specifications and vectors change only as a new version** (symbol layer, reconstruction, container); a test is never edited to make it pass: find the cause.
8. **Report honestly.** Say what was simulated, what was synthesised, what was fitted and what ran on a Pocket, and what was not run.

## Subagent delegation policy

Before spawning a subagent (Task/Agent tool), check whether it's actually worth the overhead:
- Only delegate when the work is verbose/high-volume (test runs, log scanning, multi-file research,
  large doc fetches) or genuinely parallelizable. Small, quick lookups stay in the main context —
  a subagent's own startup cost (loading CLAUDE.md, skills, MCP tool defs) isn't worth it for trivial work.
- Never spawn a subagent just to "be thorough" — spawn one because its output would otherwise bloat
  the main conversation.

When delegating, write the subagent prompt to minimize its cost and the tokens it returns:
- Scope it tightly: state the exact question/deliverable, the specific files/paths/URLs it needs
  (not "explore the codebase"), and an explicit output format so it doesn't over-explain.
- Explicitly cap output: tell it to report back a short summary or the specific artifact needed —
  not full transcripts, full file contents, or step-by-step narration — since only the returned text
  re-enters the main context.
- Pick the cheapest model that can do the job: use `model: haiku` for mechanical/simple subagent tasks
  (formatting, running a command and reporting pass/fail, simple lookups), and reserve a stronger model
  for subagents doing real reasoning or judgment calls.
- Keep the spawn prompt itself short — everything in it enters the subagent's context immediately,
  on top of whatever CLAUDE.md/skills/MCP tools it auto-loads.
- Prefer one well-scoped subagent over several redundant ones; batch independent lookups into a single
  subagent call when they don't need separate context isolation.
- If a task doesn't clearly need isolation (verbose output, parallelism), just do it directly instead
  of delegating.
