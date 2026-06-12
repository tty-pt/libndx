#ifndef NDX_WATCH_H
#define NDX_WATCH_H

/**
 * @file ndx-watch.h
 * @brief Filesystem-watch auto-reload for ndx modules.
 *
 * libndx-watch monitors loaded module .so files with inotify and calls
 * ndx_reload() automatically when a file is updated on disk.
 *
 * Typical usage:
 *
 *   ndx_load("mods/foo");
 *   ndx_watch_add("mods/foo");   // start watching
 *   ndx_watch_start();           // spawn background thread (idempotent)
 *   ...
 *   ndx_watch_stop();            // clean up before exit
 *
 * ndx_watch_start() is idempotent — calling it multiple times is safe.
 * ndx_watch_add() may be called before or after ndx_watch_start().
 *
 * Thread safety: all functions are safe to call from any thread.
 *
 * Platform: Linux only (uses inotify).
 */

/**
 * @brief Spawn the background watcher thread.
 *
 * Safe to call multiple times; subsequent calls are no-ops.
 *
 * @return 0 on success, -1 on error (check errno).
 */
int ndx_watch_start(void);

/**
 * @brief Stop the background watcher thread and release all resources.
 *
 * Blocks until the thread has exited.  After this, ndx_watch_start() may
 * be called again to restart watching.
 */
void ndx_watch_stop(void);

/**
 * @brief Add a module path to the watch set.
 *
 * @param fname  The same path string passed to ndx_load() (without .so suffix).
 * @return 0 on success, -1 on error (check errno).
 */
int ndx_watch_add(const char *fname);

/**
 * @brief Remove a module path from the watch set.
 *
 * @param fname  The same path string passed to ndx_load() (without .so suffix).
 */
void ndx_watch_remove(const char *fname);

#endif /* NDX_WATCH_H */
