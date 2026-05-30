# ADR 0001: Detect mouse-driven caret moves via the surrounding-text cursor

- **Status:** Accepted
- **Date:** 2026-05-30
- **Component:** `src/unikey-state.cpp`, `src/unikey-surrounding-text.cpp`
- **Related:** `KNOWN_ISSUES.md`, `firefox-surrounding-text.md`

## Context

The engine composes Vietnamese text from a stream of key events. In
**Immediate Commit** mode it commits each transformation directly into the
application and rewrites the just-committed word in place (via
`deleteSurroundingText` + `commitString`) when a later tone/shape key changes it.
This rewrite is positioned relative to the caret.

The caret can move for two reasons:

1. **Key events we see** — arrows, Home/End, Backspace, etc. These already flow
   through `preedit()` and `handleIgnoredKey()`, which clears composition state.
2. **Events we do _not_ see** — the user clicks elsewhere with the mouse. fcitx
   delivers no key event for this. Well-behaved apps send an `InputContextReset`
   (handled in `UnikeyEngine::reset` → `clearImmediateCommitHistory`), but not
   all apps do so reliably.

When a mouse move goes undetected, the next tone key rewrites text at the **old**
position, corrupting the document (deletes/inserts in the wrong place).

The only signal available for the undetected case is the cursor position the
application reports through `InputContext::surroundingText().cursor()`. However,
that snapshot is known to be unreliable in exactly the apps that need Immediate
Commit (Firefox, Chromium, LibreOffice): it can be **stale**, **empty**, or
**truncated**, especially right after a commit — this is the reason the
`surroundingTextUnreliable_` machinery and the stale/truncation detection in
`rebuildStateFromSurrounding()` exist. So we cannot naively trust `cursor()`.

## Decision

Track an **expected caret position** and compare it against the app-reported
cursor at the **top of each key event**, treating an unexplained divergence as a
mouse-driven caret move that **invalidates** composition state.

### 1. Expected-cursor bookkeeping

All document mutations are routed through two wrappers that keep an
`expectedCursor_` belief in sync:

- `commitStringTracked()` — advances `expectedCursor_` by the inserted length.
- `deleteSurroundingTextTracked()` — moves it back by the deleted length.

Every existing `commitString` / `deleteSurroundingText` call site in
`unikey-state.cpp` and `unikey-surrounding-text.cpp` was migrated to these.

### 2. Read fresh at the top of the key event

The comparison is performed at the very start of `preedit()`, **before** this
event commits anything. The worst staleness is the same-handler,
right-after-commit snapshot; reading at the top of the _next_ event gives the
app a full round-trip to settle. After comparing, `expectedCursor_` is
**re-anchored** to the app cursor, so each event's delta is independent and the
belief is self-correcting (a one-off bad reading cannot drift unboundedly).

> Caveat: "read at the top" is not "guaranteed fresh" — surrounding-text updates
> are asynchronous and can still lag during fast typing. This is handled by the
> directional rule below.

### 3. Decision rules for "moved"

Let `delta = actual_cursor - expectedCursor_`.

- **Forward (`delta > 0`)** beyond any active preedit ⇒ **move**. A stale/lagging
  snapshot can only report an _older, smaller_ cursor, so a larger-than-expected
  cursor cannot be staleness — it is high-confidence.
- **Backward (`delta < 0`)** ⇒ **move only if `|delta| > kCursorMoveBackwardTolerance`**
  (currently `1`). A small backward gap is ambiguous with surrounding-text lag,
  so it is absorbed; a larger gap is a real click.
- **Within active preedit length** (`0 < delta <= preeditChars`) ⇒ **not a move**.
  Some apps include the active preedit in the reported cursor; this guard keeps
  the detector portable to them.

### 4. Discard the empty/zeroed placeholder

