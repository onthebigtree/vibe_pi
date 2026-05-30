# Vibe Pi — Marketing Site

Self-contained, multi-page product site. No build step, no dependencies — just
static files. The hero recreates the device's real dual-ring gauge in SVG, and
the whole site follows the device's "calm" palette (cool white on near-black,
color reserved for warnings, one soft slate glow).

```
website/
  index.html     home — hero gauge, "see it work" mockup, features, specs, FAQ, CTA
  download.html  get the host app + device (owned separately; uses the shared CSS/JS)
  docs.html      setup, pairing, CLI, troubleshooting (owned separately; shared CSS/JS)
  styles.css     SHARED design system + components for all three pages
  app.js         SHARED behavior — mobile nav, scroll reveal, mockup cycler, signup
  README.md      this file
```

`index.html` is the only page in this folder today; `download.html` and
`docs.html` are built by other contributors but share `styles.css` and `app.js`.

## Shared design system

`styles.css` is the single source of truth for the look across all pages.

- **Tokens** live in the `:root` block at the top — the same palette as the
  firmware UI (`--bg`, `--panel`, `--text`, `--slate`, `--warn`, `--low`,
  `--radius`, `--maxw`). Color is for warnings only; everything else is cool
  white on near-black.
- **Reusable classes** are grouped and commented by section (search the
  "SHARED, REUSABLE CLASSES" banner in `styles.css`). Pages should reuse these
  rather than add one-offs:
  - Layout: `.nav` `.foot` `.page-hero` `.section-head` `.section-lead`
    `.grid` `.grid-2` `.sidebar` (sticky in-page nav, pair with `.grid-2`)
  - Components: `.btn` (`.btn-primary` / `.btn-ghost`), `.card`, `.badge`
    (`.badge-live` / `.badge-warn`), `.note`, `.faq-list` + `.faq-item`
    (`<details>` accordion), and tables — `.spec-table` (row-header, via
    `<th scope="row">`) or `.data-table` / `table.data` (column-header `<thead>`)
  - Motion: `.reveal` (fades in on scroll; auto-shown under reduced motion)

`app.js` is shared too. Every block is defensive and **no-ops when its target
element is absent**, so the same script is safe to load on every page. It loads
at the end of each page via `<script src="app.js"></script>`.

## Canonical nav & footer

Use the same `<header class="nav">` and `<footer class="foot">` on every page so
navigation stays consistent. Links point to `index.html#…`, `download.html` and
`docs.html`. Mark the current page's own nav link with `class="active"`.

## View it

Open `index.html` in a browser, or serve the folder (so relative links between
pages resolve):

```bash
cd website
python3 -m http.server 5173
# → http://localhost:5173
```

## Deploy

Drop the folder on any static host:

- **GitHub Pages** — push and enable Pages on the `/website` folder
- **Vercel / Netlify** — set the root directory to `website`, no build command
- **Cloudflare Pages / S3 / nginx** — copy the files as-is

## Wire up the signup form

The "Notify me" form works standalone (confirms locally). To capture real leads,
set `NOTIFY_ENDPOINT` in `app.js` to a form backend (e.g. Formspree) or your own
`/api/notify`.

## Customize

- Colors and spacing live in the `:root` block of `styles.css` (same tokens as
  the firmware UI). Change them there and every page updates.
- The hero gauge percentages are the `stroke-dashoffset` values on the two
  `<circle>` rings in `index.html` (lower offset = more filled). The rings keep
  the device's real geometry: `r=206` (7-day) and `r=178` (5-hour).
- The "see it work" mockup cycles between tool faces defined in `#mockScreen`;
  the cycler is in `app.js` and pauses under `prefers-reduced-motion`.
