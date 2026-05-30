# Vibe Pi — Landing Page

Self-contained product landing page. No build step, no dependencies — just three
static files. The hero recreates the device's real dual-ring gauge in SVG and the
whole page follows the device's "calm" palette (cool white on near-black, color
reserved for warnings).

```
website/
  index.html   markup + the SVG device face
  styles.css   the calm theme
  app.js       mobile nav, scroll reveal, signup handler
```

## View it

Open `index.html` in a browser, or serve the folder:

```bash
cd website
python3 -m http.server 5173
# → http://localhost:5173
```

## Deploy

Drop the folder on any static host:

- **GitHub Pages** — push and enable Pages on the `/website` folder
- **Vercel / Netlify** — set the output/root directory to `website`, no build command
- **Cloudflare Pages / S3 / nginx** — copy the three files as-is

## Wire up the signup form

The "Notify me" form works standalone (confirms locally). To capture real
leads, set `NOTIFY_ENDPOINT` in `app.js` to a form backend (e.g. Formspree) or
your own `/api/notify`.

## Customize

- Colors live in the `:root` block of `styles.css` (same tokens as the firmware UI).
- The gauge percentages are the `stroke-dashoffset` values on the two `<circle>`
  rings in `index.html` (lower offset = more filled).
