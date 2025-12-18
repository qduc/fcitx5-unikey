**Summary:**
There is a critical state synchronization issue in Firefox's Wayland text input implementation. When an IME commits text and immediately queries `get_surrounding_text` for the next keystroke, Firefox often returns either **empty strings** or **stale data** (state from before the last commit).

This prevents "edit-in-place" IMEs (like Vietnamese Unikey) from functioning, as they cannot read back the previous character to modify it (e.g., adding a tone mark).

**Scenario 1: Tone Composition Failure (The "anh" Case)**

* **Context:** Standard text input field.
* **Steps:** Type `a`, `n`, `h`, then `1` (tone mark key).
* **Expected:** `anh` becomes `ánh`.
* **Actual:** `anh` becomes `anh1`.
* **Log Analysis:**
* After typing "h", the text field visibly contains "anh".
* User types "1". IME requests surrounding text.
* **Firefox Return:** `Text: ""` (Empty string).
* **Consequence:** The IME assumes it is at the start of a new sentence. It cannot "see" the "anh" to apply the tone mark rule. It simply commits "1".



**Scenario 2: Character Data Loss (The "example" Case)**

* **Context:** Address bar (URL bar) with likely autocomplete active.
* **Steps:** Type `e`, `x`, `a`.
* **Expected:** `exa`.
* **Actual:** `ea` (The 'x' is deleted).
* **Log Analysis:**
* User types `x`. IME commits "ex".
* User types `a`. IME requests surrounding text.
* **Firefox Return:** `Text: "e"` (Cursor at 1).
* **Consequence:** Firefox returned the *stale* state from before the 'x' was typed. The IME trusts this, believes the context is just "e", and appends "a" to it, resulting in "ea". The 'x' is overwritten.



**Detailed Log Trace (Scenario 1 - "anh"):**

```text
// User types '1' (Key 49) to add tone to "anh"
D2025-12-18 16:34:04.329039 unikey-surrounding-text.cpp:139] [rebuildStateFromSurrounding] Called
D2025-12-18 16:34:04.329076 unikey-surrounding-text.cpp:166] [rebuildStateFromSurrounding] Text: "" cursor: 0 length: 0
// FAILURE: Firefox reports empty text, despite "anh" being visible on screen.
D2025-12-18 16:34:04.329131 unikey-surrounding-text.cpp:241] [rebuildStateFromSurrounding] Word length invalid, skipping
D2025-12-18 16:34:04.329481 unikey-state.cpp:337] [commit] Committing string: "1"

```

**Detailed Log Trace (Scenario 2 - "example"):**

```text
// User types 'a' (Key 97) after 'x' was already committed
D2025-12-18 16:47:36.355534 unikey-state.cpp:54] [keyEvent] Key: 97
D2025-12-18 16:47:36.355552 unikey-surrounding-text.cpp:166] [rebuildStateFromSurrounding] Text: "e" cursor: 1 length: 1
// FAILURE: Firefox reports "e" (stale), ignoring the "x" that was just committed.
D2025-12-18 16:47:36.355625 unikey-state.cpp:337] [commit] Committing string: "ea"

```

**Additional observation:**
If the user clicks outside the text box and back inside (forcing a focus reset) between keystrokes, the surrounding text is reported correctly, and composition works (`anh` becomes `ánh`).