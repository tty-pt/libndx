#ifndef XY_WATCH_H
#define XY_WATCH_H

/**
 * @file xy-watch.h
 * @brief Filesystem-watch auto-reload for xy modules.
 *
 * libxylem-watch monitors loaded module .so files with inotify and calls
 * xy_reload() automatically when a file is updated on disk.
 *
 * Typical usage:
 *
 *   xy_load("mods/foo");
 *   xy_watch_add("mods/foo");   // start watching
 *   xy_watch_start();           // spawn background thread (idempotent)
 *   ...
 *   xy_watch_stop();            // clean up before exit
 *
 * xy_watch_start() is idempotent — calling it multiple times is safe.
 * xy_watch_add() may be called before or after xy_watch_start().
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
int xy_watch_start(void);

/**
 * @brief Stop the background watcher thread and release all resources.
 *
 * Blocks until the thread has exited.  After this, xy_watch_start() may
 * be called again to restart watching.
 */
void xy_watch_stop(void);

/**
 * @brief Add a module path to the watch set.
 *
 * @param fname  The same path string passed to xy_load() (without .so suffix).
 * @return 0 on success, -1 on error (check errno).
 */
int xy_watch_add(const char *fname);

/**
 * @brief Remove a module path from the watch set.
 *
 * @param fname  The same path string passed to xy_load() (without .so suffix).
 */
void xy_watch_remove(const char *fname);

#endif /* XY_WATCH_H */
