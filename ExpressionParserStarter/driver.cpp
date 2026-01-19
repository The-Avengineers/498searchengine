/*
 * driver.cpp
 *
 * Driver for the expression parser program
 *
 * You do not have to modify this file, but you may choose to do so.
 *
 * If you do, note that your output is expected to match ours.
 */

#include <iostream>

#include "expression.h"
#include "parser.h"

int main( )
   {
   std::string input;
   std::getline( std::cin, input );

   Parser parser( input );

   Expression *expr = parser.Parse( );

   if ( expr )
      {
      std::cout << expr->Eval( ) << "\n";
      delete expr;
      }
   else
      {
      std::cout << "Syntax error\n";
      }
   }
