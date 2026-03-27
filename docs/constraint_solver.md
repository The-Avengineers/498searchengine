# Constraint Solver Interface

This document describes the current public interface of the constraint solver.
It is a usage guide for callers of the component, not an internal ISR implementation note.

## Overview

The constraint solver evaluates boolean queries over index streams.

Current pipeline:

`query string -> parse tree -> ISR execution tree -> matching documents`

The solver is responsible for:

- parsing query text into a query syntax tree
- building an ISR execution tree from that syntax tree
- evaluating boolean query logic over term streams
- returning matching documents

This layer does not do ranking.

## Public Interface

The current public entry points are declared in:

- [`include/avengine/constraint_solver/constraint_solver.h`](../include/avengine/constraint_solver/constraint_solver.h)
- [`include/avengine/constraint_solver/isr_source.h`](../include/avengine/constraint_solver/isr_source.h)
- [`include/avengine/constraint_solver/query_ast.h`](../include/avengine/constraint_solver/query_ast.h)

### `ConstraintSolver`

`ConstraintSolver` is the main user-facing class.

It currently provides:

- `ParseResult ParseQuery(const string& query) const`
- `SolveResult SolveQuery(const string& query, const IsrSource& source) const`
- `SolveResult SolveParsed(const QueryNode* root, const IsrSource& source) const`

Behavior:

- `ParseQuery(...)` only parses. It does not touch any ISR source.
- `SolveQuery(...)` parses first, and if parsing succeeds, solves the query.
- `SolveParsed(...)` skips parsing and solves from an already-built AST.

### `ParseResult`

`ParseResult` contains:

- `QueryNode* Root`
- `vector<ParseError> Errors`

Behavior:

- on success, `Root` is non-null and `Errors` is empty
- on parse failure, `Errors` is non-empty
- callers that keep the parse tree are responsible for freeing it with `DestroyQueryTree(...)`

### `SolveResult`

`SolveResult` contains:

- `vector<DocumentMatch> Matches`
- `vector<ParseError> Errors`

Behavior:

- on success, `Errors` is empty and `Matches` contains the matching documents
- on parse failure, `Errors` is populated and `Matches` is empty

### `DocumentMatch`

`DocumentMatch` currently contains:

- `uint32_t DocId`
- `string Url`

Current contract:

- matches are document-level only
- the solver does not currently return per-term posting metadata
- the solver does not currently return a query-restricted inverted index structure

### `IsrSource`

`IsrSource` is the source interface the solver depends on.

It must provide:

- `ISRWord* OpenTermIsr(const string& term) const`
- `ISREndDoc* OpenEndDocIsr() const`
- `string GetDocumentURL(uint32_t docId) const`

Caller responsibilities:

- provide normalized term streams that match the parser/index normalization rules
- provide document boundary metadata through `OpenEndDocIsr()`
- ensure returned ISRs are valid for the duration of the solve call

## Supported Query Syntax

The current parser supports:

- single terms
- quoted phrases
- implicit `AND` by adjacency
- `|` for `OR`
- unary `-` for exclusion inside a positive query
- parentheses for grouping

Examples:

- `apple`
  Matches documents containing `apple`.

- `apple banana`
  Matches documents containing both `apple` and `banana`.

- `apple | banana`
  Matches documents containing either `apple` or `banana`.

- `brown -fox`
  Matches documents containing `brown` but not `fox`.

- `"apollo moon landing"`
  Matches documents where `apollo`, `moon`, and `landing` appear consecutively in order.

- `(apple | banana) cherry`
  Matches documents containing `cherry` and also containing either `apple` or `banana`.

Notes:

- standalone negative queries such as `-fox` are rejected by the parser
- phrase syntax is exact adjacency, not loose proximity
- implicit `AND` binds tighter than `|`

## Usage Flow

### `SolveQuery(...)`

Use `SolveQuery(...)` when the caller has raw query text.

Minimal shape:

```cpp
using namespace avengine;
using namespace avengine::constraint_solver;

class MySource : public IsrSource
{
public:
  ISRWord* OpenTermIsr(const string& term) const override;
  ISREndDoc* OpenEndDocIsr() const override;
  string GetDocumentURL(uint32_t docId) const override;
};

ConstraintSolver solver;
MySource source;

SolveResult result = solver.SolveQuery("apple | banana", source);
if (!result.Errors.empty())
{
  // handle parse failure
}

for (size_t i = 0; i < result.Matches.size(); ++i)
{
  const DocumentMatch& match = result.Matches[i];
  // use match.DocId and match.Url
}
```

### `ParseQuery(...)` and `SolveParsed(...)`

Use this split when the caller wants to parse once and solve separately.

```cpp
ConstraintSolver solver;
ParseResult parsed = solver.ParseQuery("(apple | banana) cherry");

if (parsed.Errors.empty())
{
  SolveResult solved = solver.SolveParsed(parsed.Root, source);
}

DestroyQueryTree(parsed.Root);
```

Important ownership rule:

- if the caller receives a `QueryNode*` from `ParseQuery(...)`, the caller must eventually call `DestroyQueryTree(...)`

## Behavioral Guarantees

The current ISR-based implementation guarantees:

- `AND` only matches when required terms occur in the same document
- `NOT` filters documents using document boundaries
- phrases only match when terms appear consecutively and in order
- results are deduplicated at the document level
- parse errors are returned through `SolveResult.Errors`

Document-boundary behavior depends on `OpenEndDocIsr()`.
If the source does not provide valid document-end metadata, `AND`, `NOT`, and phrase behavior are not defined correctly.
