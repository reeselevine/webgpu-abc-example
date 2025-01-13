An example of how to use Dawn's generated C++ headers to write a WebGPU program.

Also using this to help with development of additional dawn features, namely internal timstamp queries.

For internal timestamp queries, some toggles must be toggled, allow_unsafe_apis, timestamp_quantization, and internal_compute_timestamp_queries. See example.cpp for how this is done. If running dawn as part of a chromium build, flags must be passed as described in https://dawn.googlesource.com/dawn/+/HEAD/docs/dawn/debugging.md.
