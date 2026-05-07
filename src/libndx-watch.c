#include <sys/inotify.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <ttypt/ndx.h>
#include <ttypt/ndx-watch.h>

/* Maximum number of simultaneously watched module paths. */
#define NDX_WATCH_MAX 256

/* inotify events that signal the .so was updated:
 *   IN_CLOSE_WRITE  – file was written and closed in-place
 *   IN_MOVED_TO     – atomic rename-into-place (typical linker output)
 */
#define NDX_WATCH_MASK (IN_CLOSE_WRITE | IN_MOVED_TO)

/* ----------------------------------------------------------------------------
 * Internal watch-set entry
 * -------------------------------------------------------------------------- */

typedef struct {
	char *fname;  /* owned copy of the ndx_load path (no .so suffix) */
	int   wd;     /* inotify watch descriptor, or -1 if not active */
} ndx_watch_entry_t;

/* ----------------------------------------------------------------------------
 * Module-level state
 * -------------------------------------------------------------------------- */

static pthread_mutex_t  g_mu      = PTHREAD_MUTEX_INITIALIZER;
static pthread_t        g_thread;
static int              g_ifd     = -1;  /* inotify fd */
static int              g_running = 0;

static ndx_watch_entry_t g_entries[NDX_WATCH_MAX];
static int               g_entry_count = 0;

/* pipe used to wake the watcher thread on stop */
static int g_pipe_r = -1;
static int g_pipe_w = -1;

/* ----------------------------------------------------------------------------
 * Internal helpers (called with g_mu held unless noted)
 * -------------------------------------------------------------------------- */

/*
 * Build the .so path from fname (the caller-supplied basename/relative path).
 * Writes into buf (size buf_len).  Returns buf on success, NULL on truncation.
 *
 * ndx_load strips .so from the path before storing it, so we need to add it
 * back to get the actual file on disk.
 */
static char *
make_so_path(const char *fname, char *buf, size_t buf_len)
{
	int n = snprintf(buf, buf_len, "%s.so", fname);
	if (n < 0 || (size_t)n >= buf_len)
		return NULL;
	return buf;
}

/*
 * Find the entry index for fname, or -1 if not present.
 * Caller must hold g_mu.
 */
static int
entry_find(const char *fname)
{
	int i;
	for (i = 0; i < g_entry_count; i++) {
		if (strcmp(g_entries[i].fname, fname) == 0)
			return i;
	}
	return -1;
}

/*
 * Look up the entry index by inotify watch descriptor.
 * Returns -1 if not found.
 * Caller must hold g_mu.
 */
static int
entry_find_by_wd(int wd)
{
	int i;
	for (i = 0; i < g_entry_count; i++) {
		if (g_entries[i].wd == wd)
			return i;
	}
	return -1;
}

/*
 * (Re-)register the inotify watch for entry i.
 * Safe to call when the file does not yet exist — inotify_add_watch will
 * fail and we leave wd as -1; it will be retried on the next IN_MOVED_TO.
 * Caller must hold g_mu.
 */
static void
entry_rewatch(int i)
{
	char buf[4096];
	const char *path;
	int wd;

	if (g_ifd < 0)
		return;

	path = make_so_path(g_entries[i].fname, buf, sizeof(buf));
	if (!path)
		return;

	wd = inotify_add_watch(g_ifd, path, NDX_WATCH_MASK);
	g_entries[i].wd = wd; /* -1 on failure is intentional */
}

/* ----------------------------------------------------------------------------
 * Background watcher thread
 * -------------------------------------------------------------------------- */

/*
 * The watcher thread blocks on read(inotify_fd) and on arrival of an event
 * calls ndx_reload() for the affected module.
 *
 * We use a self-pipe to interrupt the blocking read() when ndx_watch_stop()
 * is called.
 *
 * Because ndx_reload() is not documented as thread-safe we serialise all
 * calls through g_mu.  The lock is dropped before blocking on read() so
 * that ndx_watch_add/remove remain responsive.
 */
