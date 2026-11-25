# `.ecxtheme` structural fields

A theme package's `theme.json` (see `ThemePackage.kt`) carries colors plus a set of OPTIONAL
structural fields. Every field below may be omitted; the default reproduces the look of the theme's
`effects.style` exactly as before. Unknown keys are ignored (the parser uses `ignoreUnknownKeys`) and
unknown VALUES fall back to the default, so old apps open new packages and new apps open old ones.
`formatVersion` stays `1`. Values are case-insensitive; `-` and `_` are ignored (`double-border`,
`doubleBorder`, `DOUBLE_BORDER` are the same).

Structure fields are orthogonal to `effects.style` (the UiStyle: standard, neon, classic, luna, s60,
aero, metro, osd, pixel, terminal, paper, bubble) and to `shape` (corner style/scale). A variant other
than `default` REPLACES the painting that the UiStyle would do for that component only.

| Field | Values | Default |
|---|---|---|
| `components.card` | `default` `elevated` `outlined` `flat` `glass` `inset` `notched` `double` (alias `double-border`) | `default` |
| `components.button` | `default` `filled` `outline` `soft` `gel` `raised3d` `underline` `bracket` | `default` |
| `components.chip` | `default` `pill` `square` `tag` | `default` |
| `components.switch` | `default` `track` `checkbox` `led` | `default` |
| `components.header` | `default` `solid` `gradient` `underline` `outlined` `glass` `large-title` | `default` |
| `borders.width` | number 0..3 (dp); absent = each style's own width, `0` removes card borders | absent |
| `borders.accentEdge` | `none` `left` `top` `bottom` (3dp stripe in accent1 on cards) | `none` |
| `background.pattern` | `none` `dots` `grid` `diagonal` `waves` `noise` `circuit` `hex` | `none` |
| `typography.font` | `system` `rubik` `exo2` `monospace` `serif` `package` plus bundled `pixel` `spacemono` `lora` `oswald` | none (app font) |
| `typography.titleCase` | `none` `upper` (screen-header titles) | `none` |
| `typography.letterSpacing` | number -1..6 (sp), added to title/headline styles | `0` |
| `icons.container` | `default` (today's tinted rounded tile) `none` `circle` `squircle` `square` | `default` |

Notes per field:

* **card**: `elevated` shadow (opaque panels only); `outlined` translucent body + outline;
  `flat` no border/shadow; `glass` translucent + light border + top highlight; `inset` darkened body
  with inner top/left shade; `notched` accent triangle in the top-end corner; `double` outline plus an
  inner outline. Notch, inner outline and accent edge are drawn only on card-sized panels (at least
  96x40dp), never on icon boxes or chips. Full-bleed rectangular containers stay undecorated.
* **button**: applies to Button, FilledTonalButton and OutlinedButton; TextButton changes only for
  `underline` and `bracket`. `raised3d` adds a 3dp ledge below the button; `bracket` draws `[ ]`
  corner brackets; `underline` draws a bar under the label. With no explicit `shape`, buttons follow the
  theme's corner style (square in classic/pixel/terminal when `shape.style` is `rounded`).
* **chip**: `tag` cuts the trailing corners. **switch**: `track` forces the Material switch even in
  styles that draw their own; `led` is a lit dot.
* **header**: `solid`/`gradient` paint a primary-colored bar with on-primary content; `underline` is a
  transparent bar with an accent rule; `large-title` is a taller bar with bigger title text.
* **background.pattern**: drawn once into a cached tile and tiled by a shader, above the screens at
  roughly 7-10% ink opacity, so it works with user backgrounds and `background.dim`.
* **fonts** (SIL OFL, licenses in `assets/licenses`): `pixel` Press Start 2P (shrunk to 75%),
  `spacemono` Space Mono, `lora` Lora (serif), `oswald` Oswald (condensed).

## Example 1: terminal, brutal

```json
{
  "typography": {"font": "spacemono", "titleCase": "upper", "letterSpacing": 1.5},
  "effects": {"style": "terminal", "scanlines": true},
  "shape": {"style": "square"},
  "components": {"card": "double-border", "button": "bracket", "chip": "square", "switch": "checkbox", "header": "underline"},
  "borders": {"width": 1, "accentEdge": "left"},
  "background": {"pattern": "grid"},
  "icons": {"container": "square"}
}
```

## Example 2: candy, soft and glossy

```json
{
  "dark": false,
  "shape": {"style": "rounded", "scale": 1.6},
  "typography": {"font": "lora"},
  "effects": {"style": "bubble"},
  "components": {"card": "elevated", "button": "gel", "chip": "pill", "switch": "led", "header": "large-title"},
  "borders": {"accentEdge": "bottom"},
  "background": {"pattern": "dots"},
  "icons": {"container": "circle"}
}
```

## Example 3: glass console, condensed type

```json
{
  "typography": {"font": "oswald", "titleCase": "upper", "letterSpacing": 0.8},
  "effects": {"style": "aero"},
  "shape": {"style": "cut", "scale": 1.0},
  "components": {"card": "glass", "button": "outline", "chip": "tag", "switch": "track", "header": "glass"},
  "borders": {"width": 1.5, "accentEdge": "top"},
  "background": {"pattern": "hex", "dim": 0.5},
  "icons": {"container": "squircle"}
}
```

Sample packages that exercise the parser live in `app/src/test/resources/themes` and are checked by
`ThemeStructureTest`.
