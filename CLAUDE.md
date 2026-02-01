# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

translateLocally is a fast, privacy-focused machine translation application that runs entirely on your local machine. It's a Qt-based C++ application using the Bergamot fork of Marian for neural machine translation.

## Build Commands

### Linux
```bash
mkdir build && cd build
cmake ..
make -j5
./translateLocally
```

**Ubuntu 20.04 dependencies:**
```bash
sudo apt-get install -y libpcre++-dev qttools5-dev qtbase5-dev libqt5svg5-dev libarchive-dev libpcre2-dev
```

**Ubuntu 22.04 dependencies:**
```bash
sudo apt-get install -y libxkbcommon-x11-dev libpcre++-dev libvulkan-dev libgl1-mesa-dev qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools qt6-translations-l10n libqt6svg6-dev libarchive-dev libpcre2-dev
```

**Install MKL (required for good performance):**
```bash
wget -qO- "https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS-2019.PUB" | sudo apt-key add -
sudo sh -c "echo deb https://apt.repos.intel.com/mkl all main > /etc/apt/sources.list.d/intel-mkl.list"
sudo apt-get update -o Dir::Etc::sourcelist="/etc/apt/sources.list.d/intel-mkl.list"
sudo apt-get install -y --no-install-recommends intel-mkl-64bit-2020.0-088
```

### macOS
```bash
mkdir build && cd build
cmake ..
cmake --build . -j3 --target translateLocally-bin translateLocally.dmg
```

Uses Apple Accelerate instead of MKL. For distribution, use Qt's official distribution (not homebrew) and see [dist/macdmg.sh](dist/macdmg.sh) for signing/notarization.

### Windows
Use vcpkg to install dependencies and Visual Studio to build. CMake will handle MSVC-specific architecture flags automatically.

### ARM Build (Important)
On ARM platforms, after running cmake but before make, you MUST run:
```bash
./cmake/fix_ruy_build.sh <source_dir> <build_dir>
```
This fixes header name conflicts between RUY (used by Marian) and Qt's MOC.

## Architecture Overview

### Core Translation Pipeline
1. **MarianInterface** ([src/MarianInterface.cpp](src/MarianInterface.cpp)): Wraps the Bergamot translator, handles model loading and translation requests
2. **Translation** ([src/Translation.cpp](src/Translation.cpp)): Represents a translation job with input/output text and metadata
3. **ModelManager** ([src/inventory/ModelManager.cpp](src/inventory/ModelManager.cpp)): Manages model downloads, installation, removal, and repository connections

### Application Modes
The app can run in three modes (determined in [src/cli/CLIParsing.h](src/cli/CLIParsing.h)):
- **GUI mode**: Main Qt application ([src/mainwindow.cpp](src/mainwindow.cpp))
- **CLI mode**: Command-line translation ([src/cli/CommandLineIface.cpp](src/cli/CommandLineIface.cpp))
- **NativeMsg mode**: Browser extension integration using native messaging ([src/cli/NativeMsgIface.cpp](src/cli/NativeMsgIface.cpp))

### Key Subsystems
- **Settings** ([src/settings/Settings.cpp](src/settings/Settings.cpp)): QSettings wrapper for persistent configuration
- **Network** ([src/Network.cpp](src/Network.cpp)): HTTP client for model downloads with progress tracking
- **AlignmentHighlighter** ([src/AlignmentHighlighter.cpp](src/AlignmentHighlighter.cpp)): Shows word-level alignment between source and translation

### Document Processing Subsystems
- **DocumentSplitter** ([src/DocumentSplitter.cpp](src/DocumentSplitter.cpp)): Splits documents (TXT, DOCX, EPUB, PDF) into translatable segments with max 8MB size limit
- **DocumentMerger** ([src/DocumentMerger.cpp](src/DocumentMerger.cpp)): Reconstructs translated documents from segments while preserving structure and formatting
- **DocumentProcessor** ([src/DocumentProcessor.cpp](src/DocumentProcessor.cpp)): High-level orchestrator for document translation workflow
- **LLMInterface** ([src/LLMInterface.cpp](src/LLMInterface.cpp)): Unified interface for AI-powered translation improvement via Ollama, LM Studio, OpenAI, Claude, or Gemini

### Third-Party Dependencies
- **bergamot-translator** ([3rd_party/bergamot-translator](3rd_party/bergamot-translator)): Git submodule containing the Marian-based translation engine
- Qt 5 or Qt 6 (both supported)
- libarchive (for model archive extraction)
- Intel MKL (Linux/Windows) or Apple Accelerate (macOS) for BLAS

## CLI Usage Examples

**List installed models:**
```bash
./translateLocally -l
```

**Download a model:**
```bash
./translateLocally -d en-fr-tiny
```

**Translate a file:**
```bash
./translateLocally -m en-fr-tiny -i input.txt -o output.txt
```

**Translate from stdin/stdout:**
```bash
echo "Hello world" | ./translateLocally -m en-fr-tiny
```

**Pivot translation (chaining models):**
```bash
cat spanish.txt | ./translateLocally -m es-en-tiny | ./translateLocally -m en-de-tiny > german.txt
```

**Native messaging mode (browser extensions):**
```bash
./translateLocally -p
```

**Translate a document (DOCX, EPUB, PDF, TXT):**
```bash
./translateLocally -m en-fr-tiny -i document.docx -o document_fr.docx
```

