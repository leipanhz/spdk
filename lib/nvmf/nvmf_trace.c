/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2026 Custom Trace Implementation
 */

#include "nvmf_trace.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/util.h"
#include <pthread.h>
#include <time.h>

/* Global trace buffer */
static struct nvmf_trace_buffer g_trace_buffer = {0};

/* TSC frequency for timestamp conversion */
static uint64_t g_tsc_rate = 0;

/* Event type strings */
static const char *g_event_type_strings[] = {
	[NVMF_TRACE_EVENT_REQUEST_RECEIVED] = "REQ_RECV",
	[NVMF_TRACE_EVENT_REQUEST_EXECUTING] = "REQ_EXEC",
	[NVMF_TRACE_EVENT_REQUEST_COMPLETED] = "REQ_COMP",
	[NVMF_TRACE_EVENT_BDEV_IO_SUBMIT] = "BIO_SUBM",
	[NVMF_TRACE_EVENT_BDEV_IO_COMPLETE] = "BIO_COMP",
	[NVMF_TRACE_EVENT_QPAIR_CONNECT] = "QP_CONN",
	[NVMF_TRACE_EVENT_QPAIR_DISCONNECT] = "QP_DISC",
	[NVMF_TRACE_EVENT_BUFFER_FULL] = "BUF_FULL",
};

const char *
nvmf_trace_event_type_str(uint32_t event_type)
{
	if (event_type >= NVMF_TRACE_EVENT_MAX) {
		return "UNKNOWN";
	}
	return g_event_type_strings[event_type];
}

int
nvmf_trace_init(const char *log_path)
{
	size_t entry_size = sizeof(struct nvmf_trace_entry);
	uint64_t num_entries;
	int rc;

	if (g_trace_buffer.entries != NULL) {
		SPDK_ERRLOG("Trace buffer already initialized\n");
		return -EEXIST;
	}

	/* Calculate number of entries that fit in 2GB */
	num_entries = NVMF_TRACE_BUFFER_SIZE / entry_size;

	SPDK_NOTICELOG("Initializing trace buffer: %lu entries (%lu bytes per entry, total %lu MB)\n",
		       num_entries, entry_size, (num_entries * entry_size) / (1024 * 1024));

	/* Allocate trace buffer using huge pages for better performance */
	g_trace_buffer.entries = spdk_zmalloc(num_entries * entry_size,
					      0x1000, NULL,
					      SPDK_ENV_LCORE_ID_ANY,
					      SPDK_MALLOC_DMA);
	if (g_trace_buffer.entries == NULL) {
		SPDK_ERRLOG("Failed to allocate trace buffer\n");
		return -ENOMEM;
	}

	g_trace_buffer.capacity = num_entries;
	g_trace_buffer.head = 0;
	g_trace_buffer.tail = 0;
	g_trace_buffer.wrapped = 0;
	g_trace_buffer.enabled = true;
	g_trace_buffer.entries_written = 0;
	g_trace_buffer.entries_lost = 0;

	rc = pthread_mutex_init(&g_trace_buffer.lock, NULL);
	if (rc != 0) {
		SPDK_ERRLOG("Failed to initialize mutex: %d\n", rc);
		spdk_free(g_trace_buffer.entries);
		g_trace_buffer.entries = NULL;
		return -rc;
	}

	/* Set log file path */
	if (log_path != NULL) {
		snprintf(g_trace_buffer.log_file_path, NVMF_TRACE_LOG_PATH_MAX,
			 "%s", log_path);
	} else {
		snprintf(g_trace_buffer.log_file_path, NVMF_TRACE_LOG_PATH_MAX,
			 "/tmp/spdk_nvmf_trace.log");
	}

	/* Open log file in append mode */
	g_trace_buffer.log_file = fopen(g_trace_buffer.log_file_path, "a");
	if (g_trace_buffer.log_file == NULL) {
		SPDK_ERRLOG("Failed to open log file %s: %s\n",
			    g_trace_buffer.log_file_path, strerror(errno));
		pthread_mutex_destroy(&g_trace_buffer.lock);
		spdk_free(g_trace_buffer.entries);
		g_trace_buffer.entries = NULL;
		return -errno;
	}

	/* Get TSC rate for timestamp conversion */
	g_tsc_rate = spdk_get_ticks_hz();

	SPDK_NOTICELOG("Trace system initialized. Log file: %s\n",
		       g_trace_buffer.log_file_path);

	/* Write header to log file */
	fprintf(g_trace_buffer.log_file,
		"# SPDK NVMf Trace Log - Started at %ld\n",
		time(NULL));
	fprintf(g_trace_buffer.log_file,
		"# Format: timestamp_us,event_type,request_id,qpair_id,nsid,opcode,status,thread_id\n");
	fflush(g_trace_buffer.log_file);

	return 0;
}

void
nvmf_trace_fini(void)
{
	if (g_trace_buffer.entries == NULL) {
		return;
	}

	/* Flush remaining entries */
	nvmf_trace_flush(true);

	/* Close log file */
	if (g_trace_buffer.log_file != NULL) {
		fprintf(g_trace_buffer.log_file,
			"# Trace ended at %ld. Total entries: %lu, Lost: %lu\n",
			time(NULL), g_trace_buffer.entries_written,
			g_trace_buffer.entries_lost);
		fclose(g_trace_buffer.log_file);
		g_trace_buffer.log_file = NULL;
	}

	pthread_mutex_destroy(&g_trace_buffer.lock);

	spdk_free(g_trace_buffer.entries);
	g_trace_buffer.entries = NULL;

	SPDK_NOTICELOG("Trace system shutdown. Total entries written: %lu, lost: %lu\n",
		       g_trace_buffer.entries_written, g_trace_buffer.entries_lost);
}

