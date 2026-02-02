# translateLocally Enhanced - Documentation

**A fork of [translateLocally](https://github.com/XapaJIaMnu/translateLocally) with document processing and AI-powered translation improvement.**

## What's New in This Fork

This enhanced version of translateLocally adds professional document translation capabilities and AI-powered post-editing while maintaining full compatibility with the original project.

### Major Improvements Over Original

| Feature | Original translateLocally | This Enhanced Fork |
|---------|--------------------------|-------------------|
| **Document Support** | Plain text only (stdin/stdout) | DOCX, EPUB, PDF, TXT with structure preservation |
| **Large File Handling** | 10 MB hard limit | Automatic splitting for unlimited file sizes |
| **Translation Quality** | Marian neural translation only | Optional AI improvement via local/cloud LLMs |
| **Structure Preservation** | N/A | Full XHTML/HTML/XML structure preservation |
| **GUI Document Processing** | Not available | File menu, progress dialogs, real-time updates |
| **EPUB Support** | Not available | Professional e-book translation with DOM preservation |
| **AI Integration** | Not available | 5 providers (Ollama, LM Studio, OpenAI, Claude, Gemini) |
| **Progress Tracking** | Basic console output | Real-time GUI progress bars with segment tracking |

### Key Technical Innovations

#### 1. Document Processing Architecture
- **DocumentSplitter**: Intelligent segmentation of documents into ≤8MB chunks
  - TXT: Paragraph-based splitting with structure preservation
  - DOCX: ZIP/XML parsing via libarchive
  - EPUB: XHTML parsing with full DOM preservation
  - PDF: LibreOffice conversion pipeline

- **DocumentMerger**: Reconstruction with perfect structure preservation
  - EPUB: Word-based text replacement algorithm preserving all HTML tags, CSS classes, and formatting
  - DOCX: Full ZIP archive rebuilding with metadata preservation
  - Archive formats: Maintains non-text content (images, stylesheets, metadata)

- **DocumentProcessor**: High-level orchestration API
  - Simple workflow: `open()` → `getSegments()` → `setTranslatedSegments()` → `save()`
  - Thread-safe design for GUI integration

#### 2. AI-Powered Translation Improvement
- **LLMInterface**: Unified abstraction for 5 AI providers
  - **Local**: Ollama (http://localhost:11434), LM Studio (http://localhost:1234)
  - **Cloud**: OpenAI, Claude (Anthropic), Gemini (Google)

- **Synchronized Chunking**: 2000-character chunks (~600-700 tokens) with source/translation alignment to prevent text mismatches

- **Sequential Processing**: maxConcurrent=1 for local LLM stability

- **Optimized Prompts**: Engineered to minimize verbosity and reasoning artifacts

#### 3. GUI Integration
- **File Menu**: "Open Document" and "Save Translation" actions with keyboard shortcuts
- **Settings Dialog**: New "AI Improvement" tab with provider configuration
- **Document Translation Dialog**: Real-time progress bars for translation and AI improvement phases
- **Worker Thread Architecture**: Non-blocking UI during long-running translations

#### 4. Structure Preservation Strategy
- **EPUB XHTML Preservation**: Paragraph-by-paragraph replacement approach
  - Preserves paragraph structure: `<p>`, heading tags `<h1>`-`<h6>`
  - Maintains CSS classes, stylesheet links, and document-level formatting
  - Keeps chapter structure, TOC, metadata, cover images
  - **Trade-off**: Inline formatting (`<b>`, `<i>`, `<span>`) within paragraphs is removed to ensure clean text replacement without mangling

- **DOCX Structure Preservation**: Similar paragraph-level approach
  - Preserves paragraph properties and overall document structure
  - Maintains document metadata, images, and non-text content
  - Simplifies text content within each paragraph for reliable translation
  - **Trade-off**: Complex inline formatting may be simplified, but paragraph-level structure remains intact

#### 5. Settings Persistence
New configuration parameters in QSettings:
```cpp
llmEnabled          // Enable/disable AI improvement
llmProvider         // "Ollama", "LM Studio", "OpenAI", "Claude", "Gemini"
llmUrl              // Provider endpoint URL
llmModel            // Model identifier
openaiApiKey        // OpenAI authentication
claudeApiKey        // Claude authentication
geminiApiKey        // Gemini authentication
```

### Files Added to Original Codebase

**Core Document Processing (8 files):**
- [src/DocumentSplitter.h](src/DocumentSplitter.h) (52 lines)
- [src/DocumentSplitter.cpp](src/DocumentSplitter.cpp) (493 lines)
- [src/DocumentMerger.h](src/DocumentMerger.h) (48 lines)
- [src/DocumentMerger.cpp](src/DocumentMerger.cpp) (354 lines)
- [src/DocumentProcessor.h](src/DocumentProcessor.h) (31 lines)
- [src/DocumentProcessor.cpp](src/DocumentProcessor.cpp) (66 lines)
- [src/LLMInterface.h](src/LLMInterface.h) (68 lines)
- [src/LLMInterface.cpp](src/LLMInterface.cpp) (459 lines)

**GUI Integration (3 files):**
- [src/DocumentTranslationDialog.h](src/DocumentTranslationDialog.h) (70 lines)
- [src/DocumentTranslationDialog.cpp](src/DocumentTranslationDialog.cpp) (200 lines)
- [src/DocumentTranslationDialog.ui](src/DocumentTranslationDialog.ui) (120 lines)

**Modified Files:**
- [CMakeLists.txt](CMakeLists.txt): Added 11 new source files
- [src/cli/CLIParsing.h](src/cli/CLIParsing.h): Added `--ai-improve` flag
- [src/cli/CommandLineIface.h](src/cli/CommandLineIface.h): Document processing methods
- [src/cli/CommandLineIface.cpp](src/cli/CommandLineIface.cpp): Document translation logic
- [src/settings/Settings.h](src/settings/Settings.h): 7 new LLM settings
- [src/settings/Settings.cpp](src/settings/Settings.cpp): Settings initialization
- [src/mainwindow.ui](src/mainwindow.ui): File menu with document actions
- [src/mainwindow.h](src/mainwindow.h): Document dialog slots
- [src/mainwindow.cpp](src/mainwindow.cpp): File menu handlers
- [src/settings/TranslatorSettingsDialog.ui](src/settings/TranslatorSettingsDialog.ui): AI Improvement tab
- [src/settings/TranslatorSettingsDialog.h](src/settings/TranslatorSettingsDialog.h): LLM settings slots
- [src/settings/TranslatorSettingsDialog.cpp](src/settings/TranslatorSettingsDialog.cpp): LLM configuration logic

**Total Addition:** ~2,200 lines of new code

### Compatibility with Original

This fork maintains **100% backward compatibility** with the original translateLocally:
- All original CLI commands work unchanged
- Existing models and settings are preserved
- Original GUI functionality intact
- New features are purely additive (opt-in via `--ai-improve` flag or GUI actions)
- Same build system, dependencies, and distribution model

---

## Table of Contents

- [Quick Start](#quick-start)
- [Supported Formats](#supported-formats)
- [Document Translation](#document-translation)
- [AI-Powered Improvement](#ai-powered-improvement)
- [Configuration](#configuration)
- [Advanced Usage](#advanced-usage)
- [Troubleshooting](#troubleshooting)

## Quick Start

### Basic Document Translation

Translate a Word document from English to French:

```bash
translateLocally -m en-fr-tiny -i document.docx -o document_fr.docx
```

### With AI Improvement

Improve translation quality using a local AI model (requires Ollama or LM Studio):

```bash
translateLocally -m en-fr-tiny -i document.docx -o document_fr.docx --ai-improve
```

### GUI Document Translation

1. **File → Open Document** (Ctrl+O)
2. Select DOCX, EPUB, PDF, or TXT file
3. Choose output path
4. Click "Start Translation"
5. Watch real-time progress bars for translation and AI improvement

## Supported Formats

### TXT (Plain Text)
- **Splitting strategy**: By paragraphs
- **Structure preservation**: Maintains paragraph breaks and whitespace
- **Maximum segment**: 8 MB per segment
- **Best for**: Simple text files, articles, books

### DOCX (Microsoft Word)
- **Processing method**: ZIP archive extraction and XML parsing
- **Structure preservation**: Full formatting, styles, embedded images, metadata
- **Technical details**: Parses `word/document.xml` via libarchive
- **Best for**: Formatted documents, reports, letters

### EPUB (E-books)
- **Processing method**: ZIP archive extraction and XHTML parsing with paragraph detection
- **Structure preservation**: Document-level structure maintained (chapters, headings, paragraphs, CSS stylesheets, metadata, cover images)
- **Technical details**: Parses content XHTML files via libarchive, detects paragraph boundaries (`<p>`, `<h1>`-`<h6>`), preserves paragraph tags while replacing text content
- **Trade-off**: Inline formatting within paragraphs (`<b>`, `<i>`, `<em>`, `<strong>`) is removed to ensure correct translation without text mangling
- **Quality**: Clean, readable output with reliable paragraph structure - suitable for e-books where content matters more than inline formatting
- **Best for**: E-books, novels, articles, documentation where paragraph structure is more important than bold/italic formatting

### PDF (Portable Document Format)
- **Processing method**: PDF → DOCX conversion via LibreOffice, then DOCX workflow
- **Requirements**: LibreOffice must be installed with `soffice.exe` in PATH
- **Limitations**: Complex layouts may not convert perfectly
- **Best for**: Simple PDF documents, text-heavy PDFs

## Document Translation

### How It Works

1. **Document Split**: Large documents are automatically split into segments of max 8 MB
   - This bypasses the internal 10 MB processing limit
   - Segments maintain original structure identifiers for correct reconstruction

2. **Translation**: Each segment is translated using the selected Marian model
   - Progress is shown for each segment
   - Original formatting is preserved

3. **Document Reconstruction**: Translated segments are merged back
   - Original structure and metadata are maintained
   - Archive-based formats (DOCX, EPUB) preserve all non-text content

### Example: Translating Large Documents

For documents larger than 10 MB, automatic splitting ensures smooth processing:

```bash
# Translate a 50 MB book from Spanish to English
translateLocally -m es-en-base -i libro_grande.epub -o big_book.epub
```

The document will be automatically split into ~7 segments, translated individually, and reassembled.

## AI-Powered Improvement

### What Is AI Improvement?

AI improvement uses large language models (LLMs) to:
- Fix awkward machine translation phrasing
- Improve naturalness and fluency
- Correct context-dependent errors
- Maintain consistent terminology

### Supported AI Providers

#### Local AI (Recommended for Privacy)

**Ollama** (Free, runs locally)
1. Install Ollama: https://ollama.com/download
2. Pull a model: `ollama pull mistral`
3. Configure in translateLocally:
   - Provider: `Ollama`
   - URL: `http://localhost:11434`
   - Model: `mistral`

**LM Studio** (Free, runs locally)
1. Install LM Studio: https://lmstudio.ai/
2. Load a model and start the local server
3. Configure in translateLocally:
   - Provider: `LM Studio`
   - URL: `http://localhost:1234`
   - Model: Your loaded model name

#### Cloud AI (Requires API Keys)

**OpenAI** (GPT-3.5, GPT-4)
- Provider: `OpenAI`
- URL: `https://api.openai.com`
- Model: `gpt-4o-mini` (recommended) or `gpt-4o`
- Requires: OpenAI API key

**Claude** (Anthropic)
- Provider: `Claude`
- URL: `https://api.anthropic.com`
- Model: `claude-3-5-sonnet-20241022` (recommended)
- Requires: Claude API key

**Gemini** (Google)
- Provider: `Gemini`
- URL: `https://generativelanguage.googleapis.com`
- Model: `gemini-1.5-flash` or `gemini-1.5-pro`
- Requires: Gemini API key

### How AI Improvement Works

1. **Machine Translation**: Marian translates the text first
2. **Synchronized Chunking**: Translation is split into 2000-character chunks (~600-700 tokens) with source/translation alignment maintained to prevent text mismatches
3. **AI Refinement**: Each chunk is sent to the AI for improvement with real-time progress updates
4. **Sequential Processing**: Chunks are processed one at a time for local LLM stability
5. **Structure Preservation**: For EPUBs, HTML structure is maintained while only text content is improved

### Usage Example

```bash
# Translate with local Ollama
translateLocally -m en-de-base -i report.docx -o bericht.docx --ai-improve

# The workflow:
# 1. Splits report.docx if needed
# 2. Translates with Marian (en-de-base)
# 3. Refines with Ollama mistral
# 4. Reconstructs bericht.docx
```

## Configuration

### Settings Location

LLM settings are stored in:
- **Windows**: `HKEY_CURRENT_USER\Software\translateLocally\translateLocally`
- **Linux**: `~/.config/translateLocally/translateLocally.conf`
- **macOS**: `~/Library/Preferences/com.translateLocally.translateLocally.plist`

### Available Settings

| Setting | Description | Example |
|---------|-------------|---------|
| `llmEnabled` | Enable AI improvement | `true` or `false` |
| `llmProvider` | AI provider name | `Ollama`, `LM Studio`, `OpenAI`, `Claude`, `Gemini` |
| `llmUrl` | Provider endpoint | `http://localhost:11434` |
| `llmModel` | Model identifier | `mistral`, `gpt-4o-mini`, `claude-3-5-sonnet-20241022` |
| `openaiApiKey` | OpenAI API key | `sk-...` |
| `claudeApiKey` | Claude API key | `sk-ant-...` |
| `geminiApiKey` | Gemini API key | `AI...` |

### Manual Configuration Example

For Ollama with Mistral model, add to settings file:

```ini
[General]
llmEnabled=true
llmProvider=Ollama
llmUrl=http://localhost:11434
llmModel=mistral
```

### GUI Configuration

1. Open translateLocally
2. Go to Settings (Edit → Preferences)
3. Navigate to "AI Improvement" tab
4. Configure:
   - Enable AI improvement checkbox
   - Select provider from dropdown
   - Enter server URL (for local providers)
   - Select or enter model name
   - Enter API key (for cloud providers)
5. Click "Test Connection" to verify
6. Click "Apply" to save

## Advanced Usage

### Batch Processing Multiple Documents

Translate all DOCX files in a directory:

```bash
for file in *.docx; do
  translateLocally -m en-fr-tiny -i "$file" -o "${file%.docx}_fr.docx"
done
```

### Pivot Translation with AI

Translate Spanish → English → German with AI improvement at each step:

```bash
# Step 1: Spanish to English with AI
translateLocally -m es-en-base -i documento.txt -o document_en.txt --ai-improve

# Step 2: English to German with AI
translateLocally -m en-de-base -i document_en.txt -o dokument.txt --ai-improve
```

### Choosing the Right Model Size

**For speed:**
- Use `tiny` models: Fast but lower quality
- Example: `en-fr-tiny`

**For quality:**
- Use `base` models: Slower but better quality
- Example: `en-fr-base`

**For AI improvement:**
- Start with `tiny` Marian + AI improvement for best speed/quality balance
- The AI will fix most tiny model errors

### Testing AI Improvement

Compare translations with and without AI:

```bash
# Without AI
translateLocally -m en-fr-tiny -i test.txt -o test_fr_noai.txt

# With AI
translateLocally -m en-fr-tiny -i test.txt -o test_fr_ai.txt --ai-improve

# Compare the outputs
diff test_fr_noai.txt test_fr_ai.txt
```

## Important Notes on Formatting Preservation

### What Is Preserved
✅ **Document structure**: Chapters, sections, table of contents
✅ **Paragraph boundaries**: Headings (`<h1>`-`<h6>`), paragraphs (`<p>`) maintain proper structure
✅ **Document-level formatting**: Stylesheets, CSS classes, fonts, page layout
✅ **Non-text content**: Images, cover art, metadata, embedded files
✅ **Archive integrity**: DOCX and EPUB ZIP structure fully maintained

### What Is Not Preserved
❌ **Inline text formatting**: Bold, italic, underline, font changes within paragraphs
❌ **Spans and inline styles**: `<b>`, `<i>`, `<em>`, `<strong>`, `<span>` tags within text
❌ **Complex inline structures**: Nested formatting, hyperlinks within text (document structure links preserved)

### Why This Limitation Exists

This is a **deliberate technical choice** to ensure translation quality:

**Problem with inline formatting preservation:**
When attempting to preserve inline formatting (e.g., keeping `<b>bold words</b>` bold), we encountered severe issues:
- **Word-sticking**: Translated words appeared without spaces ("desmodules" instead of "des modules")
- **Misaligned formatting**: Wrong words received bold/italic (e.g., `<b>simple test</b>` became `<b>document de</b>`)
- **Duplicate/missing text**: When word counts differed between languages, text appeared twice or disappeared
- **Mangled structure**: Sentences split mid-word, tags broken (`<it` instead of `<i>`)

**Current solution:**
- Replace entire paragraph content as a unit
- Guarantees correct, readable translations without text corruption
- Maintains document readability and structure integrity
- Trade-off: Inline formatting within paragraphs is removed

**Best for:**
- E-books where content readability matters most
- Documents where paragraph structure is more important than bold/italic
- Translations where accuracy is critical

**Not ideal for:**
- Documents with critical formatting (e.g., legal documents with specific bolded clauses)
- Presentations with heavily formatted text
- Marketing materials where visual formatting is essential

For these cases, consider manual post-processing to re-apply formatting based on the original document.

## Troubleshooting

### Document Processing Issues

**Problem**: "Failed to open document"
- **Cause**: Unsupported format or corrupted file
- **Solution**: Verify file format, try opening in original application first

**Problem**: "Segment too large" error
- **Cause**: Single segment (e.g., huge table) exceeds 8 MB
- **Solution**: Manually split the document into smaller files

**Problem**: PDF processing fails
- **Cause**: LibreOffice not installed or not in PATH
- **Solution**: Install LibreOffice and ensure `soffice.exe` is accessible

**Problem**: DOCX/EPUB structure corrupted after translation
- **Cause**: ~~Complex document with unusual formatting~~ **FIXED in commit 99e292c**
- **Solution**: Latest version uses paragraph-by-paragraph approach that prevents text mangling, word-sticking, and duplicate text issues. Structure is preserved at paragraph level (headings, paragraphs maintain proper boundaries). Note: Inline formatting (`<b>`, `<i>`) within paragraphs is removed as a trade-off for correct text replacement.

### AI Improvement Issues

**Problem**: AI improvement not working
- **Cause**: LLM provider not running or incorrectly configured
- **Solution**:
  - For Ollama: Verify with `ollama list` and `ollama ps`
  - For LM Studio: Check local server is started
  - For cloud APIs: Verify API key is correct

**Problem**: "Connection refused" error
- **Cause**: Local LLM not running
- **Solution**: Start Ollama or LM Studio before running translateLocally

**Problem**: AI makes translation worse
- **Cause**: Wrong model or prompt configuration
- **Solution**:
  - Try a different model (e.g., `mistral` → `llama3`)
  - For cloud APIs, use recommended models (GPT-4o-mini, Claude Sonnet)

**Problem**: Slow AI processing
- **Cause**: Local LLM running on CPU without acceleration
- **Solution**:
  - Use smaller models (`mistral` instead of `llama3:70b`)
  - Consider cloud APIs for speed
  - Reduce concurrent processing in code

**Problem**: API rate limiting errors
- **Cause**: Cloud provider rate limits exceeded
- **Solution**:
  - Wait and retry
  - Use local LLM for unlimited processing
  - Upgrade API tier if needed

### General Troubleshooting

**Enable debug output:**
```bash
translateLocally -m en-fr-tiny -i test.txt --debug
```

This will show detailed information about:
- Document splitting process
- Segment sizes and count
- Translation progress
- AI provider requests/responses
- Error stack traces

**Check dependencies:**
```bash
# Verify LibreOffice installation
soffice --version

# Verify Ollama is running
curl http://localhost:11434/api/tags
```

## Performance Optimization

### Document Processing

- **TXT files**: Fastest, no archive overhead
- **DOCX/EPUB**: Moderate, requires ZIP extraction
- **PDF**: Slowest, requires LibreOffice conversion

**Recommendation**: Convert PDFs to DOCX manually before batch processing

### AI Improvement

- **Local LLMs**: Privacy-focused, unlimited usage, slower
- **Cloud APIs**: Fast, pay-per-use, requires internet

**Recommendation**: Use local LLM for sensitive documents, cloud for speed

### Model Selection

| Quality Level | Speed | Model Example | Use Case |
|---------------|-------|---------------|----------|
| Fast | ⚡⚡⚡ | `en-fr-tiny` + AI | Quick drafts, chat |
| Balanced | ⚡⚡ | `en-fr-base` no AI | Standard documents |
| High | ⚡ | `en-fr-base` + AI | Publication-quality |

## Examples Gallery

### Example 1: Academic Paper

```bash
# Translate research paper with AI improvement for academic quality
translateLocally -m en-es-base -i paper.docx -o articulo.docx --ai-improve
```

### Example 2: Novel Translation

```bash
# Translate e-book preserving chapter structure
translateLocally -m en-fr-base -i novel.epub -o roman.epub --ai-improve
```

### Example 3: Business Report

```bash
# Fast translation for internal document
translateLocally -m en-de-tiny -i report.docx -o bericht.docx
```

### Example 4: Multilingual Batch

```bash
# Translate to 3 languages with AI
for lang in fr de es; do
  translateLocally -m en-${lang}-tiny -i source.txt -o output_${lang}.txt --ai-improve
done
```

## FAQ

**Q: Can I use AI improvement without internet?**
A: Yes! Install Ollama or LM Studio for completely offline AI improvement.

**Q: How much does cloud AI cost?**
A: Varies by provider. GPT-4o-mini costs ~$0.15 per million input tokens. A 10,000-word document costs roughly $0.002.

**Q: Does AI improvement work with all language pairs?**
A: Yes, but quality depends on the AI model's training. English, French, German, Spanish, Italian, Portuguese, Chinese, and Japanese typically work best.

**Q: Can I disable AI improvement for specific documents?**
A: Yes, simply omit the `--ai-improve` flag or uncheck the option in the GUI.

**Q: What happens to images in DOCX/EPUB files?**
A: Images and other binary content are preserved unchanged in the output document.

**Q: Can I translate password-protected PDFs?**
A: No, remove password protection first.

**Q: Is my text sent to the internet when using local LLMs?**
A: No, local LLMs (Ollama, LM Studio) process everything on your machine.

**Q: Are these features compatible with the original translateLocally?**
A: Yes! This is a 100% backward-compatible fork. All original functionality works unchanged. New features are purely additive.

**Q: Can I contribute these features back to the original project?**
A: Contributions are welcome! Submit pull requests to the upstream repository at https://github.com/XapaJIaMnu/translateLocally

## Support and Contributing

For issues, feature requests, or contributions:
- **This Fork**: [Your GitHub repository URL]
- **Original Project**: https://github.com/XapaJIaMnu/translateLocally
- Report bugs with sample files (anonymized if needed)
- Include debug output when reporting issues

## License

Document processing and AI improvement features are part of translateLocally and follow the same license as the original project.

## Credits

- **Original translateLocally**: Developed by XapaJIaMnu and contributors
- **Bergamot Translator**: Mozilla's browser-based translation project
- **Marian NMT**: Fast neural machine translation framework
- **This Fork**: Document processing and AI improvement features
