/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2026 Custom Trace Implementation
 */

#ifndef __NVMF_TRACE_H__
#define __NVMF_TRACE_H__

#include "spdk/stdinc.h"
#include "spdk/thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Trace buffer size: 2GB */
#define NVMF_TRACE_BUFFER_SIZE (2ULL * 1024 * 1024 * 1024)

/* Maximum log file path length */
#define NVMF_TRACE_LOG_PATH_MAX 256

/* Trace event types */
enum nvmf_trace_event_type {
	NVMF_TRACE_EVENT_REQUEST_RECEIVED = 0,
	NVMF_TRACE_EVENT_REQUEST_EXECUTING,
	NVMF_TRACE_EVENT_REQUEST_COMPLETED,
	NVMF_TRACE_EVENT_BDEV_IO_SUBMIT,
	NVMF_TRACE_EVENT_BDEV_IO_COMPLETE,
	NVMF_TRACE_EVENT_QPAIR_CONNECT,
	NVMF_TRACE_EVENT_QPAIR_DISCONNECT,
	NVMF_TRACE_EVENT_BUFFER_FULL,
	NVMF_TRACE_EVENT_MAX
};

/* Single trace entry - optimized for single-line output */
struct nvmf_trace_entry {
	uint64_t timestamp_tsc;      /* TSC timestamp */
	uint64_t request_id;         /* Request identifier (pointer or unique ID) */
	uint32_t event_type;         /* Event type from enum */
	uint32_t qpair_id;           /* Queue pair ID */
	uint32_t nsid;               /* Namespace ID */
	uint32_t opcode;             /* NVMe opcode */
	uint32_t status;             /* Completion status */
	uint32_t thread_id;          /* Thread ID */
} __attribute__((packed));

/* Trace buffer management structure */
struct nvmf_trace_buffer {
	struct nvmf_trace_entry *entries;
	uint64_t capacity;           /* Total number of entries */
	uint64_t head;               /* Write position (atomic) */
	uint64_t tail;               /* Read position for flushing */
	uint64_t wrapped;            /* Number of times buffer wrapped */
	bool enabled;
	pthread_mutex_t lock;        /* Lock for flush operations */
	char log_file_path[NVMF_TRACE_LOG_PATH_MAX];
	FILE *log_file;
	uint64_t entries_written;    /* Total entries written to log */
	uint64_t entries_lost;       /* Entries lost due to buffer overflow */
};

/**
 * Initialize the trace buffer system
 *
 * \param log_path Path to the log file where traces will be written when buffer is full
 * \return 0 on success, negative errno on failure
 */
int nvmf_trace_init(const char *log_path);

/**
 * Shutdown and cleanup the trace buffer system
 */
void nvmf_trace_fini(void);

/**
 * Record a trace entry
 *
 * \param event_type Type of event
 * \param request_id Request identifier
 * \param qpair_id Queue pair ID
 * \param nsid Namespace ID
 * \param opcode NVMe opcode
 * \param status Completion status
 */
void nvmf_trace_record(uint32_t event_type, uint64_t request_id,
		       uint32_t qpair_id, uint32_t nsid,
		       uint32_t opcode, uint32_t status);

/**
 * Flush trace buffer to log file
 *
 * \param force If true, flush all entries. If false, flush only when buffer is full
 * \return Number of entries flushed, negative errno on error
 */
int nvmf_trace_flush(bool force);

/**
 * Enable or disable tracing
 *
 * \param enable true to enable, false to disable
 */
void nvmf_trace_set_enabled(bool enable);

/**
 * Get trace statistics
 *
 * \param entries_written Output: total entries written to log
 * \param entries_lost Output: entries lost due to overflow
 * \param buffer_usage Output: current buffer usage percentage (0-100)
 */
void nvmf_trace_get_stats(uint64_t *entries_written, uint64_t *entries_lost,
			  uint32_t *buffer_usage);

/**
 * Convert event type to string
 *
 * \param event_type Event type
 * \return String representation of event type
 */
const char *nvmf_trace_event_type_str(uint32_t event_type);

#ifdef __cplusplus
}
#endif

#endif /* __NVMF_TRACE_H__ */

// Made with Bob
