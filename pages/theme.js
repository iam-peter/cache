// Light or dark: the system's choice until the switch in the header picks
// one, which is then kept for later visits. Loaded in the head, so the page
// never shows in the other colours first.
(function () {
  const root = document.documentElement;
  const system = window.matchMedia("(prefers-color-scheme: dark)");
  let saved = null;
  try { saved = localStorage.getItem("theme"); } catch (e) {}
  if (saved === "light" || saved === "dark") root.dataset.theme = saved;

  function current() {
    return root.dataset.theme || (system.matches ? "dark" : "light");
  }
  function label(button) {
    const dark = current() === "dark";
    button.setAttribute("aria-label", dark ? "Light theme" : "Dark theme");
    button.title = button.getAttribute("aria-label");
  }

  document.addEventListener("DOMContentLoaded", function () {
    const button = document.getElementById("theme-switch");
    if (!button) return;
    label(button);
    system.addEventListener("change", function () { label(button); });
    button.addEventListener("click", function () {
      const next = current() === "dark" ? "light" : "dark";
      root.dataset.theme = next;
      try { localStorage.setItem("theme", next); } catch (e) {}
      label(button);
    });
  });
})();
