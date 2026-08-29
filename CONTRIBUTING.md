# Contributing

Thank you for contributing to Quota Fabric.

## Building & testing

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Guidelines

- **C++20, /W4 /WX clean on MSVC** and `-Wall -Wextra -Wpedantic -Werror` on GCC/Clang. Do not forward MSVC warning flags to nvcc.
- **No test timeouts.** Tests run until they naturally pass, fail, crash, or a genuine hang is manually diagnosed. Fix hangs at the source.
- **Never hold a global quota lock across network, file, CUDA, or slow backend work.**
- **Strong typed IDs** everywhere; do not introduce loose strings/raw integers for identities.
- **Checked/saturating arithmetic**; no silent negative values; no NaN/Inf in quota calculations.
- **Canonical serialization**; never persist raw ABI structs.
- **Explainability** — every important decision should carry a structured reason plus a human-readable explanation.
- Add or extend the property/adversarial/concurrency/protocol/persistence tests for any behavioral change, and keep `invariants_ok()` true.

## Run the CUDA and multiprocess proofs

The CUDA proof requires a real NVIDIA GPU (the reference environment is an RTX 5090, sm_120). The multiprocess proof spawns real coordinator/agent processes over framed TCP and validates restart fencing.

## Commit conventions

Use clear, imperative commit messages. Sign-off trailers are not required.
