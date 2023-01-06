# vp9-proto

A full protobuf specification for the VP9 video frame format. Includes C++ code that can convert binary VP9 frames to protobufs and vice versa. Specification and converters are only semi-compatible with inter-frames. 

## Purpose

Was originally created for fuzzing VP9 decoders with [libprotobuf-mutator](https://github.com/google/libprotobuf-mutator). May also be useful for testing, inspecting frames, networking, and experimenting with VP9

## Building

Make sure you have bazel installed and run build.sh

## Files and Directories

vp9.proto: Protobuf specification for the VP9 Frame Format

vp9_to_proto.cpp: C++ code for lifting binary VP9 frames to protobufs

proto_to_vp9.cpp: C++ code for converting protobufs to binary VP9 frames

frames: Test webm files, vp9 frames, and protobufs