#include "proto_to_vp9.cpp"

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  // Read protobuf
  std::ifstream ifs("./test_frame_protobuf", std::ios_base::in | std::ios_base::binary);

  // Convert protobuf
  VP9Frame vp9_frame;
  vp9_frame.ParseFromIstream(&ifs);
  ProtoToVP9 proto_to_vp9;
  proto_to_vp9.WriteVP9Frame((const VP9Frame*) &vp9_frame); 

  // Write binary to file
  std::ofstream ofs("./test_frame_out", std::ios_base::out | std::ios_base::binary);
  ofs << proto_to_vp9.GetBitBufferAsBytes();
  
  return 0;
}