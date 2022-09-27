#include <cmath>
#include <vector>

#include "./bazel-bin/vp9.pb.h"

uint64_t BindBitUInt(uint64_t number, uint32_t bits) {
  // Calculate max number for bit count
  uint64_t max_number = pow(2, bits) - 1;
  // Limit number to max bit size
  if (number > max_number) {
    return max_number;
  }
  return number;
}

void WriteBitUInt(std::vector<bool> &bit_buffer, uint64_t number, uint32_t bits) {
  // Writes integer bits in big endian format to the bit buffer
  number = __builtin_bswap64(number);
  // Write number to bit buffer
  for (int i = 0; i < bits; i++) {
    bool number_bit = (number << i) & 0b1;
    bit_buffer.push_back(number_bit);
  }
}

void WriteBitString(std::vector<bool> &bit_buffer, std::string string, uint32_t bits) {
  // Convert string to c bytes
  char* bytes = string.c_str();
  // Write string to bit buffer
  for (int i = 0; i < bits; i++) {
    int byte_index = floor(i / 8);
    bool string_bit = (bytes[byte_index] << i) & 0b1;
    bit_buffer.push_back(string_bit);
  }
}

void WriteByteUInt(std::vector<bool> &bit_buffer, uint64_t number, uint32_t bytes) {
  // Writes integer bytes in big endian format to the bit buffer
  WriteBitUInt(bit_buffer, number, bytes * 8);
}

void WriteVP9SignedInteger(std::vector<bool> &bit_buffer, VP9SignedInteger &number, uint32_t number_bits) {
  // Writes signed number bits with the signed bit at the end
  WriteBitString(bit_buffer, number.value(), bits);
  WriteBitUInt(bit_buffer, number.sign(), 1);
}

void WriteVP9FrameSyncCode(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write frame sync codes
  WriteByteUInt(bit_buffer, uncompressed_header.frame_sync_code(), 3);
}

void WriteVP9ColorConfig(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write the ten_or_twelve bit for certain profiles
  if (uncompressed_header.profile() >= 2) {
    WriteBitUInt(bit_buffer, uncompressed_header.color_config().ten_or_twelve_bit(), 1);
  } 
  // Write color space info
  WriteBitUInt(bit_buffer, uncompressed_header.color_config().color_space(), 3);
  // Write conditional color space info
  if (uncompressed_header.color_config().color_space() != UncompressedHeader_ColorConfig::CS_RGB) {
    WriteBitUInt(bit_buffer, uncompressed_header.color_config().color_range(), 1);
    if (uncompressed_header.profile() == 1 || uncompressed_header.profile() == 3) {
      WriteBitUInt(bit_buffer, uncompressed_header.color_config().subsampling_x(), 1);
      WriteBitUInt(bit_buffer, uncompressed_header.color_config().subsampling_y(), 1);
      WriteBitUInt(bit_buffer, uncompressed_header.color_config().reserved_zero(), 1);
    }
  }
  else {
    if (uncompressed_header.profile() == 1 || uncompressed_header.profile() == 3) {
      WriteBitUInt(bit_buffer, uncompressed_header.color_config().reserved_zero(), 1);
    }
  }
}

void WriteVP9FrameSize(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write frame size
  WriteBitUInt(bit_buffer, uncompressed_header.frame_size().frame_width_minus_1(), 16);
  WriteBitUInt(bit_buffer, uncompressed_header.frame_size().frame_height_minus_1(), 16);
}

void WriteVP9FrameSizeWithRefs(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write frame size with refs
  WriteBitUInt(bit_buffer, uncompressed_header.frame_size_found_ref(), 3); // TODO: Check that this is 3 and not 1 bits
  if (uncompressed_header.frame_size_found_ref() == 0) {
    WriteVP9FrameSize(bit_buffer, uncompressed_header);
  }
  WriteVP9RenderSize(bit_buffer, uncompressed_header);
}

void WriteVP9RenderSize(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write render and frame size difference bit
  WriteBitUInt(bit_buffer, uncompressed_header.render_size().render_and_frame_size_different(), 1);
  if (uncompressed_header.render_size().render_and_frame_size_different() == 1) {
    WriteBitUInt(bit_buffer, uncompressed_header.render_size().render_width_minus_1(), 16);
    WriteBitUInt(bit_buffer, uncompressed_header.render_size().render_height_minus_1(), 16);
  }
}

