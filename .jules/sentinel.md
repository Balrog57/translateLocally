## 2024-02-11 - Zip Slip Vulnerability in Model Extraction
**Vulnerability:** ModelManager was extracting archives using `libarchive` without path sanitization flags, allowing malicious archives to overwrite arbitrary files via `../`.
**Learning:** `libarchive` does not enable security flags like `ARCHIVE_EXTRACT_SECURE_NODOTDOT` by default.
**Prevention:** Always use `ARCHIVE_EXTRACT_SECURE_NODOTDOT`, `ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS`, and `ARCHIVE_EXTRACT_SECURE_SYMLINKS` when extracting archives.
