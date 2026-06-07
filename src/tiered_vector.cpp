#pragma once

	#include <cmath>     // std::sqrt
	#include <cstddef>   // std::size_t
	#include <cstdint>
	#include <stdexcept> // std::out_of_range
	#include <vector>

template <typename T>
class TieredVector {
private:
	// ------------------------------------------------------------------
	//  A single block: a fixed-capacity circular buffer.
	//
	//  `data` is a flat array of length `cap`.  The logical element j
	//  (0 <= j < count) lives at physical slot (head + j) % cap.  Because the
	//  buffer is circular, both ends can grow/shrink cheaply.
	// ------------------------------------------------------------------
	struct Block 
	{
		std::vector<T> data;     // backing storage, size == cap
		std::size_t    head = 0; // physical index of logical element 0
		std::size_t    count = 0;// number of elements currently stored
		std::size_t    cap = 0;  // capacity (== data.size())

		Block() = default;
		explicit Block(std::size_t capacity)
			: data(capacity), cap(capacity) {}

		bool full()  const { return count == cap; }
		bool empty() const { return count == 0; }

		// Map a logical index (0..count-1) to a physical slot in `data`.
		std::size_t phys(std::size_t logical) const 
		{
			std::size_t p = head + logical;
			if (p >= cap) 
			p -= cap;      // single subtraction instead of %
			return p;
		}

		// Read/write logical element j.
		T&       at(std::size_t j)       { return data[phys(j)]; }
		const T& at(std::size_t j) const { return data[phys(j)]; }

		// ---- Insert `value` so it becomes logical element `pos` ----------
		// pos is in [0..count].  Precondition: block is NOT full.
		// We shift whichever side is smaller to keep it cheap; in particular
		// inserting at pos==0 just moves `head` back one slot (no copying of
		// the existing run if pos==0).
		void insert(std::size_t pos, const T& value) 
		{
			if (pos <= count - pos) 
			{     // left part [0..pos) is the smaller side
				// Make room by moving head back one slot and sliding the first
				// `pos` elements one step toward the new head.
				head = (head == 0 ? cap - 1 : head - 1);
				for (std::size_t i = 0; i < pos; ++i)
					data[phys(i)] = data[phys(i + 1)];
			} 
			else 
			{                      // right part [pos..count) is smaller
				// Slide elements [pos..count) one step toward the back.
				for (std::size_t i = count; i > pos; --i)
					data[phys(i)] = data[phys(i - 1)];
			}
			data[phys(pos)] = value;
			++count;
		}

		// ---- Erase logical element `pos` (0..count-1) --------------------
		void erase(std::size_t pos) 
		{
			if (pos <= count - 1 - pos) 
			{ // left side smaller: slide it up
				for (std::size_t i = pos; i > 0; --i)
					data[phys(i)] = data[phys(i - 1)];
				head = (head + 1 == cap ? 0 : head + 1); // drop old front slot
			} 
			else 
			{                      // right side smaller: slide it down
				for (std::size_t i = pos; i + 1 < count; ++i)
					data[phys(i)] = data[phys(i + 1)];
			}
			--count;
		}
	};

	std::vector<Block> blocks_;   // the top tier: an array of blocks
	std::size_t        count_ = 0;// total number of elements over all blocks
	std::size_t        target_ = 1;// desired ~block size (~sqrt of count)
	std::size_t        nextRebuild_ = 2; // rebuild when count_ reaches this

	// Target block size for the current element count: ceil(sqrt(N)), min 1.
	static std::size_t targetFor(std::size_t n) 
	{
		if (n < 4) return 4;                       // avoid tiny degenerate blocks
		std::size_t t = static_cast<std::size_t>(std::sqrt((double)n));
		return t < 1 ? 1 : t;
	}

	// ------------------------------------------------------------------
	//  Full rebuild: flatten everything and re-pack into fresh blocks whose
	//  capacity is 2*target (so each starts half full and has room to grow
	//  before it must split).  O(N).  Called only when count_ crosses a
	//  power-of-two boundary, so it is geometrically rare => amortized O(1).
	// ------------------------------------------------------------------
	void rebuild() 
	{
		target_ = targetFor(count_);
		std::size_t cap = target_ * 2;             // give each block slack

		std::vector<Block> fresh;
		if (count_ == 0) {                         // keep at least one block
			fresh.emplace_back(cap);
			blocks_.swap(fresh);
			nextRebuild_ = 2;
			return;
		}

		std::size_t numBlocks = (count_ + target_ - 1) / target_; // ceil
		fresh.reserve(numBlocks);

		// Walk the old structure in order, copying `target_` elements into
		// each fresh block.
		Block cur(cap);
		for (std::size_t b = 0; b < blocks_.size(); ++b) 
		{
			Block& ob = blocks_[b];
			for (std::size_t j = 0; j < ob.count; ++j) 
			{
				cur.data[cur.count] = ob.at(j);    // fresh block: head==0
				++cur.count;
				if (cur.count == target_) 
				{        // this fresh block is full enough
					fresh.push_back(std::move(cur));
					cur = Block(cap);
				}
			}
		}
		if (cur.count > 0) fresh.push_back(std::move(cur));
		blocks_.swap(fresh);

		// Schedule the next rebuild at the next power-of-two-ish boundary so
		// rebuilds get geometrically rarer as the structure grows.
		nextRebuild_ = (count_ < 2 ? 2 : count_ * 2);
	}