void WriteVP9ReadInterpolationFilter(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  WriteBitUInt(bit_buffer, uncompressed_header.read_interpolation_filter().is_filter_switchable(), 1);
  // Write interpolation filter if switchable
  if (uncompressed_header.read_interpolation_filter().is_filter_switchable() == 0) {
    WriteBitUInt(bit_buffer, uncompressed_header.read_interpolation_filter().raw_interpolation_filter(), 2);
  }
}

void WriteVP9ReadDeltaQ(std::vector<bool> &bit_buffer, ReadDeltaQ &read_delta_q) {
  WriteBitUInt(bit_buffer, read_delta_q.delta_coded(), 1);
  if (read_delta_q.delta_coded()) {
    WriteVP9SignedInteger(bit_buffer, read_delta_q.delta_q(), 4);
  }
}

void WriteVP9QuantizationParams(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  WriteBitUInt(bit_buffer, uncompressed_header.quantization_params().base_q_idx(), 8);
  WriteVP9ReadDeltaQ(bit_buffer, uncompressed_header.quantization_params().delta_q_y_dc());
  WriteVP9ReadDeltaQ(bit_buffer, uncompressed_header.quantization_params().delta_q_uv_dc());
  WriteVP9ReadDeltaQ(bit_buffer, uncompressed_header.quantization_params().delta_q_uv_ac());
}

void WriteVP9LoopFilterParams(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().loop_filter_level(), 6);
  WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().loop_filter_sharpness(), 3);
  WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().loop_filter_delta_enabled(), 1);
  if (uncompressed_header.loop_filter_params().loop_filter_delta_enabled() == 1) {
    WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().loop_filter_delta_update(), 1);
    if (uncompressed_header.loop_filter_params().loop_filter_delta_update() == 1) {
      // Write ref deltas
      WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().update_ref_delta(), 4);
      WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().loop_filter_ref_deltas(), 28); // TODO: Check that this is 14 bits and not 6
      // Write mode deltas
      WriteBitUInt(bit_buffer, uncompressed_header.loop_filter_params().update_mode_delta(), 4);
      WriteBitUInt(bit_buffe,r uncompressed_header.loop_filter_params().loop_filter_mode_deltas, 14); // TODO: Check that this is 14 bits and not 6
    }
  }
}

void WriteVP9SegmentationParams(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  WriteBitUInt(bit_buffer, uncompressed_header.segmentation_params().segmentation_enabled(), 1);
  if (uncompressed_header.segmentation_params().segmentation_enabled() == 1) {
    WriteBitUInt(bit_buffer, uncompressed_header.segmentation_params().segmentation_update_map(), 1);
    if (uncompressed_header.segmentation_params().segmentation_update_map() == 1) {
      WriteBitUInt(bit_buffer, uncompressed_header.segmentation_params().segmentation_temporal_update(), 1);
    }
    WriteBitUInt(bit_buffer, uncompressed_header.segmentation_params().segmentation_update_data(), 1);
    if (uncompressed_header.segmentation_params().segmentation_update_data() == 1) {
      WriteBitUInt(bit_buffer, uncompressed_header.segmentation_params().segmentation_abs_or_delta_update(), 1);
      WriteBitString(bit_buffer, uncompressed_header.segmentation_params().features(), (uncompressed_header.segmentation_params().features().size() * 8))
    }
  }
}

