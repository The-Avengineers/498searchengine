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
    feedback: document.register("results-feedback", new Element("p")),
    query: document.register("results-query", new Element("input")),
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
    location: { search: "?q=dog&api=http://127.0.0.1:8080" },
    localStorage: createLocalStorage(),
    AVENGINE_API_BASE: undefined,
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
    "http://127.0.0.1:8080/api/search?q=dog&limit=10",
    "frontend should call expected api endpoint"
  );
  assert.equal(refs.query.value, "dog", "query box should reflect URL query");
  assert.equal(refs.count.textContent, "About 2 results (12 ms)");
  assert.equal(refs.list.children.length, 2, "two results should render");
  assert.equal(refs.feedback.hidden, true, "feedback banner should be hidden on success");

  const firstCard = refs.list.children[0];
  const urlNode = firstCard.children[0];
  const titleNode = firstCard.children[1].children[0];
  assert.equal(urlNode.textContent, "example.com/dog-one");
  assert.equal(titleNode.textContent, "Dog Result One");
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
  assert.equal(refs.feedback.hidden, false, "feedback banner should be shown");
  assert.equal(refs.feedback.classList.contains("is-info"), true);
}

async function main() {
  await testRendersApiResults();
  await testEmptyQuerySkipsFetch();
  console.log("frontend_results_test passed");
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
