// Vibe Pi landing — tiny, dependency-free interactions.

// Mobile nav toggle
const toggle = document.getElementById("navToggle");
const links = document.getElementById("navLinks");
if (toggle) toggle.addEventListener("click", () => links.classList.toggle("open"));
if (links) links.addEventListener("click", (e) => {
  if (e.target.tagName === "A") links.classList.remove("open");
});

// Reveal-on-scroll
const io = new IntersectionObserver(
  (entries) => entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); } }),
  { threshold: 0.14 }
);
document.querySelectorAll(".reveal").forEach((el) => io.observe(el));

// Signup — static page has no backend. To capture real leads, point this at a
// form endpoint (Formspree / your own /api/notify) below. For now it confirms
// locally so the page works standalone.
const NOTIFY_ENDPOINT = ""; // e.g. "https://formspree.io/f/xxxx"

function vibe_notify(event) {
  event.preventDefault();
  const form = event.target;
  const email = form.email.value.trim();
  const msg = document.getElementById("signupMsg");

  if (NOTIFY_ENDPOINT) {
    fetch(NOTIFY_ENDPOINT, {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify({ email }),
    })
      .then(() => { msg.textContent = "Thanks — you're on the list."; form.reset(); })
      .catch(() => { msg.textContent = "Something went wrong. Try again later."; });
  } else {
    try { localStorage.setItem("vibepi_notify", email); } catch (_) {}
    msg.textContent = "Thanks — you're on the list.";
    form.reset();
  }
  return false;
}
