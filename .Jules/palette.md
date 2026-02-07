## 2024-05-22 - Default Accessibility Missing
**Learning:** Core widgets like `QPlainTextEdit` and `QComboBox` in this codebase lack default `accessibleName` properties, relying on visual labels which may not be associated programmatically.
**Action:** Audit all interactive widgets in `.ui` files for missing `accessibleName` attributes.
