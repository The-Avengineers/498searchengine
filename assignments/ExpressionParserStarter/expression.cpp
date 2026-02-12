/*
 * expression.cpp
 *
 * Class implementations for expression functionality.
 *
 * You will have to implement any additional functions you decide
 * to include in expression.h.
 */

#include "expression.h"

Expression::~Expression() {}

Number::Number(int64_t val) : value(val) {}

int64_t Number::Eval() const { return value; }

Addition::Addition(Expression *l, Expression *r) : left(l), right(r) {}

Addition::~Addition() {
  delete left;
  delete right;
}

int64_t Addition::Eval() const { return left->Eval() + right->Eval(); }

Multiplication::Multiplication(Expression *l, Expression *r)
    : left(l), right(r) {}

Multiplication::~Multiplication() {
  delete left;
  delete right;
}

int64_t Multiplication::Eval() const { return left->Eval() * right->Eval(); }

Subtraction::Subtraction(Expression *l, Expression *r) : left(l), right(r) {}

Subtraction::~Subtraction() {
  delete left;
  delete right;
}

int64_t Subtraction::Eval() const { return left->Eval() - right->Eval(); }

Division::Division(Expression *l, Expression *r) : left(l), right(r) {}

Division::~Division() {
  delete left;
  delete right;
}

int64_t Division::Eval() const { return left->Eval() / right->Eval(); }