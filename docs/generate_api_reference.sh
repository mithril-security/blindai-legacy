#!/bin/sh
set -e  # Exit if any command fails

# Install pdoc3 for generating Python API documentation
pip install pdoc3

# Generate HTML documentation
pdoc --html --skip-errors --template-dir docs/pdoc_template -o docs/ client/blindai/audio.py client/blindai/client.py client/blindai/core.py client/blindai/utils.py client/blindai/testing.py --force

# Remove pdoc3 version reference from generated HTML files
sed -i '/<p>Generated by <a href="https:\/\/pdoc3.github.io\/pdoc" title="pdoc: Python API documentation generator"><cite>pdoc<\/cite> 0.10.0<\/a>.<\/p>/d' docs/blindai/*.html

sed -i '/<head>/a <meta name="description" content="Use the blindai.audio class for Whisper-powered audio-text conversion, ensure security with Trusted Execution Environments.">' docs/blindai/audio.html
sed -i '/<head>/a <meta name="description" content="Use the blindai.client class for secure AI server connections, model uploads, and inferences. Boost data security with Python client module.">' docs/blindai/client.html
sed -i '/<head>/a <meta name="description" content="Dive into the blindai.core class: Secure server connections, attestation, and privacy-focused features in the BlindAI core module.">' docs/blindai/core.html
sed -i '/<head>/a <meta name="description" content="Explore the BlindAI package: AI privacy, audio processing, secure connections, and more in a robust Python toolkit.">' docs/blindai/index.html
sed -i '/<head>/a <meta name="description" content="Master the blindai.testing class: Start mock servers, validate AI solutions, and improve performance with BlindAI testing tools.">' docs/blindai/testing.html
sed -i '/<head>/a <meta name="description" content="Use the blindai.utils class for utility functions like certificate conversion and Whisper Tiny 20 tokens. Streamline AI workflows.">' docs/blindai/utils.html
