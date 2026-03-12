/*
 * ParseNode.cpp
 *
 * Implementation of ParseNode types.
 * to string functions was just for debugging
 */

#include <avengine/query_compiler/ParseNode.h>

namespace avengine
{

static string MakePad(int indent)
{
  string pad;
  for (int i = 0; i < indent; ++i)
    pad.pushBack(' ');
  return pad;
}

ParseNode::~ParseNode() {}

// WordNode
WordNode::WordNode(const string& t) : term(t) {}

string WordNode::ToString(int indent) const
{
  string result = MakePad(indent);
  result += "Word(";
  result += term;
  result += ")";
  return result;
}

// PhraseNode
PhraseNode::PhraseNode(vector<WordNode*> w) : words(w) {}

PhraseNode::~PhraseNode()
{
  for (size_t i = 0; i < words.size(); ++i)
    delete words[i];
}

string PhraseNode::ToString(int indent) const
{
  string result = MakePad(indent);
  result += "Phrase(";
  for (size_t i = 0; i < words.size(); ++i)
  {
    if (i > 0)
      result += ", ";
    result += words[i]->term;
  }
  result += ")";
  return result;
}

// AndNode
AndNode::AndNode(vector<ParseNode*> ch) : children(ch) {}

AndNode::AndNode(vector<ParseNode*> ch, vector<ParseNode*> ex) : children(ch), excluded(ex) {}

AndNode::~AndNode()
{
  for (size_t i = 0; i < children.size(); ++i)
    delete children[i];
  for (size_t i = 0; i < excluded.size(); ++i)
    delete excluded[i];
}

string AndNode::ToString(int indent) const
{
  string pad = MakePad(indent);
  string result = pad;
  result += "And(\n";
  for (size_t i = 0; i < children.size(); ++i)
  {
    result += children[i]->ToString(indent + 2);
    if (i + 1 < children.size())
      result += ",";
    result += "\n";
  }
  if (!excluded.empty())
  {
    result += pad;
    result += "  Excluded:\n";
    for (size_t i = 0; i < excluded.size(); ++i)
    {
      result += excluded[i]->ToString(indent + 4);
      if (i + 1 < excluded.size())
        result += ",";
      result += "\n";
    }
  }
  result += pad;
  result += ")";
  return result;
}

// OrNode
OrNode::OrNode(vector<ParseNode*> ch) : children(ch) {}

OrNode::~OrNode()
{
  for (size_t i = 0; i < children.size(); ++i)
    delete children[i];
}

string OrNode::ToString(int indent) const
{
  string pad = MakePad(indent);
  string result = pad;
  result += "Or(\n";
  for (size_t i = 0; i < children.size(); ++i)
  {
    result += children[i]->ToString(indent + 2);
    if (i + 1 < children.size())
      result += ",";
    result += "\n";
  }
  result += pad;
  result += ")";
  return result;
}

// NotNode
NotNode::NotNode(ParseNode* c) : child(c) {}

NotNode::~NotNode()
{
  delete child;
}

string NotNode::ToString(int indent) const
{
  string pad = MakePad(indent);
  string result = pad;
  result += "Not(\n";
  result += child->ToString(indent + 2);
  result += "\n";
  result += pad;
  result += ")";
  return result;
}

} // namespace avengine
