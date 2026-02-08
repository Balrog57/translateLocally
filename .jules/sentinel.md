## 2026-02-08 - Zip Slip Vulnerability in Model Extraction
**Vulnerability:** The application was extracting tar.gz archives using `libarchive` without setting security flags. This could allow a malicious archive (e.g., a custom model imported by a user) to write files outside the extraction directory using path traversal sequences (like `../../`).
**Learning:** When using `libarchive`s `archive_write_disk`, security flags are not enabled by default. Developers must explicitly opt-in to `ARCHIVE_EXTRACT_SECURE_NODOTDOT`, `ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS`, and `ARCHIVE_EXTRACT_SECURE_SYMLINKS`.
**Prevention:** Always set these security flags when initializing `archive_write_disk` for untrusted archives.
