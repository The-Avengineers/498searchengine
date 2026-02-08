/*
 * parser.cpp
 *
 * Implementation of parser.h
 *
 * See parser.h for a full BNF of the grammar to implement.
 *
 * You should implement the different Find( ) functions,
 * as well as any additional functions you declare in parser.h.
 */

#include <avengine/expression.h>
#include <avengine/parser.h>

namespace avengine
{

Expression* Parser::FindFactor()
{
  // TODO
  return nullptr;
}

Expression* Parser::FindAdd()
{
  // TODO
  return nullptr;
}

Expression* Parser::FindMultiply()
{
  // TODO
  return nullptr;
}

Expression* Parser::Parse()
{
  // TODO
  return nullptr;
}

Parser::Parser(const std::string& in) : stream(in) {}
} // namespace avengine
