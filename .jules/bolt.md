## 2024-05-22 - Avoid std::async for Trivial Operations
**Learning:** Dispatching trivial O(N) operations like word counting to `std::async` introduces overhead (scheduling, copying) that outweighs parallelism benefits, especially when the main thread blocks on the result.
**Action:** Prefer synchronous execution for lightweight operations, using `std::string_view` to avoid data copying.
## 2024-06-14 - CI Runner Deprecations and Updates
**Learning:** GitHub Actions runners for  and  are deprecated and causing timeouts/failures. Actions  (checkout, cache, upload-artifact) are also deprecated.
**Action:** Migrated to  actions (except  which stays at ) and updated runners to / and . For , use  when aggregating artifacts.
## 2024-06-14 - CI Runner Deprecations and Updates
**Learning:** GitHub Actions runners for macos-12 and ubuntu-20.04 are deprecated and causing timeouts/failures. Actions v3 (checkout, cache, upload-artifact) are also deprecated.
**Action:** Migrated to v4 actions (except jurplel/install-qt-action which stays at v3) and updated runners to macos-13/macos-14 and ubuntu-22.04. For download-artifact@v4, use merge-multiple: true when aggregating artifacts.
