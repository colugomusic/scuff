#pragma once

// Some strings are passed between processes by reading and
// writing a pre-allocated shared memory segment.
// An example of strings that are passed this way is the text
// representations of parameter values.
// If the number of in-flight strings hits the maximum, then
// the calling thread will block until space is available.
// These limits can be increased to avoid blocking.
typedef struct scuff_string_options_t {
	// Max length of strings.
	size_t max_string_length;
	// Max number of in-flight strings.
	size_t max_in_flight_strings;
} scuff_string_options;