void WriteVP9UncompressedHeader(std::vector<bool> &bit_buffer, UncompressedHeader &uncompressed_header) {
  // Write uncompressed header frame marker
  WriteBitUInt(bit_buffer, 2, 2);
  // Write profile bits
  WriteBitUInt(bit_buffer, uncompressed_header.profile(), 2);
  // Write zero bit if needed
  if (uncompressed_header.profile() == 3) {
    WriteBitUInt(bit_buffer, uncompressed_header.reserved_zero(), 1);
  }
  // Write show existing frame bit
  WriteBitUInt(bit_buffer, uncompressed_header.show_existing_frame(), 1);
  // Write existing frame info if existing frame bit is set
  if (uncompressed_header.show_existing_frame() == 1) {
    WriteBitUInt(bit_buffer, uncompressed_header.frame_to_show_map_idx(), 3);
  }
  // Write frame type
  WriteBitUInt(bit_buffer, uncompressed_header.frame_type(), 1);
  // Write show frame bit
  WriteBitUInt(bit_buffer, uncompressed_header.show_frame(), 1);
  // Write error resilience mode bit
  WriteBitUInt(bit_buffer, uncompressed_header.error_resilient_mode(), 1);
  // Write certain info if frame is not a key frame
  if (uncompressed_header.frame_type() == UncompressedHeader_FrameType_KEY_FRAME) {
    WriteVP9FrameSyncCode(bit_buffer, frame);
    WriteVP9ColorConfig(bit_buffer, frame);
    WriteVP9FrameSize(bit_buffer, frame);
    WriteVP9RenderSize(bit_buffer, frame);
  }
  else {
    // Write intra_only byte if show_frame is not enabled
    if (uncompressed_header.show_frame() == 0) {
      WriteBitUInt(bit_buffer, uncompressed_header.intra_only(), 1);
    }
    // Write reset_frame_context if error_resilient_mode
    if (uncompressed_header.error_resilient_mode() == 0) {
      WriteBitUInt(bit_buffer, uncompressed_header.reset_frame_context(), 1);
    }
    // Write conditional intra frame data
    if (uncompressed_header.intra_only() == 1) {
      WriteVP9FrameSyncCode(bit_buffer, frame);
      // Write conditional profile data
      if (uncompressed_header.profile() > 0) {
        WriteVP9ColorConfig(bit_buffer, frame);
      }
      // Write intra frame info
      WriteBitUInt(bit_buffer, uncompressed_header.refresh_frame_flags(), 8);
      WriteVP9FrameSize(bit_buffer, uncompressed_header);
      WriteVP9RenderSize(bit_buffer, uncompressed_header);
    }
    else {
      // Write inter frame info
      WriteBitUInt(bit_buffer, uncompressed_header.refresh_frame_flags(), 8);
      WriteBitUInt(bit_buffer, uncompressed_header.ref_frame_idx(), 9);
      WriteBitUInt(bit_buffer, uncompressed_header.ref_frame_sign_bias(), 3);
      WriteVP9FrameSizeWithRefs(bit_buffer, uncompressed_header);
      WriteBitUInt(bit_buffer, uncompressed_header.allow_high_precision_mv(), 1);
      WriteVP9ReadInterpolationFilter(bit_buffer, uncompressed_header);
    }
  }
  // Write frame context data if error resilient mode is on
  if (uncompressed_header.error_resilient_mode() == 0) {
    WriteBitUInt(bit_buffer, uncompressed_header.refresh_frame_flags(), 1);
    WriteBitUInt(bit_buffer, uncompressed_header.frame_parallel_decoding_mode(), 1);
  }
  // Write frame_context_idx
  WriteBitUInt(bit_buffer, uncompressed_header.frame_context_idx(), 2);
  // Write final header data
  WriteVP9LoopFilterParams(bit_buffer, uncompressed_header);
  WriteVP9QuantizationParams(bit_buffer, uncompressed_header);
  WriteVP9SegmentationParams(bit_buffer, uncompressed_header);
  WriteBitString(bit_buffer, uncompressed_header.tile_info(), (uncompressed_header.tile_info().size() * 8));
  // Write header size
  WriteBitUInt(bit_buffer, ceil(bit_buffer.size() / 8), 16);
}

std::string ProtoToBinary(VP9Frame &frame) {
  // Create bitvector to store frame bits/bytes
  std::vector<bool> bit_buffer = std::vector<bool>();
  // Write VP9 uncompressed header
  WriteVP9UncompressedHeader(bit_buffer, frame.uncompressed_header());
  // Convert bitvector to string (may need to byte align)
  return std::string();
}

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  VP9Frame vp9_frame;
  ProtoToBinary(vp9_frame); 

  //google::protobuf::ShutdownProtobufLibrary();
  return 0;
}