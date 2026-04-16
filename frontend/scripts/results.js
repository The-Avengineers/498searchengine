(() => {
  "use strict";

  const NAMED_HTML_ENTITIES = {
    amp: "&",
    lt: "<",
    gt: ">",
    quot: "\"",
    apos: "'",
    nbsp: " ",
  };

  // core defaults + dom hooks for the results page
  const DEFAULT_PAGE_SIZE = 10;
  const MAX_PAGE_SIZE = 10;
  // Fetch this many results from the API in one shot, then paginate client-side.
  const PREFETCH_LIMIT = 100;
  const listEl = document.getElementById("results-list");
  const countEl = document.getElementById("results-count");
  const timingEl = document.getElementById("results-timing");
  const feedbackEl = document.getElementById("results-feedback");
  const queryInputEl = document.getElementById("results-query");
  const paginationEl = document.getElementById("results-pagination");

  // if the expected dom isn't here, just bail quietly
  if (!listEl || !countEl || !timingEl || !feedbackEl || !queryInputEl) {
    return;
  }

  // read query state from the url
  const params = new URLSearchParams(window.location.search);
  const query = (params.get("q") || "").trim();
  const pageSize = clampLimit(parseInteger(params.get("limit")));
  const requestedPage = clampPage(parseInteger(params.get("page")));

  // keep the search input in sync with what we are showing
  queryInputEl.value = query;

  // no query means no fetch, just show a helpful nudge
  if (query.length === 0) {
    setCount("Enter a query to search.");
    hideElapsedTime();
    showFeedback("Enter a query in the search box above.", true);
    hidePagination();
    return;
  }

  // show immediate status so the page feels responsive
  setCount(`Searching for "${query}"...`);
  clearList();
  hideElapsedTime();
  hideFeedback();
  hidePagination();

  // fire the request (or serve from cache) and render
  void loadSearchResults(query, requestedPage, pageSize);

  async function loadSearchResults(currentQuery, currentPage, limitValue) {
    const cacheKey = "avcache:" + currentQuery;
    const cached = readCache(cacheKey);

    if (cached) {
      // serve from sessionStorage — no network round-trip
      renderFromCache(cached, currentQuery, currentPage, limitValue);
      return;
    }

    // cache miss: fetch all 100 results at once
    const startedAtMs = nowMs();
    const requestUrl = buildApiUrl(currentQuery, 1, PREFETCH_LIMIT);
    try {
      const payload = await fetchSearch(requestUrl);
      setElapsedTime(elapsedSince(startedAtMs));
      const results = normalizeResults(payload && payload.results);
      writeCache(cacheKey, results);
      renderFromCache(results, currentQuery, currentPage, limitValue);
    } catch (error) {
      setElapsedTime(elapsedSince(startedAtMs));
      setCount("Search failed.");
      showFeedback(readErrorMessage(error), false);
      hidePagination();
    }
  }

  function readCache(key) {
    try {
      const raw = sessionStorage.getItem(key);
      if (!raw) return null;
      const parsed = JSON.parse(raw);
      return Array.isArray(parsed) ? parsed : null;
    } catch (e) {
      return null;
    }
  }

  function writeCache(key, results) {
    try {
      sessionStorage.setItem(key, JSON.stringify(results));
    } catch (e) {
      // storage quota exceeded — just skip caching
    }
  }

  function renderFromCache(allResults, currentQuery, currentPage, limitValue) {
    clearList();

    if (allResults.length === 0) {
      setCount(buildCountLine(0, 0, 0, 0));
      hidePagination();
      showFeedback(`No results found for "${currentQuery}".`, true);
      return;
    }

    const totalHits = allResults.length;
    const totalPages = Math.ceil(totalHits / limitValue);
    const safePage = Math.min(Math.max(currentPage, 1), totalPages);
    const from = (safePage - 1) * limitValue;
    const to = Math.min(from + limitValue, totalHits);
    const pageResults = allResults.slice(from, to);

    setCount(buildCountLine(from + 1, to, pageResults.length, totalHits));
    hideFeedback();

    for (const result of pageResults) {
      listEl.appendChild(createResultCard(result));
    }

    renderPagination(currentQuery, limitValue, safePage, totalPages);
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
      const titleSource =
        pickString(raw, ["title", "name", "headline"]) || (safeUrl.length > 0 ? safeUrl : "Untitled result");
      const title = decodeHtmlEntities(titleSource);
      const snippet = decodeHtmlEntities(
        pickString(raw, ["snippet", "description", "summary", "extract", "body"])
      );
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

  function decodeHtmlEntities(input) {
    if (typeof input !== "string" || input.indexOf("&") === -1) {
      return input;
    }

    return input.replace(/&(#(?:[xX][0-9A-Fa-f]+|\d+)|[A-Za-z][A-Za-z0-9]{1,31});?/g, (match, entity) => {
      if (entity.charAt(0) === "#") {
        const isHex = entity.charAt(1) === "x" || entity.charAt(1) === "X";
        const digits = isHex ? entity.slice(2) : entity.slice(1);
        const radix = isHex ? 16 : 10;
        const codePoint = Number.parseInt(digits, radix);
        if (!Number.isInteger(codePoint) || codePoint < 0 || codePoint > 0x10ffff) {
          return match;
        }
        if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
          return match;
        }
        try {
          return String.fromCodePoint(codePoint);
        } catch (error) {
          return match;
        }
      }

      const named = entity.toLowerCase();
      return Object.prototype.hasOwnProperty.call(NAMED_HTML_ENTITIES, named)
        ? NAMED_HTML_ENTITIES[named]
        : match;
    });
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
    // backend pages in groups of 10, so keep the UI honest
    if (!Number.isInteger(value) || value <= 0) {
      return DEFAULT_PAGE_SIZE;
    }
    if (value > MAX_PAGE_SIZE) {
      return MAX_PAGE_SIZE;
    }
    return value;
  }

  function clampPage(value) {
    if (!Number.isInteger(value) || value <= 0) {
      return 1;
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

  function buildApiUrl(queryText, pageNumber, limitValue) {
    // same-origin path; deployment routes /api/* to backend
    const queryParams = new URLSearchParams();
    queryParams.set("q", queryText);
    queryParams.set("page", String(pageNumber));
    queryParams.set("limit", String(limitValue));
    return `/api/search?${queryParams.toString()}`;
  }

  function buildResultsPageUrl(queryText, pageNumber, limitValue) {
    const queryParams = new URLSearchParams();
    queryParams.set("q", queryText);
    queryParams.set("page", String(pageNumber));
    queryParams.set("limit", String(limitValue));
    return `./results.html?${queryParams.toString()}`;
  }

  function createPaginationNode(label, queryText, targetPage, limitValue, isCurrent, isDisabled) {
    const node = document.createElement(isCurrent || isDisabled ? "span" : "a");
    node.className = "pagination-link";

    if (isCurrent) {
      node.className += " is-current";
    }
    if (isDisabled) {
      node.className += " is-disabled";
    }
    if (!isCurrent && !isDisabled) {
      node.href = buildResultsPageUrl(queryText, targetPage, limitValue);
    }
    node.textContent = label;
    return node;
  }

  function createPaginationGap() {
    const gap = document.createElement("span");
    gap.className = "pagination-gap";
    gap.textContent = "…";
    return gap;
  }

  function renderPagination(queryText, limitValue, currentPage, totalPages) {
    if (!paginationEl) {
      return;
    }

    paginationEl.textContent = "";
    if (!Number.isInteger(totalPages) || totalPages <= 1) {
      paginationEl.hidden = true;
      return;
    }

    paginationEl.hidden = false;
    paginationEl.appendChild(
      createPaginationNode("Prev", queryText, currentPage - 1, limitValue, false, currentPage === 1)
    );

    const windowStart = Math.max(1, currentPage - 2);
    const windowEnd = Math.min(totalPages, currentPage + 2);

    if (windowStart > 1) {
      paginationEl.appendChild(createPaginationNode("1", queryText, 1, limitValue, currentPage === 1, false));
      if (windowStart > 2) {
        paginationEl.appendChild(createPaginationGap());
      }
    }

    for (let page = windowStart; page <= windowEnd; ++page) {
      paginationEl.appendChild(
        createPaginationNode(String(page), queryText, page, limitValue, page === currentPage, false)
      );
    }

    if (windowEnd < totalPages) {
      if (windowEnd < totalPages - 1) {
        paginationEl.appendChild(createPaginationGap());
      }
      paginationEl.appendChild(
        createPaginationNode(
          String(totalPages),
          queryText,
          totalPages,
          limitValue,
          totalPages === currentPage,
          false
        )
      );
    }

    paginationEl.appendChild(
      createPaginationNode(
        "Next",
        queryText,
        currentPage + 1,
        limitValue,
        false,
        currentPage === totalPages
      )
    );
  }

  function hidePagination() {
    if (!paginationEl) {
      return;
    }

    paginationEl.textContent = "";
    paginationEl.hidden = true;
  }

  function buildCountLine(from, to, loadedCount, totalHits) {
    if (!Number.isInteger(loadedCount) || loadedCount <= 0) {
      return "About 0 results";
    }

    if (Number.isInteger(totalHits) && totalHits > 0) {
      return `Showing ${from.toLocaleString()}-${to.toLocaleString()} of ${totalHits.toLocaleString()} result${totalHits === 1 ? "" : "s"}`;
    }

    return `Showing ${from.toLocaleString()}-${to.toLocaleString()} of ${loadedCount.toLocaleString()} loaded result${loadedCount === 1 ? "" : "s"}`;
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

  function nowMs() {
    if (typeof performance === "object" && performance && typeof performance.now === "function") {
      return performance.now();
    }
    return Date.now();
  }

  function elapsedSince(startedAtMs) {
    const elapsedMs = Math.round(nowMs() - startedAtMs);
    return elapsedMs >= 0 ? elapsedMs : 0;
  }

  function setElapsedTime(elapsedMs) {
    timingEl.textContent = `Elapsed time: ${elapsedMs.toLocaleString()} ms`;
    timingEl.hidden = false;
  }

  function hideElapsedTime() {
    timingEl.textContent = "";
    timingEl.hidden = true;
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
