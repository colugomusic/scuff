#pragma once

enum scuff_event_type {
	scuff_event_type_param_gesture_begin,
	scuff_event_type_param_gesture_end,
	scuff_event_type_param_value,
};

typedef struct scuff_event_header_t {
	scuff_event_type type;
} scuff_event_header;

typedef struct scuff_event_param_gesture_begin_t {
	scuff_event_header header;
} scuff_event_param_gesture_begin;

typedef struct scuff_event_param_gesture_end_t {
	scuff_event_header header;
} scuff_event_param_gesture_end;

typedef struct scuff_event_param_value_t {
	scuff_event_header header;
	double value;
} scuff_event_param_value;
