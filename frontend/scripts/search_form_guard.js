(() => {
  "use strict";

  const forms = document.querySelectorAll("form.search-form");
  for (const form of forms) {
    const queryInput = form.querySelector("input[name='q']");
    if (!queryInput) {
      continue;
    }

    form.addEventListener("submit", (event) => {
      if (queryInput.value.trim().length !== 0) {
        return;
      }

      event.preventDefault();
      queryInput.focus();
    });
  }
})();
