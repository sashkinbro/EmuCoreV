#pragma once

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <stack>
#include <map>

//=============== types ===========================

template<typename T>
class merkle_tree_node
{
public:
   std::shared_ptr<merkle_tree_node> m_parent;
   std::shared_ptr<merkle_tree_node> m_left; 
   std::shared_ptr<merkle_tree_node> m_right;

public:
   //used for propagating sector index
   std::uint32_t m_index;
   //useful for aggregating nodes by level
   std::uint32_t m_depth;

public:
   //used to store any other user context linked to this node
   T m_context;

public:
   merkle_tree_node()
      : m_parent(nullptr), 
        m_left(nullptr), 
        m_right(nullptr), 
        m_index(0)
   {
   }

   bool isLeaf()
   {
      return !m_left && !m_right;
   }
};

template<typename T>
class merkle_tree
{
public:
   //number of nodes is used to make iterating through the tree easier
   std::uint32_t nNodes;
   //number of leaves in the tree - can be usefull
   std::uint32_t nLeaves;
   //root of the merkle tree
   std::shared_ptr<merkle_tree_node<T> > root;
};

//================ generator ==========================

//this generates an empty merkle tree with specified number of leaves
//merkle tree is always a full tree
template<typename T>
std::shared_ptr<merkle_tree<T> > generate_merkle_tree(std::uint32_t nSectors)
{
   // number of nodes is N = 2 * L - 1 if L is number of leaves
   // in case of merkle trees - leaves are sector hashes
   // I am not sure but merkle trees are probably always full trees
   // meaning that each node has 2 children
   std::uint32_t nNodesMax = nSectors * 2 - 1;
   std::uint32_t nNodes = 1;
   std::uint32_t depth = 0;

   std::vector<std::shared_ptr<merkle_tree_node<T> > > children;
   std::vector<std::shared_ptr<merkle_tree_node<T> > > children_temp;

   std::shared_ptr<merkle_tree_node<T> > root = std::make_shared<merkle_tree_node<T> >();
   root->m_depth = depth++;
   children.push_back(root);

   //this is a non recoursive algorithm that iterates through merkle tree
   //level by level going from left to right, from top to bottom
   while(nNodes != nNodesMax)
   {
      for(auto c : children)
      {
         c->m_left = std::make_shared<merkle_tree_node<T> >();
         c->m_left->m_parent = c;
         c->m_left->m_depth = depth;
         children_temp.push_back(c->m_left);
         nNodes++;
         if(nNodes == nNodesMax)
            throw std::runtime_error("Not a full binary tree");

         c->m_right = std::make_shared<merkle_tree_node<T> >();
         c->m_right->m_parent = c;
         c->m_right->m_depth = depth;
         children_temp.push_back(c->m_right);
         nNodes++;
         if(nNodes == nNodesMax)
            break;

         //if algo exits here it means we have unbalanced tree (full tree with last level not completely full)
         //this is not important, just for note
      }

      //move deeper one level
      children.assign(children_temp.begin(), children_temp.end());
      children_temp.clear();
      depth++;
   }

   //return a merkle tree
   std::shared_ptr<merkle_tree<T> > mkt = std::make_shared<merkle_tree<T> >();
   mkt->nNodes = nNodesMax;
   mkt->nLeaves = nSectors;
   mkt->root = root;
   return mkt;
}

//================= walkers =========================

template<typename T>
struct merkle_node_walker
{
   typedef int (type)(std::shared_ptr<merkle_tree_node<T> > node, void* ctx);
};

//this functions walks through tree nodes from top to bottom from left to right
//this is non recoursive walk that goes level by level in depth
template<typename T>
int walk_tree(std::shared_ptr<merkle_tree<T> > mkt, typename merkle_node_walker<T>::type* wlk, void* ctx)
{
   std::uint32_t nNodes = 1;   

   std::vector<std::shared_ptr<merkle_tree_node<T> > > children;
   std::vector<std::shared_ptr<merkle_tree_node<T> > > children_temp;

   children.push_back(mkt->root);
   if(wlk(mkt->root, ctx) < 0)
      return 0;

   //this is a non recoursive algorithm that iterates through merkle tree
   //level by level going from left to right, from top to bottom
   while(nNodes != mkt->nNodes)
   {
      for(auto c : children)
      {
         if(wlk(c->m_left, ctx) < 0)
            return 0;
         children_temp.push_back(c->m_left);
         nNodes++;
         if(nNodes == mkt->nNodes)
            throw std::runtime_error("Not a full binary tree");

         if(wlk(c->m_right, ctx) < 0)
            return 0;
         children_temp.push_back(c->m_right);
         nNodes++;
         if(nNodes == mkt->nNodes)
            break;

         //if algo exits here it means we have unbalanced tree (full tree with last level not completely full)
         //this is not important, just for note
      }

      //move deeper one level
      children.assign(children_temp.begin(), children_temp.end());
      children_temp.clear();
   }

   return 0;
}

