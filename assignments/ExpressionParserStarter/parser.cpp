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

#include "parser.h"
#include "expression.h"

Expression *Parser::FindFactor() {
  if (stream.Match('(')) {
    Expression *inner = FindAddSub();
    if (!inner) {
      return nullptr;
    }
    if (!stream.Match(')')) {
      delete inner;
      return nullptr;
    }
    return inner;
  }
  if (stream.Match('-')) {
    Expression *inner = FindFactor();
    if (!inner) {
      return nullptr;
    }
    return new Subtraction(new Number(0), inner);
  }
  return stream.ParseNumber();
}

Expression *Parser::FindAddSub() {
  Expression *left = FindMulDiv();
  if (!left) {
    return nullptr;
  }

  while (true) {
    if (stream.Match('+')) {
      Expression *right = FindMulDiv();
      if (!right) {
        delete left;
        return nullptr;
      }
      left = new Addition(left, right);
    } else if (stream.Match('-')) {
      Expression *right = FindMulDiv();
      if (!right) {
        delete left;
        return nullptr;
      }
      left = new Subtraction(left, right);
    } else {
      break;
    }
  }
  return left;
}

Expression *Parser::FindMulDiv() {
  Expression *left = FindFactor();
  if (!left) {
    return nullptr;
  }
  while (true) {
    if (stream.Match('*')) {
      Expression *right = FindFactor();
      if (!right) {
        delete left;
        return nullptr;
      }
      left = new Multiplication(left, right);
    } else if (stream.Match('/')) {
      Expression *right = FindFactor();
      if (!right) {
        delete left;
        return nullptr;
      }
      left = new Division(left, right);
    } else {
      break;
    }
  }

  return left;
}

Expression *Parser::Parse() {
  Expression *exp = FindAddSub();
  if (exp && stream.AllConsumed()) {
    return exp;
  }
  delete exp;
  return nullptr;
}

Parser::Parser(const std::string &in) : stream(in) {}