**Translate a document with AI improvement:**
```bash
./translateLocally -m en-fr-tiny -i document.docx -o document_fr.docx --ai-improve
```

**Document processing features:**
- Automatically splits large documents into segments ≤8MB to handle files larger than the 10MB internal limit
- Preserves document structure, formatting, and metadata
- Supports TXT (paragraph-based splitting), DOCX (ZIP/XML parsing), EPUB (XHTML parsing), and PDF (via LibreOffice conversion)
- Optional AI-powered post-editing using local LLMs or cloud APIs

## Model Format

Models are tar.gz archives containing:
- `config.intgemm8bitalpha.yml`: Marian configuration (hardcoded name)
- `model_info.json`: Metadata (name, version, source/target languages)
- `model.npz` or `model.bin`: Model weights (quantized int8 models preferred)
- `vocab.*.spm`: SentencePiece vocabulary

For best performance, use quantized int8 models with precomputed multipliers. See README section "Importing custom models" for details.

## Important Development Notes

### OpenBLAS is Strongly Discouraged
OpenBLAS performance is significantly worse than MKL (up to 100x slower in some cases) due to inappropriate OpenMP parallelization for small matrices. Always use MKL on Linux/Windows or Apple Accelerate on macOS.

### Browser Extension Integration
To add support for a new browser extension, add the extension ID to [src/constants.h](src/constants.h) and rebuild. The app auto-registers native messaging manifests on first GUI launch.

### Build Architecture Flags
The `BUILD_ARCH` CMake variable controls CPU optimization (default: `native`). Common values:
- `native`: Optimize for build machine
- `x86-64-v2`: SSE4.2 baseline
- `x86-64-v3`: AVX2 (recommended for broad compatibility)
- `x86-64-v4`: AVX512

On Windows/MSVC, these are automatically mapped to appropriate `/arch:` flags.

### Model Repositories
Three repositories are supported (configured in Settings → Repositories):
- **Bergamot**: https://translatelocally.com/models.json (default)
- **OpusMT**: https://object.pouta.csc.fi/OPUS-MT-models/app/models.json
- **HPLT**: https://raw.githubusercontent.com/hplt-project/bitextor-mt-models/refs/heads/main/models.json

Repository format is documented at https://translatelocally.com/models.json.

### Document Processing and LLM Features - Build Notes

**CRITICAL:** The document processing and LLM features are completely independent of the marian/bergamot core:
- They only depend on libarchive (already required) and Qt (already required)
- They do NOT use sentencepiece or protobuf
- NEVER modify files in [3rd_party/bergamot-translator](3rd_party/bergamot-translator) to add these features

**Windows vcpkg considerations:**
- Do NOT install sentencepiece or protobuf via vcpkg - these conflict with embedded versions in marian
- If compilation fails with protobuf header conflicts, remove them: `vcpkg remove protobuf:x64-windows sentencepiece:x64-windows`
- The embedded versions in [3rd_party/bergamot-translator/3rd_party/marian-dev/src/3rd_party/sentencepiece](3rd_party/bergamot-translator/3rd_party/marian-dev/src/3rd_party/sentencepiece) should be used

## Document Processing and AI Improvement

translateLocally now supports full document translation with AI-powered improvement:

### Document Processing Architecture

**Workflow:**
1. **DocumentSplitter** segments documents into chunks ≤8MB
2. **MarianInterface** translates each segment
3. **LLMInterface** (optional) refines translations using AI
4. **DocumentMerger** reconstructs the final document

**Supported Formats:**
- **TXT**: Paragraph-based splitting with structure preservation
- **DOCX**: XML parsing via libarchive, full ZIP reconstruction
- **EPUB**: XHTML content extraction and rebuilding
- **PDF**: Conversion to DOCX via LibreOffice soffice.exe, then processed as DOCX

**Implementation Details:**
- 8MB segment size provides safety margin under 10MB Marian limit
- Segments preserve original identifiers for correct document reconstruction
- Archive-based formats (DOCX, EPUB) maintain all original metadata and non-translated files

### AI-Powered Translation Improvement

**LLMInterface** provides unified access to 5 AI providers:
- **Ollama** (http://localhost:11434) - Local inference
- **LM Studio** (http://localhost:1234) - Local inference with OpenAI-compatible API
- **OpenAI** (api.openai.com) - Cloud GPT models
- **Claude** (api.anthropic.com) - Cloud Anthropic models
- **Gemini** (generativelanguage.googleapis.com) - Cloud Google models

**Configuration:**
LLM settings are persisted in [Settings](src/settings/Settings.h:113-119):
- `llmEnabled`: Enable/disable AI improvement
- `llmProvider`: Selected provider name
- `llmUrl`: Provider endpoint URL
- `llmModel`: Model identifier
- `openaiApiKey`, `claudeApiKey`, `geminiApiKey`: API authentication

**Processing Strategy:**
- Chunks translations into 3000-character segments for coherence
- Sequential processing (maxConcurrent=1) for local LLM stability
- Optimized prompts to minimize verbosity and reasoning artifacts

### Dependencies

**Document processing requires:**
- libarchive (installed via vcpkg on Windows)
- Qt5/Qt6 XML and Network modules
- LibreOffice (optional, for PDF conversion)

**No additional dependencies beyond standard translateLocally build.**
