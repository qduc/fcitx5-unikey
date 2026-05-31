# ADR 0002: Immediate Commit rebuild and cursor-move follow-ups

- **Status:** Accepted
- **Date:** 2026-05-31
- **Component:** `src/unikey-state.cpp`, `src/unikey-surrounding-text.cpp`, `src/unikey-state.h`, `test/testsurroundingtext.cpp`
- **Related:** `docs/adr/0001-surrounding-cursor-move-detection.md`, `KNOWN_ISSUES.md`, `firefox-surrounding-text.md`

## Context

ADR 0001 established expected-cursor tracking so mouse-driven caret moves could be detected without relying on a key event. That solved the immediate corruption case, but later changes pushed Immediate Commit into a more self-contained model:

1. The engine now keeps its own committed-word history and can restore a session without trusting surrounding text content.
2. Re-editing a committed word is now part of the normal flow. If the caret moves back into already-committed text, a VNI modifier should apply to that word instead of starting a fresh one or rewriting the wrong location.
3. Rebuilt Vietnamese characters must survive session replay. If surrounding text was used to reconstruct a word, the rebuilt non-ASCII characters must remain in the immediate-commit stroke history or later backspace/retone operations lose the prefix.

The surrounding-text API is still unreliable in the same places as before. Apps can return stale, empty, truncated, or otherwise inconsistent snapshots. The follow-up work therefore keeps the same rule from 0001: never trust surrounding text blindly, and prefer invalidation over speculative rewrites.

## Decision

### 1. Use surrounding text as a rebuild hint, not as the source of truth

`src/unikey-surrounding-text.cpp` now treats stale or truncated snapshots as explicit failure cases instead of trying to force a rewrite from them. When `rebuildStateFromSurrounding()` sees an empty or mismatching word after a recent Immediate Commit, it marks the snapshot unsafe and lets `rebuildStateFromLastImmediateWord()` take over when possible.

This keeps the current word/session anchored in internal history rather than in the app's latest text snapshot.

### 2. Keep cursor-move detection as a guardrail

`src/unikey-state.h` and `src/unikey-state.cpp` still maintain `expectedCursor_` and compare it with the app-reported cursor at the top of each key event. If the caret appears to have moved without a key event, the engine invalidates the immediate-commit state instead of trying to repair it in place.

The detector remains intentionally conservative:

- forward jumps are treated as real moves;
- small backward gaps are tolerated as surrounding-text lag;
- large backward gaps are treated as real moves;
- false positives are accepted because invalidation is safer than rewriting at the wrong position.

### 3. Re-edit committed words only through a narrow VNI path

`UnikeyState::tryReeditImmediateFromSurrounding()` is the narrow re-entry path for committed text. When there is no active immediate-commit session and the next key is a VNI tone/shape digit, the engine can rebuild the preceding word from surrounding text and apply the modifier to it.

This path is deliberately constrained:

- VNI digits only, not Telex tone keys;
- only when there is no active immediate-commit session;
- only when surrounding text can provide a plausible preceding word;
- it reuses the same invalidate-first philosophy if the snapshot looks unsafe.

### 4. Preserve rebuilt characters and prefix behavior

When a word is rebuilt from surrounding text, the replayed stroke history must preserve rebuilt Vietnamese characters, not just raw ASCII keystrokes. This prevents regressions where a later modifier or BackSpace would drop the prefix and leave only the tail of the word.

BackSpace handling in Immediate Commit therefore continues to delete the character nearest the caret while preserving the visible prefix, even after the word was reconstructed from surrounding text.

## Consequences

### Positive

- Mouse-driven caret moves still invalidate Immediate Commit safely, so later tones do not rewrite the old location.
- Re-editing a previously committed word works after navigation back into the word, including Left arrow and BackSpace.
- Rebuilt Vietnamese characters remain editable instead of collapsing into raw tail fragments.
- Stale surrounding text content can be ignored without disabling Immediate Commit entirely.

### Negative / accepted trade-offs

- The re-edit path is intentionally narrow. It solves the VNI digit case; it does not generalize to all input methods.
- The cursor detector still depends on the application reporting a mostly truthful cursor. If the cursor itself is inconsistent, the engine prefers to invalidate and retype rather than guess.
- The implementation now depends on several internal invariants: `expectedCursor_`, `lastImmediateWord_`, and the tracked commit/delete wrappers must stay in sync.

## Validation

Regression coverage in `test/testsurroundingtext.cpp` now documents the follow-up behavior:

- **Case 31** - stale surrounding text content is ignored when the cursor remains truthful.
- **Case 34** - a mouse-like caret jump invalidates immediate-commit state.
- **Case 35** - after Left arrow back into a word, a VNI modifier re-edits the committed word.
- **Case 36** - the same re-edit behavior works after BackSpace.
- **Case 38** - rebuilt Vietnamese characters survive session replay.
- **Case 39** - BackSpace preserves the visible prefix after repeated immediate-commit rewrites.

## Relationship To 0001

This ADR does not replace ADR 0001. It records the next layer of behavior built on top of the original cursor-move detection decision: surrounding text is now treated as an unreliable hint, and Immediate Commit is expected to recover from stale snapshots without corrupting already-committed words.

