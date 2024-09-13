#pragma once

#include <clap/events.h>

typedef enum scuff_event_type_t {
	scuff_event_type_clap,
	scuff_event_type_vst,
} scuff_event_type;

typedef struct scuff_event_header_t {
	scuff_event_type type;
} scuff_event_header;

typedef struct scuff_event_clap_t {
	scuff_event_header header;
	const clap_event_header_t* event;
} scuff_event_clap;

typedef struct scuff_event_vst_t {
	scuff_event_header header;
	// Not implemented
} scuff_event_vst;
