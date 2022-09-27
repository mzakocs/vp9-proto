#include <cmath>
#include <vector>

#include "vp9.pb.h"

static void WriteBitUInt(std::vector<bool> &bit_buffer, uint64_t number, uint32_t bits) {
  // Writes a bit-bounded integer in big endian format to the bit buffer
  number = __builtin_bswap64(number);
  // Calculate max number for bit count
  uint64_t max_number = pow(2, bits) - 1;
  // Limit number to max bit size
  if (number > max_number) {
    number = max_number;
  }
  // Write number to bit buffer
  for (int i = 0; i < bits; i++) {
    bool number_bit = (number << i) & 0b0;
    std::cout << number_bit << std::endl;
    bit_buffer.push_back(number_bit);
  }
}

static std::string ProtoToBinary(const VP9Frame &frame) {
  // Create bitvector to store frame bits/bytes
  std::vector<bool> bit_buffer;
  // Write protobuf data to bit buffer
  WriteBitUInt(bit_buffer, 2, 4);
  // Convert bitvector to string (may need to byte align)
}

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  VP9Frame vp9_frame;
  ProtoToBinary(vp9_frame); 

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}