A snapshot that is `isValid()` but has empty text with `cursor == 0 && anchor == 0`
is treated as an unreliable placeholder (observed as Firefox's first keystroke
after focus). It is discarded: no detection, and it does **not** overwrite the
existing belief.

### 5. Action on a detected move

If there is composition state to lose, mirror the `InputContextReset` path:
`clearImmediateCommitHistory()` + `reset()`. The current keystroke then starts a
fresh word at the new caret. We **only invalidate** — never rewrite — so a false
positive costs at most one re-typed tone.

### 6. Enables re-editing a committed word (Immediate Commit)

Because a detected move (mouse or arrow) now reliably clears internal state, we
also added `tryReeditImmediateFromSurrounding()`: when a **VNI tone/shape digit**
arrives at a word boundary with **no active immediate session**, the preceding
word is rebuilt from surrounding text into a fresh session so the modifier
applies to it (e.g. `ca` → navigate back → `1` → `cá`). This is deliberately
narrow — VNI digits only, no active session — so it never interferes with
forward typing.

## Consequences

### Positive

- Mouse clicks that move the caret no longer cause wrong-position rewrites in
  Immediate Commit mode.
- Re-editing a previously committed word with a tone key works after navigating
  back (arrow, Backspace, or mouse) — previously produced a literal digit
  (`ca1`).
- The mechanism is cursor-only and independent of whether the surrounding _text_
  content is trusted, so it protects Immediate Commit (including Firefox).
- Self-correcting design: re-anchoring each event bounds the impact of any single
  bad snapshot.

### Negative / accepted trade-offs

- **Backward moves of exactly one character are not caught** (tolerance = 1).
  Tunable via `kCursorMoveBackwardTolerance`.
- An app reporting a **wildly inconsistent cursor every keystroke** is now
  indistinguishable from a mouse move and will invalidate state. This changed the
  contract of regression **test case 31**, which previously fed garbage
  surrounding _and_ a garbage cursor; it now feeds garbage text with a truthful,
  advancing cursor (a real app reports an accurate cursor even when its text
  snapshot is stale). See the test for rationale.
- The re-edit helper is **VNI-only**. Telex tone keys (`s/f/r/x/j`) are ordinary
  letters, so a digit-style trigger does not apply and "modify previous word" vs.
  "start a new word" is ambiguous. Left as future work.
- Adds an `expectedCursor_` invariant that every new document-mutation site must
  respect by going through the tracked wrappers.

### Neutral

- A false positive is benign by construction (invalidate-only): at worst the user
  re-types one tone.

## Alternatives considered

1. **Gate cursor-move detection off in Immediate Commit mode** (only act where
   surrounding text is already trusted). Preserves the old test-case-31 contract
   unchanged but leaves the primary use case (Firefox immediate commit) with no
   mouse-move protection. Rejected.
2. **Text-consistency cross-check** — only invalidate when the surrounding text
   corroborates our state. A single snapshot cannot reliably distinguish a lying
   app from a genuine move, so it is both complex and imperfect. Rejected.
3. **Rely solely on `InputContextReset`.** This is the proper channel and is kept
   as the primary signal, but it is not emitted reliably by all apps — which is
   the entire reason this ADR exists.

## Validation

- Logged (`[cursor-move]` lines) and observed in real apps: normal typing reports
  `in-sync`; the only anomaly was Firefox's empty/zeroed first-keystroke
  placeholder, handled by rule 4.
- Regression tests: `test/testsurroundingtext.cpp`
  - **case 31** — updated contract (stale text content, truthful cursor).
  - **case 34** — a simulated mouse jump invalidates state (fresh `a` instead of
    rewriting `to` → `toa`).
  - **cases 35 / 36** — re-edit after Left arrow / Backspace yields `cá`.

## Tuning knobs

- `kCursorMoveBackwardTolerance` (`src/unikey-state.h`) — backward-gap threshold.
- `tryReeditImmediateFromSurrounding()` scope — currently VNI digits only.
