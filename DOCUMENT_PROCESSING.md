# Document Processing and AI-Powered Translation

translateLocally now supports translating entire documents (DOCX, EPUB, PDF, TXT) with optional AI-powered improvement of machine translations.

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
- **Processing method**: ZIP archive extraction and XHTML parsing
- **Structure preservation**: Chapter structure, formatting, metadata, cover images
- **Technical details**: Parses content XHTML files via libarchive
- **Best for**: E-books, digital publications

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
2. **Chunking**: Translation is split into 3000-character chunks
3. **AI Refinement**: Each chunk is sent to the AI for improvement
4. **Sequential Processing**: Chunks are processed one at a time for stability

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
- **Cause**: Complex document with unusual formatting
- **Solution**: Simplify formatting before translation, report issue with sample file

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
A: Yes, simply omit the `--ai-improve` flag.

**Q: What happens to images in DOCX/EPUB files?**
A: Images and other binary content are preserved unchanged in the output document.

**Q: Can I translate password-protected PDFs?**
A: No, remove password protection first.

**Q: Is my text sent to the internet when using local LLMs?**
A: No, local LLMs (Ollama, LM Studio) process everything on your machine.

## Support and Contributing

For issues, feature requests, or contributions:
- GitHub: https://github.com/XapaJIaMnu/translateLocally
- Report bugs with sample files (anonymized if needed)
- Include debug output when reporting issues

## License

Document processing and AI improvement features are part of translateLocally and follow the same license as the main project.
