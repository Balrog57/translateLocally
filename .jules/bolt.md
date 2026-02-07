## 2024-05-22 - [Optimization] std::async overhead vs string copy
**Learning:** `std::async` with string arguments by value incurs a copy. For simple O(N) operations like word counting, synchronous execution with `std::string_view` is faster and avoids allocation.
**Action:** Use `std::string_view` for read-only string operations and prefer synchronous execution for lightweight tasks.
