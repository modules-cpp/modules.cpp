# Changelog

All notable changes to modules.cpp. Versions follow [semantic versioning](https://semver.org/).

## [v1.0.1] — 2026-09-28

### Fixed

- Bootstrap and build under GCC 14: `const auto` bindings of
  `options.values(...)` triggered an internal compiler error in
  `simplify_aggr_init_expr`; both are spelled as `std::vector<std::string>`
  now. GCC 14 bootstraps, builds, and configures.
- Bootstrap under GCC 15: the CI workflow builds with GCC 15 and installs
  `cppcheck` for the `check` tool.
- macOS: the build tool, configure tool, mdy parser, debug and run tests, and
  the shell tool handle macOS paths and line endings.

## [v1.0.0] — 2026-09-28

First release. A self-contained C++20 project that builds, tests and
documents itself from source, with no third-party build system, package
manager, test framework or documentation generator.

### Added

- **MDY**, a line-oriented manifest and document format, with the `mm.mdy`
  manifest tree that every tool walks: `kind: project`, `dir`, `module`,
  `app`, `test` and `doc`.
- **Bootstrap.** `bootstrap.sh` compiles enough by hand through the literal
  `c++` to produce `build1`, which then builds the rest, including the real
  build tool. A raw-command fallback runs if that fails.
- **Core modules** under `modules/mm`: `mm.mdy` (parser), `mm.build`
  (manifest walk, dependency graph, compiler and linker driver), `mm.app`
  (application boundary and command line), `mm.test` (test framework),
  `mm.shell` (shell and environment), `mm.model` (adapter onto `models/`).
- **Tools**: `build`, `test`, `check`, `model`, `shell`.
- **Applications**: `main` and `mdy`, the documentation renderer.
- **`models/`**, a second independent description of the project's own
  structure as abstract C++20 interfaces: `models.document`,
  `models.configuration`, `models.manifest`, `models.repository`,
  `models.tool`, `models.modules`, `models.workflow`, `models.artifacts`.
- **Documentation** written in MDY and rendered by the project's own tool:
  `docs/modules.mdy`, `docs/mdy.mdy`, `docs/modules-c++20.mdy`,
  `docs/modules-model.mdy`.
- **Workflow scripts**: `bootstrap.sh`, `build.sh`, `test.sh`,
  `document.sh`, `check.sh`, `model.sh`, `clean.sh`, each with a
  no-extension counterpart that runs through the project's own shell tool.
- Test framework with self-registering suites, expected failures reported as
  `xfail`, and `xpass` failing the run when a known defect starts passing.
- GCC and Clang backends, selected per build.
