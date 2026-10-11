# Contributing to QALYVYRON

QALYVYRON is early-stage software. Small, reviewable changes are preferred over broad rewrites.

## Local setup

Install a C++17 compiler, CMake 3.21 or later, Ninja (recommended), and Qt 6.4 or later with the Quick, Quick Controls, Quick 3D, and Quick Dialogs modules.

Configure and build:

    cmake -S . -B build -G Ninja
    cmake --build build

Launch the application from the build output for your platform.

## Expectations

- Keep user-facing actions discoverable and mouse-friendly.
- Avoid adding keyboard-only workflows where a visible control is reasonable.
- Do not claim rendering quality or file-format support that has not been tested.
- Validate file inputs and handle failures with an actionable message.
- Keep scene data and UI state separate where practical.
- Add tests for format parsing, serialization, object transforms, and renderer behavior as those systems mature.
- Keep platform-specific behavior behind small, documented interfaces.
- Record known limitations in the README or release notes.

## Reporting problems

Include the operating system, Qt version, graphics backend/GPU when known, exact steps to reproduce, and relevant application output. Do not attach private scene files without removing sensitive content first.
