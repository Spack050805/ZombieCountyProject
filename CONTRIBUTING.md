# Contributing to Zombie County

Thanks for your interest! Zombie County started as a one-week personal challenge and is now
shared as a portfolio piece and learning resource. It isn't under active development, but
contributions are welcome.

## Ways to help

- **Report a bug**: open an [issue](../../issues) with steps to reproduce, what you expected,
  and what happened. Include your Unreal Engine version and OS.
- **Ask a question**: if something in the code is unclear, open an issue. If it confused you,
  it probably confuses others too.
- **Submit a fix or improvement**: pull requests for bug fixes, clearer comments, or small
  improvements are appreciated.

## Pull request guidelines

1. Fork the repo and create a branch from `main`.
2. Make sure the project **compiles with Unreal Engine 5.8** (Development Editor, Win64).
3. Follow the existing code style:
   - Gameplay logic in C++, presentation (VFX, sound, UI) in Blueprints via
     `BlueprintImplementableEvent` hooks.
   - Expose tunable values as `UPROPERTY` with sensible `ClampMin`/`ClampMax` and a comment
     explaining what the value means in gameplay terms.
4. Keep PRs focused: one fix or feature per PR.
5. Describe **what** you changed and **why** in the PR description.

## Assets and licensing

- **Do not commit third-party assets** (Fab, Marketplace, Sketchfab, etc.) unless their license
  explicitly allows redistribution in source form. This repository is meant to stay 100% free.
- By contributing, you agree that your contributions are licensed under the
  [MIT License](LICENSE) of this project.

## Code of conduct

Please be respectful. This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).
