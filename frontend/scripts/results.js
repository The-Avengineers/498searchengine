(() => {
  "use strict";

  // core defaults + dom hooks for the results page
  const DEFAULT_LIMIT = 10;
  const listEl = document.getElementById("results-list");
  const countEl = document.getElementById("results-count");
  const feedbackEl = document.getElementById("results-feedback");
  const queryInputEl = document.getElementById("results-query");

  // if the expected dom isn't here, just bail quietly
  if (!listEl || !countEl || !feedbackEl || !queryInputEl) {
    return;
  }

  // read query state from the url
  const params = new URLSearchParams(window.location.search);
  const query = (params.get("q") || "").trim();
  const limit = clampLimit(parseInteger(params.get("limit")));

  // keep the search input in sync with what we are showing
  queryInputEl.value = query;

  // no query means no fetch, just show a helpful nudge
  if (query.length === 0) {
    setCount("Enter a query to search.");
    showFeedback("Enter a query in the search box above.", true);
    return;
  }

  // show immediate status so the page feels responsive
  setCount(`Searching for "${query}"...`);
  clearList();
  hideFeedback();

  // fire the request and render whatever comes back
  const requestUrl = buildApiUrl(query, limit);
  void loadSearchResults(requestUrl, query);

  async function loadSearchResults(url, currentQuery) {
    try {
      const payload = await fetchSearch(url);
      renderPayload(payload, currentQuery);
    } catch (error) {
      setCount("Search failed.");
      showFeedback(readErrorMessage(error), false);
    }
  }

  async function fetchSearch(url) {
    // backend contract is get + json
    const response = await fetch(url, {
      method: "GET",
      headers: {
        Accept: "application/json",
      },
    });

    const bodyText = await response.text();
    let payload = {};

    if (bodyText.length > 0) {
      try {
        payload = JSON.parse(bodyText);
      } catch (error) {
        throw new Error("API returned invalid JSON.");
      }
    }

    if (!response.ok) {
      // prefer api-provided errors over generic status text
      const message = firstErrorMessage(payload) || `API request failed (${response.status}).`;
      throw new Error(message);
    }

    return payload;
  }

  function renderPayload(payload, currentQuery) {
    // tolerate partial payloads and missing fields
    const apiErrors = normalizeErrors(payload && payload.errors);
    const results = normalizeResults(payload && payload.results);
    const tookMs = parseInteger(payload && payload.took_ms);
    const totalHits = parseInteger(payload && payload.total_hits);

    clearList();

    if (results.length === 0) {
      const countLine = buildCountLine(0, totalHits, tookMs);
      setCount(countLine);

      if (apiErrors.length > 0) {
        showFeedback(apiErrors.join(" "), false);
      } else {
        showFeedback(`No results found for "${currentQuery}".`, true);
      }
      return;
    }

    // normal success path
    setCount(buildCountLine(results.length, totalHits, tookMs));
    if (apiErrors.length > 0) {
      showFeedback(`Search completed with warnings: ${apiErrors.join(" ")}`, false);
    } else {
      hideFeedback();
    }

    for (const result of results) {
      listEl.appendChild(createResultCard(result));
    }
  }

  function createResultCard(result) {
    // build each result card using only safe text/hrefs
    const item = document.createElement("li");
    item.className = "result-card";

    const urlNode = createLinkOrTextNode("result-url", result.displayUrl, result.safeUrl);
    item.appendChild(urlNode);

    const title = document.createElement("h2");
    title.className = "result-title";
    title.appendChild(createLinkOrTextNode("", result.title, result.safeUrl));
    item.appendChild(title);

    if (result.snippet.length > 0) {
      const snippet = document.createElement("p");
      snippet.className = "result-snippet";
      snippet.textContent = result.snippet;
      item.appendChild(snippet);
    }

    if (result.score !== null) {
      const meta = document.createElement("p");
      meta.className = "result-meta";
      meta.textContent = `Score: ${result.score}`;
      item.appendChild(meta);
    }

    return item;
  }

  function createLinkOrTextNode(className, text, href) {
    // if we got a valid url, make it clickable; otherwise plain text
    if (href.length > 0) {
      const link = document.createElement("a");
      if (className.length > 0) {
        link.className = className;
      }
      link.href = href;
      link.textContent = text;
      link.target = "_blank";
      link.rel = "noopener noreferrer";
      return link;
    }

    const span = document.createElement("span");
    if (className.length > 0) {
      span.className = className;
    }
    span.textContent = text;
    return span;
  }

  function normalizeResults(rawResults) {
    // accept a few common key variants so frontend isn't brittle
    if (!Array.isArray(rawResults)) {
      return [];
    }

    const output = [];
    for (const raw of rawResults) {
      if (!raw || typeof raw !== "object") {
        continue;
      }

      const url = pickString(raw, ["url", "link", "href", "document_url", "documentUrl"]);
      const safeUrl = sanitizeUrl(url);
      const title =
        pickString(raw, ["title", "name", "headline"]) || (safeUrl.length > 0 ? safeUrl : "Untitled result");
      const snippet = pickString(raw, ["snippet", "description", "summary", "extract", "body"]);
      const score = formatScore(raw.score);
      const displayUrl = safeUrl.length > 0 ? compactUrl(safeUrl) : "URL unavailable";

      output.push({
        safeUrl,
        displayUrl,
        title,
        snippet,
        score,
      });
    }
    return output;
  }

  function normalizeErrors(rawErrors) {
    // keep only non-empty message strings
    if (!Array.isArray(rawErrors)) {
      return [];
    }

    const messages = [];
    for (const rawError of rawErrors) {
      if (rawError && typeof rawError === "object" && typeof rawError.message === "string") {
        const trimmed = rawError.message.trim();
        if (trimmed.length > 0) {
          messages.push(trimmed);
        }
      }
    }
    return messages;
  }

  function firstErrorMessage(payload) {
    // grab the first user-facing api error if present
    const errors = normalizeErrors(payload && payload.errors);
    return errors.length > 0 ? errors[0] : "";
  }

  function compactUrl(url) {
    // trim protocol to keep the result url line cleaner
    try {
      const parsed = new URL(url);
      const path = parsed.pathname === "/" ? "" : parsed.pathname;
      return `${parsed.host}${path}${parsed.search}`;
    } catch (error) {
      return url;
    }
  }

  function sanitizeUrl(url) {
    // only allow http/https links into the dom
    if (typeof url !== "string" || url.trim().length === 0) {
      return "";
    }

    try {
      const parsed = new URL(url);
      if (parsed.protocol === "http:" || parsed.protocol === "https:") {
        return parsed.href;
      }
      return "";
    } catch (error) {
      return "";
    }
  }

  function pickString(source, keys) {
    // return the first non-empty string in priority order
    for (const key of keys) {
      if (typeof source[key] === "string") {
        const trimmed = source[key].trim();
        if (trimmed.length > 0) {
          return trimmed;
        }
      }
    }
    return "";
  }

  function clampLimit(value) {
    // backend currently expects a tiny limit window
    if (!Number.isInteger(value) || value <= 0) {
      return DEFAULT_LIMIT;
    }
    if (value > DEFAULT_LIMIT) {
      return DEFAULT_LIMIT;
    }
    return value;
  }

  function parseInteger(value) {
    // parse loosely, return null when not usable
    const parsed = Number.parseInt(value, 10);
    return Number.isFinite(parsed) ? parsed : null;
  }

  function formatScore(rawScore) {
    // normalize score display to a consistent precision
    if (typeof rawScore === "number" && Number.isFinite(rawScore)) {
      return rawScore.toFixed(3);
    }
    return null;
  }

  function buildApiUrl(queryText, limitValue) {
    // same-origin path; deployment routes /api/* to backend
    const queryParams = new URLSearchParams();
    queryParams.set("q", queryText);
    queryParams.set("limit", String(limitValue));
    return `/api/search?${queryParams.toString()}`;
  }

  function buildCountLine(renderedCount, totalHits, tookMs) {
    // show backend totals when available, fallback to rendered count
    const count = Number.isInteger(totalHits) && totalHits >= 0 ? totalHits : renderedCount;
    const countPart = `About ${count.toLocaleString()} result${count === 1 ? "" : "s"}`;
    const timingPart = Number.isInteger(tookMs) && tookMs >= 0 ? ` (${tookMs.toLocaleString()} ms)` : "";
    return `${countPart}${timingPart}`;
  }

  function readErrorMessage(error) {
    // unwrap thrown errors into something user-facing
    if (error && typeof error.message === "string" && error.message.trim().length > 0) {
      return error.message;
    }
    return "Unable to connect to the search API.";
  }

  function clearList() {
    listEl.textContent = "";
  }

  function setCount(text) {
    countEl.textContent = text;
  }

  function showFeedback(message, isInfo) {
    feedbackEl.textContent = message;
    feedbackEl.hidden = false;
    feedbackEl.classList.toggle("is-info", Boolean(isInfo));
  }

  function hideFeedback() {
    feedbackEl.textContent = "";
    feedbackEl.hidden = true;
    feedbackEl.classList.remove("is-info");
  }
})();