	// Locate the block containing global index `index`, returning the block
	// number and writing the in-block local index into `local`.  O(#blocks).
	std::size_t locate(std::size_t index, std::size_t& local) const 
	{
		std::size_t b = 0;
		while (b < blocks_.size() && index >= blocks_[b].count) 
		{
			index -= blocks_[b].count;
			++b;
		}
		local = index;
		return b;
	}

public:
	// Start with a single empty block so the structure is always usable.
	TieredVector() { blocks_.emplace_back(targetFor(0) * 2); target_ = targetFor(0); }

	std::size_t size()  const { return count_; }
	bool        empty() const { return count_ == 0; }

	// ------------------------------------------------------------------
	//  Random access.  Scan blocks to find the right one, then index into it.
	// ------------------------------------------------------------------
	const T& get(std::size_t index) const 
	{
		if (index >= count_) throw std::out_of_range("get index out of range");
		std::size_t local;
		std::size_t b = locate(index, local);
		return blocks_[b].at(local);
	}

	// ------------------------------------------------------------------
	//  Insert `value` at global position `index` (0..size()).
	// ------------------------------------------------------------------
	void insert(std::size_t index, const T& value) 
	{
		if (index > count_) throw std::out_of_range("insert index out of range");

		std::size_t local;
		std::size_t b = locate(index, local);
		if (b == blocks_.size()) b = blocks_.size() - 1, local = blocks_[b].count;

		Block& blk = blocks_[b];

		// If the chosen block is full, split it into two half-sized blocks so
		// there is room.  This keeps individual blocks near the target size.
		if (blk.full()) 
		{
			splitBlock(b);
			// After splitting, re-decide which half the local index lands in.
			if (local > blocks_[b].count) 
			{
				local -= blocks_[b].count;
				++b;
			}
		}
		blocks_[b].insert(local, value);
		++count_;

		// Periodic global rebuild keeps block size ~ sqrt(N) as N grows.
		if (count_ >= nextRebuild_) rebuild();
	}

	void push_back(const T& value)  { insert(count_, value); }
	void push_front(const T& value) { insert(0, value); }

	// ------------------------------------------------------------------
	//  Erase the element at global position `index`.
	// ------------------------------------------------------------------
	void erase(std::size_t index) 
	{
		if (index >= count_) throw std::out_of_range("erase index out of range");

		std::size_t local;
		std::size_t b = locate(index, local);
		blocks_[b].erase(local);
		--count_;

		// Drop a block that became empty (but always keep at least one).
		if (blocks_[b].empty() && blocks_.size() > 1)
			blocks_.erase(blocks_.begin() + b);

		// Rebuild on shrinking too, so we don't end up with far too many tiny
		// blocks relative to the current N.
		if (count_ > 0 && blocks_.size() > 3 * target_ + 4)
			rebuild();
	}

private:
	// Split the (full) block at position `b` into two blocks of half the
	// elements each, giving both room to grow again.
	void splitBlock(std::size_t b) 
	{
		Block& src = blocks_[b];
		std::size_t half = src.count / 2;
		std::size_t cap  = target_ * 2 + 2;     // a little extra slack

		Block left(cap), right(cap);
		for (std::size_t j = 0; j < half; ++j)        left.data[left.count++]   = src.at(j);
		for (std::size_t j = half; j < src.count; ++j) right.data[right.count++] = src.at(j);

		blocks_[b] = std::move(left);
		blocks_.insert(blocks_.begin() + b + 1, std::move(right));
	}

public:
	// ------------------------------------------------------------------
	//  Memory accounting.
	//
	//  The Tiered Vector trades memory for speed: each block's circular buffer
	//  is allocated to `cap` but only `count` slots are in use, so there is
	//  always some unused capacity.  These helpers let the benchmark report
	//  both the live payload and the total reserved capacity.
	// ------------------------------------------------------------------
	std::size_t numBlocks() const { return blocks_.size(); }

	// Total element slots reserved across all blocks (used + unused).
	std::size_t reservedSlots() const 
	{
		std::size_t s = 0;
		for (const auto& b : blocks_) s += b.cap;
		return s;
	}

	// Approximate heap bytes: reserved element slots + per-block bookkeeping +
	// the top-level vector of blocks.
	std::size_t memoryBytes() const 
	{
		std::size_t elemBytes  = reservedSlots() * sizeof(T);
		std::size_t blockMeta  = blocks_.size() * sizeof(Block);
		return elemBytes + blockMeta;
	}
};
