/*
 * QueryDecorator.h
 *
 * Expands WordNode and PhraseNode leaves into field-specific
 * variants (body, title, anchor, url) wrapped in OrNodes.
 */

#pragma once

#include <avengine/query_compiler/ParseNode.h>

namespace avengine
{

class QueryDecorator
{
public:
  // Decorate
  // REQUIRES: root is a valid ParseNode* tree (or nullptr)
  // MODIFIES: root (replaces leaf nodes in-place with field-expanded OrNodes)
  // EFFECTS: Walks the tree and replaces each WordNode with an OrNode
  //    of 4 field variants (body, #title, @anchor, /url), and each
  //    PhraseNode with an OrNode of 4 field-decorated PhraseNodes.
  //    Returns the (possibly new) root, or nullptr if root was nullptr.
  //    Caller owns the returned pointer
  static ParseNode* Decorate(ParseNode* root);
};

} // namespace avengine

