# Release Protocol

The CI/release pattern used in this repo (`.github/workflows/ci.yml`), written up so it can be copied into other JUCE plugin projects (SampleForge, Juno, etc.).

## The pattern

One workflow file, two jobs:

1. **`test`** - runs on every push and every pull request, to every branch. Builds the unit-test target and runs it. No artifacts, no publishing - just a pass/fail signal on the commit.
2. **`release`** - runs only when a tag matching `v*` is pushed. Before building anything, it verifies the tagged commit is actually reachable from `main`. If it isn't, the job fails immediately and nothing gets published. Only after that check passes does it build VST3/AU/Standalone and attach them to a GitHub Release.

Both jobs live in the same file because `release` uses `needs: test` - a release can't publish unless the tests on that exact commit passed in the same workflow run. `needs:` only works across jobs in the same file, which is why this isn't split into `ci.yml` + `release.yml`.

## Why the main-ancestry check exists

Without it, tagging *any* branch with `v1.2.3` and pushing the tag triggers a public release build - including a throwaway test/POC branch someone tagged by habit or by mistake. The check:

```yaml
- name: Verify tag is on main
  run: |
    git fetch origin main
    if ! git merge-base --is-ancestor "${{ github.sha }}" origin/main; then
      echo "::error::Tag ${{ github.ref_name }} is not reachable from main - refusing to publish a release."
      exit 1
    fi
```

`git merge-base --is-ancestor A B` succeeds only if commit A is an ancestor of commit B. So this fails closed: a tag on a branch that was never merged to `main` can't publish, full stop, regardless of who tagged it or why.

This is new as of MidiDestruct - **SampleForge's `release.yml` doesn't have this guard yet**. Worth retrofitting there if you want the same protection.

## Full template

Copy this to `.github/workflows/ci.yml` in a new project and fill in the placeholders (marked `<...>`):

```yaml
name: CI

on:
  push:
  pull_request:

jobs:
  test:
    name: Build & test
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4

      - name: Cache JUCE (FetchContent)
        uses: actions/cache@v4
        with:
          path: build/_deps
          key: ${{ runner.os }}-<project-slug>-deps-${{ hashFiles('CMakeLists.txt') }}

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build unit tests
        run: cmake --build build --target <TestTargetName> -j 4

      - name: Run unit tests
        run: ./build/<TestTargetName>

  release:
    name: Build & publish release artifacts
    needs: test
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: macos-14
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Verify tag is on main
        run: |
          git fetch origin main
          if ! git merge-base --is-ancestor "${{ github.sha }}" origin/main; then
            echo "::error::Tag ${{ github.ref_name }} is not reachable from main - refusing to publish a release."
            exit 1
          fi

      - name: Cache JUCE (FetchContent)
        uses: actions/cache@v4
        with:
          path: build/_deps
          key: ${{ runner.os }}-<project-slug>-deps-${{ hashFiles('CMakeLists.txt') }}

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build plugin formats
        run: cmake --build build --target <PluginTarget>_Standalone <PluginTarget>_VST3 <PluginTarget>_AU -j 4

      - name: Package artifacts
        run: |
          cd build/<PluginTarget>_artefacts/Release
          ditto -c -k --sequesterRsrc VST3/<ProductName>.vst3 <ProductName>-macOS-VST3.zip
          ditto -c -k --sequesterRsrc AU/<ProductName>.component <ProductName>-macOS-AU.zip
          ditto -c -k --sequesterRsrc Standalone/<ProductName>.app <ProductName>-macOS-Standalone.zip

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            build/<PluginTarget>_artefacts/Release/<ProductName>-macOS-VST3.zip
            build/<PluginTarget>_artefacts/Release/<ProductName>-macOS-AU.zip
            build/<PluginTarget>_artefacts/Release/<ProductName>-macOS-Standalone.zip
```

| Placeholder | Where it comes from |
|---|---|
| `<project-slug>` | Any short, unique-per-project string (e.g. `mididestruct`, `sampleforge`) - just keeps the dependency cache from colliding across repos if you ever share a self-hosted runner. |
| `<TestTargetName>` | The CMake executable target for your unit tests (e.g. `MidiDestructTests`). |
| `<PluginTarget>` | The name passed to `juce_add_plugin(...)` in your `CMakeLists.txt`. JUCE derives target names like `<PluginTarget>_VST3` from it. |
| `<ProductName>` | The `PRODUCT_NAME` argument to `juce_add_plugin(...)` - the actual `.vst3`/`.component`/`.app` bundle name on disk. |

If a project needs a universal (x86_64 + arm64) binary, add a verification step like SampleForge's (`lipo -info ... | grep -q "x86_64 arm64"`) right after the build step, before packaging.

## Cutting a release

Always tag `main`, never a feature branch:

```bash
git checkout main
git pull
git tag v1.2.3
git push origin v1.2.3
```

If the tag isn't on `main`, the `release` job's ancestry check fails and nothing publishes - you'll see it fail in the Actions tab rather than silently skip.

## Checking on a run

```bash
gh run list --repo garyanewsome/<repo> --limit 5
```

or watch the **Actions** tab in the GitHub UI.
