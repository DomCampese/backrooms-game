# Readability protocol

Write code and prose the way a careful engineer who owns the codebase would:
plainly, briefly, and for the next reader. Apply this to everything you touch.
Do not reformat files you are not otherwise changing; a style-only diff across
untouched code buries the change that matters.

## Code

- A name says what the thing is, with units where it helps (`riseM`, `cells`,
  `px`). Short names are fine when they are local and conventional in this code:
  `gi, gk` global cell, `i, kk` chunk-local cell, `cx, cz` chunk.
- One function, one job. When a block inside a function needs a comment to say
  what it does, it usually wants to be a function with that name.
- No dead code, no commented-out code, no unused parameters kept "for later".
- A number that means something gets a name. A number used in two places gets
  one definition that both places read.
- Prefer data over branches: a table of per-level values beats a chain of
  `if (level == ...)`.
- Keep the layering: core code does not include `raylib.h`, and simulation code
  does not draw or play sound (docs/migration.md).

## Comments

- A comment states a constraint or a reason the code cannot show. It does not
  restate the code.
- Lead with the rule. If a plausible edit would break it, add the symptom in one
  clause: "Must match `PANEL_HALF`, or the highlight is the wrong size."
- One to three lines is normal. A long history belongs in the commit message,
  where `git log -S` finds it, not above the function.
- No lore flavour, jokes or scene-setting in code. The design is described in
  CREDITS.md and the docs.
- No first person, no "we", no addressing the reader.

## Words and constructions to cut

These read as filler or as generated text. Remove them from code, comments,
commit messages and docs:

- Intensifiers and hedges: *very, really, crucially, importantly, simply, just,
  basically, actually, quite, arguably, essentially*.
- Signposting: *note that, it's worth noting, keep in mind, the key insight,
  here's the thing, in other words, that is to say*.
- Inflated vocabulary: *leverage, robust, seamless, utilize, facilitate,
  comprehensive, delve, ensure that* (write "so" or "to").
- Rhetorical shapes: "not X, but Y"; "X — and that's the point"; "the one thing
  that"; three-item lists added for rhythm; a sentence that only announces the
  next one.
- Narrated symptoms in prose style ("which reads as...", "looks exactly like...")
  when a plain statement of the failure will do.
- Emphasis markup (bold, capitals) inside code comments.
- Em dashes used as general-purpose punctuation. Use a full stop or a comma.

## Commit messages

Imperative subject under 72 characters, naming the area: `world: split mesher
out of the generator`. The body says what changed and why, and how it was
verified. No marketing, no summary of the summary.

## Documentation

Lead each section with the rule or the fact. Use a table when comparing things.
State a measured number with how it was measured. Record a failure mode when it
is not obvious from the code, in the form: rule, symptom, reason.

## Behaviour-preserving changes

A refactor claims to change nothing. Prove it, cheapest proof first:

1. `diff` of `mapdump` output for the generator (tools/proof-shots.sh lists the
   capture set; the mapdump commands are in docs/migration.md).
2. Identical assembly for a file whose code only moved or was renamed (AGENTS.md,
   "Measuring the layout").
3. `tools/proof-shots.sh` before and after, compared with `--diff`, against the
   noise floor of the same binary captured twice.
4. The regression harness.

State which proofs you ran in the commit message.
