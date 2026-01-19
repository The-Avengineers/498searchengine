/*
 * expression.cpp
 *
 * Class implementations for expression functionality.
 *
 * You will have to implement any additional functions you decide
 * to include in expression.h.
 */

#include "expression.h"

Expression::~Expression( )
   {
   }

Number::Number( int64_t val ) :
      value( val )
   {
   }

int64_t Number::Eval( ) const
   {
   return value;
   }
