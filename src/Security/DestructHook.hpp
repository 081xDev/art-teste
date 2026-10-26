#pragma once

// ---------------------------------------------------------------------------
// Lightweight entry point for the exit-time artifact cleanup.
//
// The implementation in destruct.hpp pulls in winioctl.h, <filesystem> and
// roughly a thousand lines of registry and NTFS code. Including that from
// Includes.hpp dragged all of it into every translation unit in the project
// and roughly doubled a full rebuild, so only the two declarations that other
// files actually need live here.
//
// Call InstallDestructHandler() once, after the overlay window exists and
// before the render loop. It registers a console control handler and an atexit
// hook; the heavy work runs at exit and only once.
// ---------------------------------------------------------------------------

// Registers the cleanup to run on process exit. Idempotent.
void InstallDestructHandler( );

// Runs the cleanup immediately, at most once per process. Exposed for a menu
// "clean now" action and for tests.
void RunDestructOnce( );
