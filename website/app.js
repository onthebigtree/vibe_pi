// Vibe Pi — tiny, dependency-free interactions shared across all pages.
// Everything below is defensive: each block no-ops gracefully when its
// target element isn't present, so the same file is safe on index.html,
// download.html and docs.html.

// ── Mobile nav toggle ──────────────────────────────────────────────
const toggle = document.getElementById("navToggle");
const links = document.getElementById("navLinks");
if (toggle && links) {
  toggle.addEventListener("click", () => links.classList.toggle("open"));
  links.addEventListener("click", (e) => {
    if (e.target.tagName === "A") links.classList.remove("open");
  });
}

// ── Reveal-on-scroll ───────────────────────────────────────────────
// Respects prefers-reduced-motion (CSS already shows .reveal in that case;
// here we just skip wiring up the observer so nothing depends on JS).
const reveals = document.querySelectorAll(".reveal");
const reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
if (reveals.length && !reduceMotion && "IntersectionObserver" in window) {
  const io = new IntersectionObserver(
    (entries) =>
      entries.forEach((e) => {
        if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); }
      }),
    { threshold: 0.14 }
  );
  reveals.forEach((el) => io.observe(el));
} else {
  // No observer (reduced motion or unsupported) — show everything immediately.
  reveals.forEach((el) => el.classList.add("in"));
}

// ── "See it work" device mockup — cycle between tool faces ──────────
// Home page only; absent elsewhere, so it self-disables. Paused for
// reduced-motion users (the first face stays shown).
(function cycleMock() {
  const screen = document.getElementById("mockScreen");
  if (!screen) return;
  const faces = Array.from(screen.querySelectorAll(".mock-face"));
  if (faces.length < 2 || reduceMotion) return;

  let i = 0;
  const advance = () => {
    faces[i].classList.remove("is-on");
    i = (i + 1) % faces.length;
    faces[i].classList.add("is-on");
  };
  setInterval(advance, 2600);
})();

// ── Signup ─────────────────────────────────────────────────────────
// Static pages have no backend. To capture real leads, point this at a
// form endpoint (Formspree / your own /api/notify) below. For now it
// confirms locally so the page works standalone.
const NOTIFY_ENDPOINT = ""; // e.g. "https://formspree.io/f/xxxx"

function vibe_notify(event) {
  event.preventDefault();
  const form = event.target;
  const email = form.email.value.trim();
  const msg = document.getElementById("signupMsg");
  const say = (t) => { if (msg) msg.textContent = t; };

  if (NOTIFY_ENDPOINT) {
    fetch(NOTIFY_ENDPOINT, {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify({ email }),
    })
      .then(() => { say("Thanks — you're on the list."); form.reset(); })
      .catch(() => { say("Something went wrong. Try again later."); });
  } else {
    try { localStorage.setItem("vibepi_notify", email); } catch (_) {}
    say("Thanks — you're on the list.");
    form.reset();
  }
  return false;
}
