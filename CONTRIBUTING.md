# Contributing Guide

## Code Review Process
- All changes must be reviewed by the code owners.
- Ensure all CI checks pass before requesting a review.
- Address all comments and resolve conversations before merging.

## Testing Requirements
- Unit tests are required for all new features and bug fixes.
- Integration tests should be added for significant changes.
- Ensure all tests pass locally using `make check` before pushing.

## Documentation Requirements
- Update `README.md` if there are changes to the build or usage instructions.
- Add assertions and comments for public APIs.
- Ensure comments are clear and strictly in simple English (no emojis).

## Commit Message Conventions
- Use present tense ("Add feature" not "Added feature").
- Use the imperative mood ("Move cursor to..." not "Moves cursor to...").
- Reference issues and pull requests liberally after the first line.

## Branch Naming Conventions
- `feature/` for new features (e.g., `feature/login-system`).
- `bugfix/` for bug fixes (e.g., `bugfix/memory-leak`).
- `hotfix/` for urgent fixes.
- `docs/` for documentation updates.

## Pull Request Process
1. Fork the repository.
2. Create a new branch.
3. Make your changes and commit them.
4. Push to your fork.
5. Open a Pull Request against the `main` branch.
