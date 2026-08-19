# PCkey architecture

## Goals

PCkey is a local-first, user-mode Windows keyboard remapper. The active
profile applies to every standard keyboard in the logged-in desktop session.
It does not identify individual physical devices.

The project deliberately avoids a kernel driver, Electron, network traffic,
telemetry, arbitrary script execution, and key-history logging.

## Processes

### `PCkeyCore.exe`

- Owns the `WH_KEYBOARD_LL` hook.
- Tracks physical key state and suppresses recursive injected events.
- Resolves layers, Tap-Hold, Tap Dance, Combo, macros, and Key Override.
- Runs all deadlines from one high-resolution scheduler.
- Injects scan-code, virtual-key, and mouse events through `SendInput`.
- Owns the tray icon and fixed emergency bypass.
- Hosts health, configuration, shutdown, and key-test IPC on a message-only
  window.
- Atomically reloads the complete active profile after Save and Apply.

### `PCkeyEditor.exe`

- Starts only when the user opens the editor.
- Uses native Win32, Direct2D, and DirectWrite.
- Provides profile creation and a clickable visual keyboard.
- Provides Vial-style category tabs and action keycaps.
- Provides modal editors for macros, Tap Dance, Combo, Key Override, and
  mouse-key settings.
- Provides in-place editing for existing advanced definitions.
- Provides a two-mode key-test page for physical input and applied output.
- Keeps a recoverable draft separate from the applied configuration.
- Saves a complete configuration snapshot and requests a core reload.

## Input pipeline

The implemented processing order is:

1. Ignore injected events so PCkey cannot remap its own output.
2. Inspect the fixed physical emergency chord.
3. Update physical key-down state.
4. Advance expired Combo, Tap-Hold, Tap Dance, macro, and mouse deadlines.
5. Resolve the active profile and Layer stack.
6. Resolve Combo candidates from mapped key identities.
7. Resolve Tap Dance and Tap-Hold decisions.
8. Apply Key Override to the resulting key action.
9. Execute the atomic keyboard, shortcut, virtual-key, macro, layer, or
   mouse action.
10. Inject output with a private marker.

Combo therefore has priority over Tap Dance and Tap-Hold, while Key Override
can transform the key produced by direct mapping, a failed Combo candidate,
Tap-Hold, Tap Dance, or a completed Combo.

## Key identity

A physical input key is identified by:

```text
scan code + None/E0/E1 prefix
```

It is not identified by keyboard device, VID/PID, Bluetooth address, or
human-readable character.

Combo members and Key Override triggers are matched after the active Layer
mapping has produced a logical key identity.

## Scheduling

The core uses one next-deadline timer rather than one thread per feature.
`MappingEngine::NextDeadlineMicros` reports the earliest pending deadline.

Scheduled work includes:

- Tap-Hold threshold activation;
- Tap Dance hold and second-tap windows;
- Combo decision windows;
- macro event delays;
- held mouse movement and wheel repetition.

The low-level hook callback never sleeps while waiting for a decision.

## Tap-Hold

- A press is swallowed and stored in a fixed-size pending binding.
- Releasing before the tapping term emits the Tap action.
- Reaching the tapping term activates Hold.
- Pressing another key activates older pending Holds first, so a held Layer or
  modifier affects the new key immediately.
- Releasing a pre-held modifier settles pending taps first, preserving
  shortcuts such as `Ctrl+Space`.
- Quick Tap can force a recent Tap key to become a normally held repeating key.

## Tap Dance

Each definition stores four non-nested actions:

- tap;
- hold;
- double tap;
- tap then hold.

Another physical key interrupts an unresolved Tap Dance and settles it
immediately. Quick Tap falls back to holding the single-tap action when no
double-tap or tap-hold action is configured.

## Combo

- A possible Combo member is delayed only for the largest relevant Combo term.
- The deferred action is retained so a failed candidate can resume normal
  behavior.
- Members use mapped key identities.
- Complete overlapping rules prefer the larger member count.
- A Combo output is held until any member is released.
- Up to eight Combo outputs may be active concurrently.

## Macros

Macro definitions contain only:

```text
delay + physical key + press/release
```

The scheduler supports eight concurrent different macros. Re-triggering the
same macro while it is active is ignored. Each active macro owns a fixed-size
held-key bitmap, and every held key is released on completion, Stop Macros,
bypass, shutdown, or profile reload.

## Key Override

Rules contain:

- mapped trigger key;
- required modifiers;
- forbidden modifiers;
- modifiers to suppress while the replacement is held;
- optional exact matching;
- replacement action;
- Layer mask.

Ctrl, Shift, Alt, and Win can use an “either side” mask. Modifier suppression
uses reference counts so overlapping rules do not restore a modifier too
early.

## Mouse and media output

- Media and browser actions use `INPUT_KEYBOARD` with Windows virtual keys.
- Normal key mappings and macros use `KEYEVENTF_SCANCODE`.
- Mouse buttons, relative movement, and wheels use `INPUT_MOUSE`.
- Held movement uses profile-level speed, acceleration, and repeat settings.

## Shortcut output

Common system actions are stored as a target scan-code key plus a modifier
mask. The engine presses missing modifiers before the target and releases the
target before releasing only those modifiers that PCkey synthesized. A
physical Ctrl, Shift, Alt, or Win key already held by the user is preserved.

## Key testing

The editor subscribes a temporary test window through `WM_COPYDATA`.

- Physical mode receives the raw scan code before mapping.
- Mapped mode receives pass-through or synthetic output after mapping,
  including delayed Tap Dance, Combo, macro, media, and mouse output.
- While subscribed, the core advances the mapping state for observation but
  suppresses original input and does not inject synthetic output, preventing
  shortcut, media, lock-screen, or mouse side effects.
- Events are sent with non-blocking window messages only while the page is
  open.
- Test events are not written to disk and are not added to startup logs.

## Real-time constraints

The hook callback must remain bounded:

- no file I/O;
- no UI calls;
- no network access;
- no logging of key content;
- no waiting on worker threads;
- no general-purpose heap allocation in the steady-state mapping path.

Runtime bindings, key reference counts, layer holds, macro instances, Combo
instances, and modifier-suppression counts use fixed-size arrays.

## Safety

- Built-in normal mode is immutable and passes all keys through.
- `Left Shift + Right Shift + Escape` held for two seconds activates a latched
  emergency bypass.
- On reload, shutdown, or bypass, PCkey releases every synthetic key and mouse
  button it owns and restores suppressed physical modifiers.
- If `SendInput` fails, the current original event is allowed through to
  preserve basic keyboard usability.
- A crash removes the user-mode hook.

## Configuration and IPC

The core uses a message-only health/IPC window. Configuration reload and
key-test subscription payloads use bounded `WM_COPYDATA` messages. The reload
request contains the selected profile name as UTF-16, and the core reloads
the complete versioned configuration from disk. Invalid or oversized
payloads are rejected.

Configuration writes use a temporary file followed by `MoveFileExW`, so the
core never reads a partially written formal configuration.