//walks from top to bottom from left to right in recoursive manner (in depth)
template<typename T>
int walk_tree_recoursive_forward(const merkle_tree<T>& mkt, typename merkle_node_walker<T>::type* wlk, void* ctx)
{
   std::shared_ptr<merkle_tree_node<T> > currentNode = mkt.root;

   std::stack<std::shared_ptr<merkle_tree_node<T> > > nodeStack;
   nodeStack.push(currentNode);

   do
   {
      do
      {
         while(true)
         {
            wlk(currentNode, ctx);

            if(currentNode->isLeaf())
               break;

            nodeStack.push(currentNode);
            currentNode = currentNode->m_left;
         }

         currentNode = nodeStack.top()->m_right;
         nodeStack.pop();
      }
      while(!nodeStack.empty());
   }
   while(!nodeStack.empty());

   return 0;
}

//================ index tree ==========================

//this is a tree walk indexing function that propagates index from top to bottom, from left to right
template<typename T>
int tree_indexer(std::shared_ptr<merkle_tree_node<T> > node, void* ctx)
{
   if(node->isLeaf())
      return 0;

   int* idx = (int*)ctx;

   //propagate index to left node
   node->m_left->m_index = node->m_index;

   //select next index into right node
   node->m_right->m_index = (*idx)++;

   return 0;
}

//this is a tree_indexer wrapper that keeps state locally
template<typename T>
int index_merkle_tree(std::shared_ptr<merkle_tree<T> > mkt)
{
   int index = 1;
   walk_tree(mkt, tree_indexer, &index);
   return 0;
}

//================= depth slice tree =========================

template<typename T>
struct depth_mapper_context
{
   typedef std::map<std::uint32_t, std::vector<std::shared_ptr<merkle_tree_node<T> > > > type;
   typedef std::uint32_t key_type;
   typedef std::vector<std::shared_ptr<merkle_tree_node<T> > > value_type;
};

template<typename T>
int depth_mapper(std::shared_ptr<merkle_tree_node<T> > node, void* ctx)
{
   typename depth_mapper_context<T>::type* nodeDepthMap = (typename depth_mapper_context<T>::type*)ctx;
   
   auto depthEntryIt = nodeDepthMap->find(node->m_depth);
   if(depthEntryIt == nodeDepthMap->end())
   {
      auto insRes = nodeDepthMap->insert(std::make_pair(node->m_depth, typename depth_mapper_context<T>::value_type()));
      depthEntryIt = insRes.first;
   }

   depthEntryIt->second.push_back(node);

   return 0;
}

template<typename T>
int map_by_depth(std::shared_ptr<merkle_tree<T> > mkt, typename depth_mapper_context<T>::type& nodeDepthMap)
{
   walk_tree(mkt, depth_mapper, &nodeDepthMap);
   return 0;
}

//================ bottom top combiner ==========================

template<typename T>
struct node_combiner
{
   typedef int(type)(std::shared_ptr<merkle_tree_node<T> > result, std::shared_ptr<merkle_tree_node<T> > left, std::shared_ptr<merkle_tree_node<T> > right, void* ctx);
};

template<typename T>
int bottom_top_walk_combine(std::shared_ptr<merkle_tree<T> > mkt, typename node_combiner<T>::type* wlk, void* ctx)
{
   //build depth slice
   typename depth_mapper_context<T>::type nodeDepthMap;
   map_by_depth(mkt, nodeDepthMap);

   //walk from bottom to top
   for(typename depth_mapper_context<T>::type::const_reverse_iterator it = nodeDepthMap.rbegin(); it != nodeDepthMap.rend(); ++it)
   {
      //walk through each node
      for(auto item : it->second)
      {
         //skip leaves
         if(item->isLeaf())
            continue;

         //call walker
         wlk(item, item->m_left, item->m_right, ctx);
      }
   }

   return 0;
}