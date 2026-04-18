# Contributing Guide

## How to Contribute

1. **Fork the repository** on GitHub
2. **Clone your fork**:
   ```bash
   git clone https://github.com/YOUR-USERNAME/CS2Vault.git
   cd CS2Vault
   ```
3. **Create a branch** for your changes:
   ```bash
   git checkout -b feature/my-new-feature
   # or
   git checkout -b fix/my-bug-fix
   ```
4. **Make your changes**
5. **Test your changes** build and run, verify the change works
6. **Commit your changes**:
   ```bash
   git add .
   git commit -m "Add feature X that does Y"
   ```
7. **Push to your fork**:
   ```bash
   git push origin feature/my-new-feature
   ```
8. **Open a Pull Request** on GitHub

## Pull Request Guidelines

- For **bug fixes**, describe the bug and how your fix addresses it
- For **new features**, describe the feature and why it's valuable
- For **large changes**, open an issue first to discuss the approach
- Reference any related issues with "Fixes #123" or "Closes #123"

## Development Setup

See [Setup Guide](setup.md) and [Development Guide](development.md) for build instructions.

## What to Contribute

### Welcome Contributions

- Bug fixes with clear reproduction steps
- UI improvements (styles, layouts, usability)
- Performance improvements
- Documentation improvements (README, code comments, these docs)
- Error handling improvements

### Needs Investigation

- Float fetching re-enablement (blocked by Valve API issues)
- macOS support (not currently tested)
- Better test coverage

### Not Currently Accepting

- Major architectural changes without prior discussion
- Changes that require Steam API keys or accounts
- Changes that break existing functionality

## Code Style

### C++

- Follow existing patterns in the codebase
- Use `Q_OBJECT` macro for any class with signals/slots
- Use modern `QObject::connect()` syntax
- Initialize member variables inline in headers

### Node.js

- Follow existing style in `steamcompanion/index.js`
- 4-space indentation
- Use `require()` for modules (CommonJS)
- Use `process.stderr.write()` for logging

## Reporting Issues

When reporting bugs, include:

1. **Platform** (Linux/Windows + version)
2. **CS2Vault version** (from about dialog or `src/main.cpp:30`)
3. **Steps to reproduce**
4. **Expected behavior**
5. **Actual behavior**
6. **Relevant log output** (run from terminal to capture stderr)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
