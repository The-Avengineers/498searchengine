/*
 * QueryParser.h
 *
 * Recursive descent parser for search queries.
 *
 * Precedence (lowest to highest):
 *   1. OR  ('|')  — combines results from either side
 *   2. AND ('+')  — requires both sides to match
 *   3. NOT ('-')  — excludes the following term
 *   4. Atoms      — a single word, a quoted phrase, or a parenthesized group
 *
 * Returns a ParseNode* tree, or nullptr on error.
 */

#pragma once

#include <avengine/query_compiler/ParseNode.h>
#include <avengine/query_compiler/QueryLexer.h>

namespace avengine
{

class QueryParser
{
  QueryLexer lexer;

  // ParseOrExpr
  // REQUIRES: Nothing
  // MODIFIES: lexer
  // EFFECTS: Parses an Expression (OR level). Returns a ParseNode*
  //    tree or nullptr on failure. Caller owns the returned pointer
  ParseNode* ParseOrExpr();

  // ParseAndExpr
  // REQUIRES: Nothing
  // MODIFIES: lexer
  // EFFECTS: Parses a Term (AND level). Extracts NOT children into
  //    the excluded list. Returns nullptr on failure
  ParseNode* ParseAndExpr();

  // ParseNotPrefix
  // REQUIRES: Nothing
  // MODIFIES: lexer
  // EFFECTS: Parses a Factor (NOT prefix or bare Atom).
  //    Returns nullptr on failure
  ParseNode* ParseNotPrefix();

  // ParseAtom
  // REQUIRES: Nothing
  // MODIFIES: lexer
  // EFFECTS: Parses an Atom (parenthesized group, phrase, or word).
  //    Returns nullptr on failure
  ParseNode* ParseAtom();

  // ParsePhrase
  // REQUIRES: Current token is PHRASE_START
  // MODIFIES: lexer
  // EFFECTS: Parses a quoted phrase. Returns nullptr on failure
  PhraseNode* ParsePhrase();

public:
  // Constructor
  // REQUIRES: input is a null-terminated C string
  // MODIFIES: *this
  // EFFECTS: Constructs a parser that will parse the given query
  QueryParser(const char* input);

  // Parse
  // REQUIRES: Nothing
  // MODIFIES: lexer
  // EFFECTS: Parses the entire query. Returns a ParseNode* tree
  //    or nullptr if the query is empty, malformed, or a standalone NOT.
  //    Caller owns the returned pointer
  ParseNode* Parse();
};

} // namespace avengine

