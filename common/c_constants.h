#pragma once

#define SCUFF_CHANNEL_COUNT     2        // Hard-coded for now just to make things easier.
#define SCUFF_EVENT_PORT_SIZE   128      // Max number of audio events per vector.
#define SCUFF_INVALID_INDEX     SIZE_MAX
#define SCUFF_SHM_BLOB_MAX      256
#define SCUFF_SHM_STRING_MAX    256
#define SCUFF_SHM_BLOB_BUF_SZ   32
#define SCUFF_SHM_STRING_BUF_SZ 32
#define SCUFF_VECTOR_SIZE       64       // Hard-coded for now just to make things easier.
#define SCUFF_GC_INTERVAL_MS    1000
#define SCUFF_POLL_SLEEP_MS     10
