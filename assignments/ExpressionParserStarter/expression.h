/*
 * expression.h
 *
 * Class declarations for expressions
 *
 * You should declare more classes and functionality to match
 * the target grammar. Don't forget to implement them in expression.cpp.
 */

#ifndef EXPRESSION_H_
#define EXPRESSION_H_

#include <stdint.h>

/**
 * Just a plain old expression
 */
class Expression
   {
public:

   virtual ~Expression( );

   virtual int64_t Eval( ) const = 0;
   };
// class Expression

/**
 * A number
 */
class Number: public Expression
   {
protected:

   int64_t value;

public:

   Number( int64_t val );

   int64_t Eval( ) const override;
   };
// class Number


class Addition: public Expression 
   {
protected:

   Expression *left;
   Expression *right;

public:

   Addition(Expression *l, Expression *r);

   ~Addition();

   int64_t Eval() const override;
   };
// addition

class Multiplication: public Expression
   {
protected:

   Expression *left;
   Expression *right;

public:

   Multiplication(Expression *l, Expression *r );

   ~Multiplication();

   int64_t Eval() const override;
   };
// multiplication

class Subtraction: public Expression
   {
protected:

   Expression *left;
   Expression *right;

public:

   Subtraction(Expression *l, Expression *r);

   ~Subtraction();

   int64_t Eval() const override;
   };
// subtraction

#endif /* EXPRESSION_H_ */
