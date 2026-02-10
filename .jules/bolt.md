## 2024-05-22 - Avoid std::async for Trivial Operations
**Learning:** Dispatching trivial O(N) operations like word counting to `std::async` introduces overhead (scheduling, copying) that outweighs parallelism benefits, especially when the main thread blocks on the result.
**Action:** Prefer synchronous execution for lightweight operations, using `std::string_view` to avoid data copying.
