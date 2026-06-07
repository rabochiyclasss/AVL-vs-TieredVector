#include <cstddef>
#include <algorithm>
#pragma once

template <typename T>
class AVLTree 
{
    public:
		struct Node
		{
			T 		value;
			Node* 	left = nullptr;
			Node* 	right = nullptr;
			int 	height = 1;
			int 	size = 1;

			explicit Node (const T& v) : value(v) {}
		};
	
	private:
		Node* 	root_ = nullptr;
		int		count_ = 0;

		// ------------------------------------------------------------------
    	//  Small helpers that tolerate nullptr children (an empty subtree has
    	//  height 0 and size 0).  Writing them as functions keeps the rotation
    	//  code readable and avoids repeated null checks everywhere.
    	// ------------------------------------------------------------------
		static int h(Node* n)
		{
			if (n != nullptr)
				return n->height;
			else
				return 0;
		}
		static int sz(Node* n)
		{
			if (n != nullptr)
				return n->size;
			else
				return 0;
		}

		// Recompute height & size of `n` from its (already correct) children.
    	// Must be called after we change a node's children (e.g. after a rotation).
		static void update(Node* n)
		{
			n->height = 1 + std::max(h(n->left), h(n->right));
			n->size = 1 + sz(n->left) + sz(n->right);
		}

		// Balance factor = height(left) - height(right).
    	//  >  1  => left heavy  (too tall on the left)
    	//  < -1  => right heavy
    	// AVL invariant keeps this in {-1, 0, +1} for every node.
		static int balanceFactor(Node* n)
		{
			return (h(n->left) - h(n->right));
		}

		// ------------------------------------------------------------------
		//  Right rotation (used to fix a left-heavy node `y`):
		//
		//          y                 x
		//         / \               / \
		//        x   C     --->     A   y
		//       / \                    / \
		//      A   B                  B   C
		// ------------------------------------------------------------------
			static Node* rotateRight(Node* y)
		{
			Node* x = y->left;
			Node* B = x->right;

			x->right = y;
			y->left = B;

			update(y);
			update(x);
			return y;
		}

		//  Left rotation (mirror image, used to fix a right-heavy node `x`)
		static Node* rotateLeft(Node* x)
		{
			Node* y = x->right;
			Node* B = y->left;

			y->left = x;
			x->right = B;

			update(x);
			update(y);
			return y;
		}

		static Node* rebalance(Node* n)
		{
			update(n); //make sure metadata is fresh
			int bf = balanceFactor(n);

			//Left-heavy---------------------------------------------------------
			if(bf > 1)
			{
				// Left-Right case: first rotate the left child left to reduce it
				if (balanceFactor(n->left) < 0)
					n->left = rotateLeft(n->left);
				return rotateRight(n);
			}

			// Right heavy ---------------------------------------------------
			if (bf < -1) {
				// Right-Left case: rotate right child right first.
				if (balanceFactor(n->right) > 0)
					n->right = rotateRight(n->right);
				return rotateLeft(n);
			}
			// Already balanced.
			return n;
		}
};