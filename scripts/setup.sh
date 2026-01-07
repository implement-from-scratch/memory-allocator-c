#!/bin/bash

echo "Setting up development environment..."

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Installing dependencies for Linux..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        clang \
        clang-format \
        clang-tidy \
        cppcheck \
        valgrind \
        lcov \
        libcunit1-dev \
        libcheck-dev \
        bear
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Installing dependencies for macOS..."
    brew install cmake clang-format llvm cppcheck
fi

# Install Rust (if needed)
if ! command -v rustc &> /dev/null; then
    echo "Installing Rust..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
fi

# Install pre-commit hooks
if [ -f ".git/hooks/pre-commit" ]; then
    echo "Pre-commit hook already exists"
else
    echo "Installing pre-commit hook..."
    mkdir -p .git/hooks
    cp scripts/pre-commit .git/hooks/
    chmod +x .git/hooks/pre-commit
fi

echo "Setup complete!"
