// Test that TLS is correctly considered supported or unsupported for the
// different targets.

// Linux supports TLS.
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fsyntax-only %s
// RUN: %clang_cc1 -triple i386-pc-linux-gnu -fsyntax-only %s

// Darwin supports TLS since macOS 10.4.
// RUN: not %clang_cc1 -triple x86_64-apple-darwin7 -fsyntax-only %s
// RUN: %clang_cc1 -triple x86_64-apple-macos10.4.0 -fsyntax-only %s

// RUN: %clang_cc1 -triple x86_64-pc-win32 -fsyntax-only %s
// RUN: %clang_cc1 -triple i386-pc-win32 -fsyntax-only %s

// OpenBSD supports TLS.
// RUN: %clang_cc1 -triple x86_64-pc-openbsd -fsyntax-only %s
// RUN: %clang_cc1 -triple i386-pc-openbsd -fsyntax-only %s

// Haiku does not support TLS.
// RUN: not %clang_cc1 -triple i586-pc-haiku -fsyntax-only %s

__thread int x;
