export BAZEL_CXXOPTS="-std=c++14"
bazel build :vp9_to_proto --compilation_mode=dbg $1