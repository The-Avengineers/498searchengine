/*
 * Parse tree node types for the query compiler
 * ToString methods used for debugging
 */

#pragma once

#include <avengine/string.h>
#include <avengine/vector.h>

namespace avengine
{

class ParseNode
{
public:
  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this
  // EFFECTS: Virtual destructor for polymorphic deletion
  virtual ~ParseNode();

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a human-readable string representation of the
  //    node and its children, indented by indent spaces
  virtual string ToString(int indent = 0) const = 0;
};

class WordNode : public ParseNode
{
public:
  string term;

  // Constructor
  // REQUIRES: t is a non-empty search term
  // MODIFIES: *this
  // EFFECTS: Creates a WordNode representing a single search term
  WordNode(const string& t);

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns "Word(<term>)" indented by indent spaces
  string ToString(int indent = 0) const override;
};

class PhraseNode : public ParseNode
{
public:
  vector<WordNode*> words;

  // Constructor
  // REQUIRES: w is non-empty, caller transfers ownership of all pointers
  // MODIFIES: *this
  // EFFECTS: Creates a PhraseNode representing an exact phrase query
  PhraseNode(vector<WordNode*> w);

  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this and all owned WordNode pointers
  // EFFECTS: Deletes every WordNode in words
  ~PhraseNode();

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns "Phrase(<word1>, <word2>, ...)" indented by indent spaces
  string ToString(int indent = 0) const override;
};

class AndNode : public ParseNode
{
public:
  vector<ParseNode*> children;
  vector<ParseNode*> excluded;

  // Constructor
  // REQUIRES: ch is non-empty, caller transfers ownership of all pointers
  // MODIFIES: *this
  // EFFECTS: Creates an AndNode with the given children and no exclusions
  AndNode(vector<ParseNode*> ch);

  // Constructor
  // REQUIRES: ch is non-empty, caller transfers ownership of all pointers
  // MODIFIES: *this
  // EFFECTS: Creates an AndNode with the given children and excluded terms
  AndNode(vector<ParseNode*> ch, vector<ParseNode*> ex);

  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this and all owned children and excluded pointers
  // EFFECTS: Deletes every ParseNode in children and excluded
  ~AndNode();

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a multi-line "And(...)" representation
  string ToString(int indent = 0) const override;
};

class OrNode : public ParseNode
{
public:
  vector<ParseNode*> children;

  // Constructor
  // REQUIRES: ch has at least 2 elements, caller transfers ownership
  // MODIFIES: *this
  // EFFECTS: Creates an OrNode with the given children
  OrNode(vector<ParseNode*> ch);

  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this and all owned children pointers
  // EFFECTS: Deletes every ParseNode in children
  ~OrNode();

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a multi-line "Or(...)" representation
  string ToString(int indent = 0) const override;
};

class NotNode : public ParseNode
{
public:
  ParseNode* child;

  // Constructor
  // REQUIRES: c is non-null, caller transfers ownership
  // MODIFIES: *this
  // EFFECTS: Creates a NotNode that negates the given child
  NotNode(ParseNode* c);

  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this and the owned child pointer
  // EFFECTS: Deletes the child ParseNode
  ~NotNode();

  // ToString
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a multi-line "Not(...)" representation, this is for debugging purposes
  string ToString(int indent = 0) const override;
};

}

