const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

class ClassList {
  constructor() {
    this.values = new Set();
  }

  toggle(name, force) {
    if (force === undefined) {
      if (this.values.has(name)) {
        this.values.delete(name);
        return false;
      }
      this.values.add(name);
      return true;
    }

    if (force) {
      this.values.add(name);
      return true;
    }

    this.values.delete(name);
    return false;
  }

  remove(name) {
    this.values.delete(name);
  }

  contains(name) {
    return this.values.has(name);
  }
}

class Element {
  constructor(tagName) {
    this.tagName = String(tagName || "div").toUpperCase();
    this.children = [];
    this.className = "";
    this.hidden = false;
    this.value = "";
    this.href = "";
    this.target = "";
    this.rel = "";
    this._textContent = "";
    this.classList = new ClassList();
  }

  set textContent(value) {
    this._textContent = String(value);
    if (this._textContent.length === 0) {
      this.children = [];
    }
  }

  get textContent() {
    return this._textContent;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }
}

class FakeDocument {
  constructor() {
    this.elements = new Map();
  }

  register(id, element) {
    this.elements.set(id, element);
    return element;
  }

  getElementById(id) {
    return this.elements.get(id) || null;
  }

  createElement(tagName) {
    return new Element(tagName);
  }
}

function createLocalStorage() {
  const storage = new Map();
  return {
    getItem(key) {
      return storage.has(key) ? storage.get(key) : null;
    },
    setItem(key, value) {
      storage.set(key, String(value));
    },
  };
}

function buildDom() {
  const document = new FakeDocument();
  const refs = {
    list: document.register("results-list", new Element("ol")),
    count: document.register("results-count", new Element("p")),
    timing: document.register("results-timing", new Element("p")),
    feedback: document.register("results-feedback", new Element("p")),
    query: document.register("results-query", new Element("input")),
    pagination: document.register("results-pagination", new Element("nav")),
  };
  return { document, refs };
}

async function flushMicrotasks() {
  for (let i = 0; i < 6; ++i) {
    await new Promise((resolve) => setImmediate(resolve));
  }
}

function runResultsScript() {
  const scriptPath = path.resolve(__dirname, "..", "frontend", "scripts", "results.js");
  const source = fs.readFileSync(scriptPath, "utf8");
  vm.runInThisContext(source, { filename: scriptPath });
}