static void *
watcher_thread(void *arg)
{
	/* inotify_event is variable-length; 4096 bytes fits many events. */
	char evbuf[4096]
		__attribute__((aligned(__alignof__(struct inotify_event))));
	fd_set rfds;
	int maxfd;

	(void)arg;

	for (;;) {
		ssize_t len;
		const struct inotify_event *ev;
		char *p;
		int ret;

		FD_ZERO(&rfds);
		FD_SET(g_ifd, &rfds);
		FD_SET(g_pipe_r, &rfds);
		maxfd = g_ifd > g_pipe_r ? g_ifd : g_pipe_r;

		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		/* Stop signal from ndx_watch_stop(). */
		if (FD_ISSET(g_pipe_r, &rfds))
			break;

		if (!FD_ISSET(g_ifd, &rfds))
			continue;

		len = read(g_ifd, evbuf, sizeof(evbuf));
		if (len <= 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (p = evbuf; p < evbuf + len; ) {
			char *fname_copy;
			int idx;

			ev = (const struct inotify_event *)p;
			p += sizeof(struct inotify_event) + ev->len;

			if (!(ev->mask & NDX_WATCH_MASK))
				continue;

			pthread_mutex_lock(&g_mu);
			idx = entry_find_by_wd(ev->wd);
			if (idx < 0) {
				pthread_mutex_unlock(&g_mu);
				continue;
			}

			fname_copy = strdup(g_entries[idx].fname);

			/*
			 * IN_MOVED_TO watch descriptors are one-shot on the
			 * source file — re-register so the next build is caught.
			 */
			if (ev->mask & IN_MOVED_TO)
				entry_rewatch(idx);

			pthread_mutex_unlock(&g_mu);

			if (!fname_copy)
				continue;

			/* ndx_reload is serialised through g_mu. */
			pthread_mutex_lock(&g_mu);
			ndx_reload(fname_copy);
			pthread_mutex_unlock(&g_mu);

			free(fname_copy);
		}
	}

	return NULL;
}

/* ----------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int
ndx_watch_start(void)
{
	int pipefd[2];
	int ret;

	pthread_mutex_lock(&g_mu);

	if (g_running) {
		pthread_mutex_unlock(&g_mu);
		return 0;
	}

	g_ifd = inotify_init1(IN_CLOEXEC);
	if (g_ifd < 0) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}

	if (pipe(pipefd) < 0) {
		close(g_ifd);
		g_ifd = -1;
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	g_pipe_r = pipefd[0];
	g_pipe_w = pipefd[1];

	/* Register inotify watches for any paths already in the set. */
	{
		int i;
		for (i = 0; i < g_entry_count; i++)
			entry_rewatch(i);
	}

	ret = pthread_create(&g_thread, NULL, watcher_thread, NULL);
	if (ret != 0) {
		close(g_ifd);  g_ifd = -1;
		close(g_pipe_r); g_pipe_r = -1;
		close(g_pipe_w); g_pipe_w = -1;
		pthread_mutex_unlock(&g_mu);
		errno = ret;
		return -1;
	}

	g_running = 1;
	pthread_mutex_unlock(&g_mu);
	return 0;
}

void
ndx_watch_stop(void)
{
	pthread_mutex_lock(&g_mu);

	if (!g_running) {
		pthread_mutex_unlock(&g_mu);
		return;
	}

	/* Wake the thread. */
	{
		char b = 1;
		(void)write(g_pipe_w, &b, 1);
	}

	g_running = 0;
	pthread_mutex_unlock(&g_mu);

	pthread_join(g_thread, NULL);

	pthread_mutex_lock(&g_mu);

	close(g_ifd);    g_ifd = -1;
	close(g_pipe_r); g_pipe_r = -1;
	close(g_pipe_w); g_pipe_w = -1;

	/* Clear all watch descriptors (fds are gone). */
	{
		int i;
		for (i = 0; i < g_entry_count; i++)
			g_entries[i].wd = -1;
	}

	pthread_mutex_unlock(&g_mu);
}

int
ndx_watch_add(const char *fname)
{
	int idx;
	char *copy;

	if (!fname)
		return -1;

	pthread_mutex_lock(&g_mu);

	/* Idempotent — ignore duplicates. */
	idx = entry_find(fname);
	if (idx >= 0) {
		pthread_mutex_unlock(&g_mu);
		return 0;
	}

	if (g_entry_count >= NDX_WATCH_MAX) {
		pthread_mutex_unlock(&g_mu);
		errno = ENOMEM;
		return -1;
	}

	copy = strdup(fname);
	if (!copy) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}

	idx = g_entry_count++;
	g_entries[idx].fname = copy;
	g_entries[idx].wd    = -1;

	if (g_ifd >= 0)
		entry_rewatch(idx);

	pthread_mutex_unlock(&g_mu);
	return 0;
}

void
ndx_watch_remove(const char *fname)
{
	int idx;
	int i;

	if (!fname)
		return;

	pthread_mutex_lock(&g_mu);

	idx = entry_find(fname);
	if (idx < 0) {
		pthread_mutex_unlock(&g_mu);
		return;
	}

	if (g_ifd >= 0 && g_entries[idx].wd >= 0)
		inotify_rm_watch(g_ifd, g_entries[idx].wd);

	free(g_entries[idx].fname);

	/* Compact the array. */
	for (i = idx; i < g_entry_count - 1; i++)
		g_entries[i] = g_entries[i + 1];

	g_entry_count--;

	pthread_mutex_unlock(&g_mu);
}
