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

		// Re-balance a single node after an insert/delete may have changed its
    	//  subtree heights.  Returns the (possibly new) subtree root.
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

		//  Recursive insert of `value` so that it lands at position `index`
    	//  (0-based) in the in-order sequence.  After the call, get(index) will
    	//  return `value` and everything that used to be at >= index shifts right
		Node* insertAt(Node* node, std::size_t index, const T& value)
		{
			// reached an empty slot -> create leaf
			if(!node)
				return new Node(value);
			
			std::size_t leftSize = sz(node->left);
			if(index <= leftSize)
				node->left = insertAt(node->left, index, value);
			else
				node->right = insertAt(node->right, index - leftSize - 1, value);
			
				return rebalance(node);
		}

		// Find the node that holds the minimum index of a subtree (its leftmost
    	// node).  Used by erase when a node has two children.
		static Node* leftmost(Node* node) 
		{
			while (node->left)
				node = node->left;
			return node;
		}

		// ------------------------------------------------------------------
    	//  Recursive delete of the element currently at position `index`.
    	// ------------------------------------------------------------------
		Node* eraseAt(Node* node, std::size_t index)
		{
			std::size_t leftSize = sz(node->left);

			if (index < leftSize)
			{
				node->left = eraseAt(node->left, index);
			}
			else if (index > leftSize)
			{
				node->right = eraseAt(node->right, index - leftSize - 1);
			}
			else
			{
				// ---- This is the node to remove (index == leftSize) ----------
				if (!node->left || !node->right)
				{
					// Zero or one child
					Node* child = node->left ? node->left : node->right;
					delete node;
					return child;
				}

				// Two children: copy the value of the in-order successor (the
            	// smallest element in the right subtree) into this node, then
            	// delete that successor (which has at most one child) from the
            	// right subtree.  Successor is at local index 0 of the right side.
				Node* successor = leftmost(node->right);
				node->value = successor->value;
				node->right = eraseAt(node->right, 0);
			}
			return rebalance(node);
		}

		static void destroy(Node* n)
		{
			if(!n) return;
			destroy(n->left);
			destroy(n->right);
			delete n;
		}
	
	public:
		IndexableAVL() = default;
		~IndexableAVL() { destroy(root_); }

		// Non-copyable for simplicity (deep copy is easy to add but unnecessary
		// for the benchmark and would just invite accidental O(N) copies).
		IndexableAVL(const IndexableAVL&)            = delete;
		IndexableAVL& operator=(const IndexableAVL&) = delete;

		// ------------------------------------------------------------------
		//  Public API mirrors a dynamic array / list.
		// ------------------------------------------------------------------

		std::size_t size()  const { 
			return count_; 
		}
		bool        empty() const { 
			return count_ == 0; 
		}

		// Insert `value` so it ends up at position `index`. index may equal size()
		// (append at the end).  Throws if index > size().
		void insert(std::size_t index, const T& value) {
			if (index > count_) throw std::out_of_range("insert index out of range");
			root_ = insertAt(root_, index, value);
			++count_;
		}

		// Convenience: append to the back (the "Append Workload").
		void push_back(const T& value) { insert(count_, value); }

		// Convenience: insert at the very front (the "Front Mutation Workload").
		void push_front(const T& value) { insert(0, value); }

		// Remove and discard the element at `index`. Throws if out of range.
		void erase(std::size_t index) {
			if (index >= count_) throw std::out_of_range("erase index out of range");
			root_ = eraseAt(root_, index);
			--count_;
		}

		// ------------------------------------------------------------------
		//  Random access: return the element at position `index` in O(log N).
		//  We walk down the tree, at each step deciding left/right by comparing
		//  the index with the size of the left subtree.
		// ------------------------------------------------------------------
		const T& get(std::size_t index) const {
			if (index >= count_) throw std::out_of_range("get index out of range");
			Node* node = root_;
			while (true) {
				std::size_t leftSize = sz(node->left);
				if (index < leftSize) {
					node = node->left;
				} else if (index > leftSize) {
					index -= leftSize + 1;   // skip the left subtree + this node
					node = node->right;
				} else {
					return node->value;      // index == leftSize: found it
				}
			}
		}

		// Height of the whole tree.
		int height() const { return h(root_); }

		// Bytes of heap memory consumed by the tree nodes themselves.  This counts
		// the logical node footprint; the real process also pays per-allocation
		// malloc overhead, which we estimate separately in the benchmark.
		std::size_t memoryBytes() const { return count_ * sizeof(Node); }
		static constexpr std::size_t nodeSize() { return sizeof(Node); }
};