#include <cstddef>
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
};