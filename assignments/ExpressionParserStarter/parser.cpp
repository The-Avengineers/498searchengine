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

#include "expression.h"
#include "parser.h"

Expression *Parser::FindFactor( )
   {
   if ( stream.Match('(')){
      Expression *inner = FindAdd();
      if (!inner){
         return nullptr;
      }
      if (!stream.Match(')')){
         delete inner;
         return nullptr;
      }
      return inner;
   }
   return stream.ParseNumber( );
   }

Expression *Parser::FindAdd( )
   {
   Expression *left = FindMultiply();
   if (!left){
      return nullptr;
   }

   while (stream.Match('+')){
      Expression *right = FindMultiply();
      if (!right){
         delete left;
         return nullptr;
      }
      left = new Addition(left, right);
   }
   return left;
   }

Expression *Parser::FindMultiply( )
   {
   Expression *left = FindFactor();
   if (!left){
      return nullptr;
   }

   while (stream.Match('*')){
      Expression *right = FindAdd();
      if (!right){
         delete left;
         return nullptr;
      }
      left = new Multiplication(left, right);
   }
   return left;
   }

Expression *Parser::Parse( )
   {
   Expression *exp = FindAdd();
   if (exp){
      return exp;
   }
   delete exp;
   return nullptr;
   }

Parser::Parser( const std::string &in ) :
      stream( in )
   {
   }
