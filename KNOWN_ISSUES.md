# Known Issues / Watch List

Short, actionable notes about not-yet-fixed edge cases. If you hit one of these,
point Claude at this file and the specific anchor (e.g. "see KNOWN_ISSUES.md
#stale-surrounding-different-length") to skip re-investigation.

---

## #stale-surrounding-different-length — Stale surrounding text at a *different* length re-converts/corrupts a word (immediate-commit mode)

**Status:** Mitigated for known unreliable browser/app surrounding-text providers.
A related *same-length* variant is also fixed (see below).

**Mode:** Immediate Commit ON (also relevant to Modify Surrounding Text).

**Symptom to watch for:** while typing/editing a word, the just-committed word
gets re-converted or mangled — e.g. a repeated tone key re-applies the tone
(`cá` instead of undoing to `ca1`), or stray/missing characters appear — because
the application returned a *stale* surrounding-text snapshot.

**Root cause:** in immediate-commit mode the engine is reset after every
keystroke and the word is rebuilt by replaying the app's *surrounding text* back
through the Unikey engine. If that snapshot is stale, the replay re-runs Vietnamese
conversion on the wrong text. The IME keeps an authoritative internal copy in
`lastImmediateWord_` / `lastImmediateWordCharCount_`, and any real cursor move /
navigation clears it (`UnikeyState::handleIgnoredKey`), so when `lastImmediateWord_`
is still populated, a surrounding word that disagrees with it is almost certainly
stale rather than a genuinely different word.

**Where:** `src/unikey-surrounding-text.cpp`, `UnikeyState::rebuildStateFromSurrounding()`,
the `if (deleteSurrounding && !lastImmediateWord_.empty())` /
`if (wordUtf8 != lastImmediateWord_)` block. Fallback is
`rebuildStateFromLastImmediateWord()`, invoked from `rebuildPreedit()` when
`lastSurroundingRebuildWasStale_` is set.

**Handled:**
- *Truncated* snapshots (surrounding is a prefix/suffix of `lastImmediateWord_`).
- *Longer* snapshots that end with `lastImmediateWord_` (more context — trusted; see test case 6).
- *Same character length but different content* (e.g. `ca` vs `cá`) → treated as
  stale, falls back to the internal word. Regression test: `test/testsurroundingtext.cpp`
  **case 27** ("Double-tap undo survives stale surrounding").
- Divergent snapshots from known unreliable providers (`Firefox`, Chromium-based
  browsers, LibreOffice) that are not the explicit "longer context ending with
  last word" case → treated as stale, falls back to the internal word.

**Remaining gap:** for applications not in the known-unreliable list, a divergent
snapshot still falls through to "accept surrounding" to avoid breaking legitimate
cursor moves or external text changes. Add a regression test before broadening
that policy.
