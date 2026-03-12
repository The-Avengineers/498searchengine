/*
 * QueryDecorator.cpp
 */

#include <avengine/query_compiler/QueryDecorator.h>

namespace avengine
{

static const char* FieldPrefixes[] = {"", "#", "@", "/"};

static string PrefixTerm(const char* prefix, const string& term)
{
  string result(prefix);
  result += term;
  return result;
}

static ParseNode* DecorateWord(WordNode* word)
{
  vector<ParseNode*> variants;
  for (size_t f = 0; f < 4; ++f)
  {
    variants.pushBack(new WordNode(PrefixTerm(FieldPrefixes[f], word->term)));
  }
  delete word;
  return new OrNode(variants);
}

static ParseNode* DecoratePhrase(PhraseNode* phrase)
{
  vector<ParseNode*> variants;
  for (size_t f = 0; f < 4; ++f)
  {
    vector<WordNode*> decorated;
    for (size_t w = 0; w < phrase->words.size(); ++w)
    {
      decorated.pushBack(
          new WordNode(PrefixTerm(FieldPrefixes[f], phrase->words[w]->term)));
    }
    variants.pushBack(new PhraseNode(decorated));
  }
  delete phrase;
  return new OrNode(variants);
}

static ParseNode* DecorateNode(ParseNode* node)
{
  WordNode* word = dynamic_cast<WordNode*>(node);
  if (word)
    return DecorateWord(word);

  PhraseNode* phrase = dynamic_cast<PhraseNode*>(node);
  if (phrase)
    return DecoratePhrase(phrase);

  AndNode* andNode = dynamic_cast<AndNode*>(node);
  if (andNode)
  {
    for (size_t i = 0; i < andNode->children.size(); ++i)
      andNode->children[i] = DecorateNode(andNode->children[i]);
    for (size_t i = 0; i < andNode->excluded.size(); ++i)
      andNode->excluded[i] = DecorateNode(andNode->excluded[i]);
    return andNode;
  }

  OrNode* orNode = dynamic_cast<OrNode*>(node);
  if (orNode)
  {
    for (size_t i = 0; i < orNode->children.size(); ++i)
      orNode->children[i] = DecorateNode(orNode->children[i]);
    return orNode;
  }

  NotNode* notNode = dynamic_cast<NotNode*>(node);
  if (notNode)
  {
    notNode->child = DecorateNode(notNode->child);
    return notNode;
  }

  return node;
}

ParseNode* QueryDecorator::Decorate(ParseNode* root)
{
  if (!root)
    return nullptr;
  return DecorateNode(root);
}

} // namespace avengine
