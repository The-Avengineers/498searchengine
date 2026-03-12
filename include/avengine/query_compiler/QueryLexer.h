/*
 * QueryLexer.h
 *
 * Tokenizer for search queries. Scans a raw query string and
 * produces a flat token list the parser can consume.
 *
 * Token types:
 *   WORD, PHRASE_START, PHRASE_END, AND, OR, NOT,
 *   LPAREN, RPAREN, EOF_TOKEN
 *
 * Implicit AND is inserted between adjacent terms
 */

#pragma once

#include <avengine/string.h>
#include <avengine/vector.h>

namespace avengine
{

enum class TokenType
{
  WORD,
  AND,
  OR,
  NOT,
  LPAREN,
  RPAREN,
  PHRASE_START,
  PHRASE_END,
  EOF_TOKEN
};

struct Token
{
  TokenType type;
  string value;

  Token() : type(TokenType::EOF_TOKEN) {}
  Token(TokenType t, const string& v = "") : type(t), value(v) {}
};

class QueryLexer
{
  vector<Token> tokens;
  size_t pos;

  // NeedsImplicitAnd
  // REQUIRES: prev and cur are valid TokenTypes
  // MODIFIES: Nothing
  // EFFECTS: Returns true if an implicit AND token should be
  //    inserted between two adjacent tokens of these types
  static bool NeedsImplicitAnd(TokenType prev, TokenType cur);

public:
  // Constructor
  // REQUIRES: input is a null-terminated C string
  // MODIFIES: *this
  // EFFECTS: Tokenizes the input query string into a flat token
  //    list, inserting implicit AND between adjacent terms
  QueryLexer(const char* input);

  // Peek
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a const reference to the current token
  //    without consuming it
  const Token& Peek() const;

  // Consume
  // REQUIRES: !AllConsumed()
  // MODIFIES: *this
  // EFFECTS: Returns the current token and advances past it
  Token Consume();

  // Match
  // REQUIRES: Nothing
  // MODIFIES: *this (advances position if matched)
  // EFFECTS: If the current token has type t, consumes it and
  //    returns true. Otherwise returns false
  bool Match(TokenType t);

  // AllConsumed
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if all tokens have been consumed
  //    or the current token is EOF_TOKEN
  bool AllConsumed() const;

  // GetTokens
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a const reference to the full token list
  const vector<Token>& GetTokens() const;
};

// TokenTypeName
// REQUIRES: t is a valid TokenType
// MODIFIES: Nothing
// EFFECTS: Returns the name of the token type as a C string
const char* TokenTypeName(TokenType t);

} // namespace avengine

