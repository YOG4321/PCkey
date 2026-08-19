# PCkey configuration format

PCkey stores configuration as versioned UTF-8 text.

```text
PCKEY_CONFIG 4
active "Office"
profile "Office" 4 0
mouse 4 18 500 16 120
action 0 57 0 5 57 0 1 0 0 500 200 0 0 0 0 0 0 0 0 0
endprofile
```

The loader accepts versions 1, 2, 3, and 4. Saving always writes version 4.

## Header

```text
PCKEY_CONFIG <version>
```

Current version: `4`.

## Active profile

```text
active "<profile name>"
```

The reserved built-in profile name is `普通模式`.

## Profile

```text
profile "<name>" <layer count> <layout preset>
...
endprofile
```

Profiles may contain 1～32 layers.

Layout preset values:

- `0`: 104-key full size;
- `1`: 87-key TKL;
- `2`: 75%;
- `3`: 65%;
- `4`: 60%;
- `5`: generic laptop.

## Mouse settings

```text
mouse <initial speed>
      <maximum speed>
      <acceleration ms>
      <repeat ms>
      <wheel step>
```

Validation ranges:

- initial speed: `1～100`;
- maximum speed: initial speed through `100`;
- acceleration: `0～5000ms`;
- repeat: `5～100ms`;
- wheel step: non-zero signed 16-bit value.

## Action fields

Actions are stored as one fixed field sequence:

```text
<kind>
<target scan> <target prefix>
<target layer>
<hold scan> <hold prefix>
<tapping term ms> <quick tap term ms>
<virtual key>
<mouse button>
<mouse x> <mouse y> <mouse amount>
<reference id>
<shortcut modifier mask>
```

An ordinary mapped key line is:

```text
action <layer>
       <source scan> <source prefix>
       <action fields>
```

Prefix values:

- `0`: no prefix;
- `1`: E0;
- `2`: E1.

Action kinds:

- `0`: transparent;
- `1`: pass through;
- `2`: block;
- `3`: map to scan-code key;
- `4`: momentary layer;
- `5`: Layer-Tap;
- `6`: Mod-Tap;
- `7`: Windows virtual key;
- `8`: mouse button;
- `9`: mouse movement;
- `10`: mouse wheel;
- `11`: macro reference;
- `12`: stop all macros;
- `13`: Tap Dance reference.
- `14`: shortcut chord.

Shortcut actions use `target scan` and `target prefix` for the non-modifier
key and use the final shortcut modifier mask field. Modifier bits use the
same left/right layout documented under Key Override.

Mouse button values:

- `0`: left;
- `1`: right;
- `2`: middle;
- `3`: X1;
- `4`: X2.

Layer-Tap and Mod-Tap tapping terms are `100～1500ms`. Quick Tap is
`0～500ms`; zero disables it.

Default actions are omitted. Layer 0 defaults to pass-through, while upper
layers default to transparent.

## Macro

```text
macro <id> "<name>"
macroevent <delay ms> <scan> <prefix> <transition>
...
endmacro
```

Transition values:

- `0`: press;
- `1`: repeat — rejected for stored macros;
- `2`: release.

Limits:

- 32 macros per profile;
- 256 events per macro;
- IDs must be non-zero and unique.

## Tap Dance

```text
tapdance <id> "<name>"
         <hold term ms>
         <multi-tap term ms>
         <quick-tap term ms>
tapdanceaction <slot> <action fields>
...
endtapdance
```

Slot values:

- `0`: tap;
- `1`: hold;
- `2`: double tap;
- `3`: tap then hold.

Limits:

- 64 definitions per profile;
- hold term `100～1000ms`;
- multi-tap term `50～500ms`;
- Quick Tap `0～500ms`.

Tap Dance actions may not contain Layer-Tap, Mod-Tap, or another Tap Dance.

## Combo

```text
combo <id> "<name>" <member count> <term ms> <layer mask>
combomember <index> <scan> <prefix>
...
comboaction <action fields>
endcombo
```

Limits and rules:

- 64 definitions per profile;
- 2～4 unique members;
- term `20～300ms`;
- non-zero Layer mask;
- output may not be Layer-Tap, Mod-Tap, or Tap Dance.

Members represent mapped key identities rather than source key positions.

## Key Override

```text
override <id> "<name>"
         <trigger scan> <trigger prefix>
         <required modifier mask>
         <forbidden modifier mask>
         <suppressed modifier mask>
         <exact match 0|1>
         <layer mask>
overrideaction <action fields>
endoverride
```

Modifier bits:

- bit 0: left Ctrl;
- bit 1: left Shift;
- bit 2: left Alt;
- bit 3: left Win;
- bit 4: right Ctrl;
- bit 5: right Shift;
- bit 6: right Alt;
- bit 7: right Win.

An “either side” group sets both bits for that modifier type. Required and
forbidden masks may not overlap. Replacement actions may not contain
Layer-Tap, Mod-Tap, or Tap Dance.

## Draft

The editor writes unapplied changes to:

```text
config.pckey.draft
```

The core never loads the draft. Save and Apply writes the formal
configuration and removes the draft. Discard Changes removes the draft and
restores the last formal snapshot.

## Atomic writes

The editor writes a `.tmp` file first and replaces the destination with
`MoveFileExW`. The core never reads a partially written formal configuration.
