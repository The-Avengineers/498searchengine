/*
 * QueryCompiler.h
 *
 * Public entry point for the query compiler.
 * Runs the full pipeline: Lexer → Parser → Decorator → ParseNode*
 */

#pragma once

#include <avengine/query_compiler/ParseNode.h>

namespace avengine
{

// CompileQuery
// REQUIRES: queryString is a null-terminated C string (or nullptr) ex. "apple banana"
// MODIFIES: Nothing
// EFFECTS: Tokenizes, parses, and decorates the query string.
//    Returns a fully decorated ParseNode* tree, or nullptr if the
//    input is null, empty. Caller owns the returned pointer
ParseNode* CompileQuery(const char* queryString);

} // namespace avengine