async function testRendersApiResults() {
  const { document, refs } = buildDom();
  let capturedUrl = "";

  global.document = document;
  global.window = {
    location: { search: "?q=dog" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };
  global.performance = {
    now: (() => {
      const values = [100, 112];
      return () => values.shift();
    })(),
  };

  global.fetch = async (url) => {
    capturedUrl = url;
    return {
      ok: true,
      status: 200,
      text: async () =>
        JSON.stringify({
          query: "dog",
          page: 1,
          limit: 10,
          total_hits: 2,
          took_ms: 12,
          errors: [],
          results: [
            {
              url: "https://example.com/dog-one",
              title: "Dog Result One",
              snippet: "first result",
              score: 1.0,
            },
            {
              url: "https://example.com/dog-two",
              title: "Dog Result Two",
              snippet: "second result",
              score: 0.5,
            },
          ],
        }),
    };
  };

  runResultsScript();
  await flushMicrotasks();

  assert.equal(
    capturedUrl,
    "/api/search?q=dog&page=1&limit=10",
    "frontend should call expected api endpoint"
  );
  assert.equal(refs.query.value, "dog", "query box should reflect URL query");
  assert.equal(refs.count.textContent, "Showing 1-2 of 2 results");
  assert.equal(refs.timing.textContent, "Elapsed time: 12 ms");
  assert.equal(refs.timing.hidden, false, "elapsed-time line should be visible after a search");
  assert.equal(refs.list.children.length, 2, "two results should render");
  assert.equal(refs.feedback.hidden, true, "feedback banner should be hidden on success");
  assert.equal(refs.pagination.hidden, true, "single-page results should hide pagination");

  const firstCard = refs.list.children[0];
  const urlNode = firstCard.children[0];
  const titleNode = firstCard.children[1].children[0];
  assert.equal(urlNode.textContent, "example.com/dog-one");
  assert.equal(titleNode.textContent, "Dog Result One");
}

async function testPaginatesResultsArray() {
  const { document, refs } = buildDom();
  let capturedUrl = "";

  global.document = document;
  global.window = {
    location: { search: "?q=dog&page=2&limit=25" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };
  global.performance = {
    now: (() => {
      const values = [200, 209];
      return () => values.shift();
    })(),
  };

  global.fetch = async (url) => {
    capturedUrl = url;
    const results = [];
    for (let i = 11; i <= 20; ++i) {
      results.push({
        url: `https://example.com/dog-${i}`,
        title: `Dog Result ${i}`,
        snippet: `result ${i}`,
        score: 1.0 - i * 0.01,
      });
    }

    return {
      ok: true,
      status: 200,
      text: async () =>
        JSON.stringify({
          query: "dog",
          page: 2,
          limit: 10,
          total_hits: 23,
          took_ms: 9,
          errors: [],
          results,
        }),
    };
  };

  runResultsScript();
  await flushMicrotasks();

  assert.equal(capturedUrl, "/api/search?q=dog&page=2&limit=10");
  assert.equal(refs.list.children.length, 10, "page two should show exactly ten results");
  assert.equal(refs.count.textContent, "Showing 11-20 of 23 results");
  assert.equal(refs.timing.textContent, "Elapsed time: 9 ms");
  assert.equal(refs.pagination.hidden, false, "multi-page results should show pagination");
  assert.equal(refs.pagination.children.length, 5, "pager should render prev/pages/next");

  const prev = refs.pagination.children[0];
  const current = refs.pagination.children[2];
  const next = refs.pagination.children[4];
  assert.equal(prev.textContent, "Prev");
  assert.equal(prev.href, "./results.html?q=dog&page=1&limit=10");
  assert.equal(current.textContent, "2");
  assert.equal(next.textContent, "Next");
  assert.equal(next.href, "./results.html?q=dog&page=3&limit=10");

  const firstCard = refs.list.children[0];
  const titleNode = firstCard.children[1].children[0];
  assert.equal(titleNode.textContent, "Dog Result 11");
}

async function testEmptyQuerySkipsFetch() {
  const { document, refs } = buildDom();
  let fetchCalls = 0;

  global.document = document;
  global.window = {
    location: { search: "" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };

  global.fetch = async () => {
    fetchCalls += 1;
    throw new Error("fetch should not be called for empty query");
  };

  runResultsScript();
  await flushMicrotasks();

  assert.equal(fetchCalls, 0, "empty query should not call backend");
  assert.equal(refs.count.textContent, "Enter a query to search.");
  assert.equal(refs.timing.textContent, "");
  assert.equal(refs.timing.hidden, true, "elapsed-time line should stay hidden with no query");
  assert.equal(refs.feedback.hidden, false, "feedback banner should be shown");
  assert.equal(refs.feedback.classList.contains("is-info"), true);
}

async function testFailedSearchStillReportsElapsedTime() {
  const { document, refs } = buildDom();

  global.document = document;
  global.window = {
    location: { search: "?q=dog" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };
  global.performance = {
    now: (() => {
      const values = [50, 83];
      return () => values.shift();
    })(),
  };

  global.fetch = async () => ({
    ok: false,
    status: 503,
    text: async () =>
      JSON.stringify({
        errors: [{ message: "backend unavailable" }],
      }),
  });

  runResultsScript();
  await flushMicrotasks();

  assert.equal(refs.count.textContent, "Search failed.");
  assert.equal(refs.timing.textContent, "Elapsed time: 33 ms");
  assert.equal(refs.timing.hidden, false, "elapsed-time line should be visible after failed fetches");
  assert.equal(refs.feedback.textContent, "backend unavailable");
  assert.equal(refs.pagination.hidden, true, "failed searches should not show pagination");
}

async function testRendersDescriptionFromApiResults() {
  const { document, refs } = buildDom();

  global.document = document;
  global.window = {
    location: { search: "?q=metadata" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };
  global.performance = {
    now: (() => {
      const values = [25, 30];
      return () => values.shift();
    })(),
  };

  global.fetch = async () => ({
    ok: true,
    status: 200,
    text: async () =>
      JSON.stringify({
        query: "metadata",
        page: 1,
        limit: 10,
        total_hits: 1,
        errors: [],
        results: [
          {
            url: "https://example.com/metadata",
            title: "Metadata Result",
            description: "Description from the indexed page.",
            score: 0.75,
          },
        ],
      }),
  });

  runResultsScript();
  await flushMicrotasks();

  const firstCard = refs.list.children[0];
  const descriptionNode = firstCard.children[2];
  assert.equal(descriptionNode.className, "result-snippet");
  assert.equal(descriptionNode.textContent, "Description from the indexed page.");
}

async function testDecodesHtmlEntitiesButKeepsTextSafe() {
  const { document, refs } = buildDom();

  global.document = document;
  global.window = {
    location: { search: "?q=google" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
  };
  global.performance = {
    now: (() => {
      const values = [10, 14];
      return () => values.shift();
    })(),
  };

  global.fetch = async () => ({
    ok: true,
    status: 200,
    text: async () =>
      JSON.stringify({
        query: "google",
        page: 1,
        limit: 10,
        total_hits: 1,
        errors: [],
        results: [
          {
            url: "https://developers.google.com/youtube",
            title:
              "YouTube&nbsp;|&nbsp;Google for Developers &amp; Friends &#39; &#x3C;script&#x3E;alert(1)&#x3C;/script&#x3E;",
            snippet: "Privacy &amp; Terms &#39; &#x3C;b&#x3E;safe&#x3C;/b&#x3E;",
            score: 1.0,
          },
        ],
      }),
  });

  runResultsScript();
  await flushMicrotasks();

  const firstCard = refs.list.children[0];
  const titleNode = firstCard.children[1].children[0];
  const snippetNode = firstCard.children[2];

  assert.equal(
    titleNode.textContent,
    "YouTube | Google for Developers & Friends ' <script>alert(1)</script>"
  );
  assert.equal(snippetNode.textContent, "Privacy & Terms ' <b>safe</b>");
  assert.equal(
    titleNode.textContent.includes("&nbsp;") || titleNode.textContent.includes("&amp;"),
    false,
    "entity sequences should be decoded before render"
  );
}

async function main() {
  await testRendersApiResults();
  await testPaginatesResultsArray();
  await testEmptyQuerySkipsFetch();
  await testFailedSearchStillReportsElapsedTime();
  await testRendersDescriptionFromApiResults();
  await testDecodesHtmlEntitiesButKeepsTextSafe();
  console.log("frontend_results_test passed");
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