void
nvmf_trace_record(uint32_t event_type, uint64_t request_id,
		  uint32_t qpair_id, uint32_t nsid,
		  uint32_t opcode, uint32_t status)
{
	struct nvmf_trace_entry *entry;
	uint64_t head;

	if (!g_trace_buffer.enabled || g_trace_buffer.entries == NULL) {
		return;
	}

	/* Get current position atomically */
	head = __atomic_fetch_add(&g_trace_buffer.head, 1, __ATOMIC_RELAXED);

	/* Check if buffer wrapped */
	if (head >= g_trace_buffer.capacity) {
		/* Buffer full - trigger flush */
		__atomic_fetch_add(&g_trace_buffer.wrapped, 1, __ATOMIC_RELAXED);
		__atomic_fetch_add(&g_trace_buffer.entries_lost, 1, __ATOMIC_RELAXED);

		/* Try to flush asynchronously (non-blocking) */
		if (pthread_mutex_trylock(&g_trace_buffer.lock) == 0) {
			nvmf_trace_flush(false);
			pthread_mutex_unlock(&g_trace_buffer.lock);
		}
		return;
	}

	/* Write entry */
	entry = &g_trace_buffer.entries[head];
	entry->timestamp_tsc = spdk_get_ticks();
	entry->request_id = request_id;
	entry->event_type = event_type;
	entry->qpair_id = qpair_id;
	entry->nsid = nsid;
	entry->opcode = opcode;
	entry->status = status;
	entry->thread_id = (uint32_t)pthread_self();

	/* Check if buffer is getting full (>90%) */
	if (head > (g_trace_buffer.capacity * 9 / 10)) {
		/* Try to flush asynchronously */
		if (pthread_mutex_trylock(&g_trace_buffer.lock) == 0) {
			nvmf_trace_flush(false);
			pthread_mutex_unlock(&g_trace_buffer.lock);
		}
	}
}

int
nvmf_trace_flush(bool force)
{
	uint64_t head, tail, count, i;
	struct nvmf_trace_entry *entry;
	uint64_t timestamp_us;
	int entries_flushed = 0;

	if (g_trace_buffer.entries == NULL || g_trace_buffer.log_file == NULL) {
		return -EINVAL;
	}

	head = __atomic_load_n(&g_trace_buffer.head, __ATOMIC_ACQUIRE);
	tail = g_trace_buffer.tail;

	/* Calculate number of entries to flush */
	if (head >= g_trace_buffer.capacity) {
		/* Buffer wrapped - flush everything and reset */
		count = g_trace_buffer.capacity;
		tail = 0;
	} else if (force) {
		/* Flush all pending entries */
		count = head - tail;
	} else {
		/* Flush only if buffer is >50% full */
		count = head - tail;
		if (count < g_trace_buffer.capacity / 2) {
			return 0;
		}
	}

	if (count == 0) {
		return 0;
	}

	/* Write entries to log file - one line per entry */
	for (i = 0; i < count; i++) {
		entry = &g_trace_buffer.entries[tail + i];

		/* Convert TSC to microseconds */
		timestamp_us = (entry->timestamp_tsc * 1000000) / g_tsc_rate;

		/* Single line format: timestamp,event,req_id,qpair,nsid,opcode,status,thread */
		fprintf(g_trace_buffer.log_file,
			"%lu,%s,0x%lx,%u,%u,0x%x,0x%x,%u\n",
			timestamp_us,
			nvmf_trace_event_type_str(entry->event_type),
			entry->request_id,
			entry->qpair_id,
			entry->nsid,
			entry->opcode,
			entry->status,
			entry->thread_id);

		entries_flushed++;
	}

	fflush(g_trace_buffer.log_file);

	/* Update tail and stats */
	g_trace_buffer.tail = tail + count;
	g_trace_buffer.entries_written += entries_flushed;

	/* Reset buffer if it wrapped */
	if (head >= g_trace_buffer.capacity) {
		__atomic_store_n(&g_trace_buffer.head, 0, __ATOMIC_RELEASE);
		g_trace_buffer.tail = 0;
		SPDK_NOTICELOG("Trace buffer wrapped and flushed. Entries written: %lu, lost: %lu\n",
			       g_trace_buffer.entries_written, g_trace_buffer.entries_lost);
	}

	return entries_flushed;
}

void
nvmf_trace_set_enabled(bool enable)
{
	g_trace_buffer.enabled = enable;
	SPDK_NOTICELOG("Trace system %s\n", enable ? "enabled" : "disabled");
}

void
nvmf_trace_get_stats(uint64_t *entries_written, uint64_t *entries_lost,
		     uint32_t *buffer_usage)
{
	uint64_t head = __atomic_load_n(&g_trace_buffer.head, __ATOMIC_ACQUIRE);

	if (entries_written != NULL) {
		*entries_written = g_trace_buffer.entries_written;
	}

	if (entries_lost != NULL) {
		*entries_lost = g_trace_buffer.entries_lost;
	}

	if (buffer_usage != NULL) {
		if (g_trace_buffer.capacity > 0) {
			*buffer_usage = (uint32_t)((head * 100) / g_trace_buffer.capacity);
		} else {
			*buffer_usage = 0;
		}
	}
}

// Made with Bob
