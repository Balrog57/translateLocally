## 2025-02-10 - [Path Traversal in Model Extraction]
**Vulnerability:** Model extraction using `libarchive` was vulnerable to path traversal (Zip Slip) because it only used `ARCHIVE_EXTRACT_TIME`. A malicious model archive could overwrite arbitrary files on the system.
**Learning:** `libarchive` does not enable secure extraction flags by default. It assumes the archive is trusted unless told otherwise.
**Prevention:** Always use `ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS | ARCHIVE_EXTRACT_SECURE_SYMLINKS` when extracting archives, especially those from external sources.
