#include <cmath>
#include <string>
#include <fstream>
#include <vector>

// TODO: Add Superframe Support
// TODO: Add full inter-frame reference support
// TODO: Add partition/tile tree fields
// TODO: Verify that more obscure fields/messages are being written properly

#include "vp9.pb.h"
#include "vp9_constants.h"

class ProtoToVP9 {
public:
  // Parser State Variables
  std::vector<bool> bit_buffer;
  bool Lossless = false;
  uint32_t tx_mode;
  uint32_t profile;
  bool FrameIsIntra = false;
  uint32_t interpolation_filter = 0;
  bool compoundReferenceAllowed = false;
  uint32_t reference_mode = 0;
  uint32_t header_size_in_bytes = 0;
  bool allow_high_precision_mv = 0;

  uint32_t FrameWidth = 0;
  uint32_t FrameHeight = 0;
  uint32_t MiCols = 0;
  uint32_t MiRows = 0;
  int32_t Sb64Cols = 0;
  int32_t Sb64Rows = 0;

  std::string BoolBuffer;
  uint32_t BoolLowValue = 0;
  uint32_t BoolRange = 0;
  int32_t BoolCount = 0;
  uint32_t BoolPos = 0;

  void WriteBitUInt(uint64_t number, uint32_t bits) {
    // Writes integer bits in big endian format to the end of bit buffer
    for (uint32_t i = bits; i --> 0;) {
      bool number_bit = (number >> i) & 0b1;
      // // std::cout << number_bit << std::endl;
      bit_buffer.push_back(number_bit);
    }
  }

  void WriteBitUIntPos(uint64_t number, uint32_t bits, uint32_t pos) {
    // Writes integer bits in big endian format to the bit buffer at a specific position
    for (uint32_t i = bits; i --> 0;) {
      bool number_bit = (number >> i) & 0b1;
      // // std::cout << number_bit << std::endl;
      bit_buffer[pos++] = number_bit;
    }
  }

  void WriteBitStringPos(std::string string, uint32_t bits, uint32_t pos) {
    // Write bytes to bit_buffer at specific position
    // Convert string to c bytes
    const char* bytes = string.c_str();
    // Write bytes to bit buffer
    uint32_t byte_count = ceil(bits / 8.0); 
    for (uint32_t byte_index = 0; byte_index < byte_count; byte_index++) {
      
      uint32_t bit_limit = (byte_index > 0) ? 8 : 8 - ((byte_count * 8) - bits);
      uint8_t current_byte = byte_index < string.size() ? bytes[byte_index] : 0;

      for (uint32_t bit_index = bit_limit; bit_index --> 0;) {
        bool bit = (current_byte >> bit_index) & 0b1;
        bit_buffer[pos++] = bit;
      }
    }
  }

  void WriteBitString(std::string string, uint32_t bits) {
    // Write bit string to end of bit buffer
    // Convert string to c bytes
    const char* bytes = string.c_str();
    // Write bytes to bit buffer
    uint32_t byte_count = ceil(bits / 8.0);
    for (uint32_t byte_index = 0; byte_index < byte_count; byte_index++) {
      
      uint32_t bit_limit = (byte_index > 0) ? 8 : 8 - ((byte_count * 8) - bits);
      uint8_t current_byte = byte_index < string.size() ? bytes[byte_index] : 0;

      for (uint32_t bit_index = bit_limit; bit_index --> 0;) {
        bool bit = (current_byte >> bit_index) & 0b1;
        bit_buffer.push_back(bit);
      }
    }
  }

  void WriteBool(int32_t bit, int32_t p) {

    // // std::cout << "Bool Bit: " << bit << " Prob: " << p << std::endl;

    unsigned int split;
    int count = BoolCount;
    unsigned int range = BoolRange;
    unsigned int lowvalue = BoolLowValue;
    int shift;

    split = 1 + (((range - 1) * p) >> 8);

    range = split;

    if (bit) {
      lowvalue += split;
      range = BoolRange - split;
    }

    shift = VP9Fuzzer::vpx_norm[range];

    range <<= shift;
    count += shift;

    if (count >= 0) {
      int offset = shift - count;

      if ((lowvalue << (offset - 1)) & 0x80000000) {
        int x = BoolPos - 1;

        while (x >= 0 && BoolBuffer[x] == '\xff') {
          BoolBuffer[x] = 0;
          x--;
        }

        BoolBuffer[x] += 1;
      }

      BoolBuffer[BoolPos++] = (lowvalue >> (24 - offset)) & 0xff;
      lowvalue <<= offset;
      shift = count;
      lowvalue &= 0xffffff;
      count -= 8;
    }

    lowvalue <<= shift;
    BoolCount = count;
    BoolLowValue = lowvalue;
    BoolRange = range;
  }

  void InitBool() {
    // Stolen from bitwriter.c in libvpx
    BoolLowValue = 0;
    BoolRange = 255;
    BoolCount = -24;
    BoolPos = 0;
    BoolBuffer = std::string(0x10000, 0);
    WriteBool(0, 128);
  }

  void ExitBool() {
    for (uint32_t i = 0; i < 32; i++) WriteBool(0, 128);

    if ((BoolBuffer[BoolPos - 1] & 0xe0) == 0xc0) {
      // std::cout << "Superframe Index Conflict" << std::endl;
      BoolBuffer[BoolPos++] = 0;
    }

    // std::cout << "End Bool Bytes: " << BoolPos << std::endl;
  }

  void WriteLiteral(uint64_t number, uint32_t bits) {
    int bit;
    for (bit = bits - 1; bit >= 0; bit--) WriteBool(1 & (number >> bit), 128);
  }

  void WriteVP9SignedInteger(const VP9SignedInteger *number, uint32_t number_bits) {
    // Writes signed number bits with the signed bit at the end
    WriteBitString(number->value(), number_bits);
    WriteBitUInt(number->sign(), 1);
  }

  void WriteVP9FrameSyncCode(const UncompressedHeader *uncompressed_header) {
    // Write frame sync codes
    WriteBitUInt(uncompressed_header->frame_sync_code(), 24);
  }

  void WriteVP9ColorConfig(const UncompressedHeader *uncompressed_header) {
    // Write the ten_or_twelve bit for certain profiles
    if (profile >= 2) {
      WriteBitUInt(uncompressed_header->color_config().ten_or_twelve_bit(), 1);
    } 
    // Write color space info
    WriteBitUInt(uncompressed_header->color_config().color_space(), 3);
    // Write conditional color space info
    if (uncompressed_header->color_config().color_space() != UncompressedHeader_ColorConfig::CS_RGB) {
      WriteBitUInt(uncompressed_header->color_config().color_range(), 1);
      if (profile == 1 || profile == 3) {
        WriteBitUInt(uncompressed_header->color_config().subsampling_x(), 1);
        WriteBitUInt(uncompressed_header->color_config().subsampling_y(), 1);
        WriteBitUInt(uncompressed_header->color_config().reserved_zero(), 1);
      }
    }
    else {
      if (profile == 1 || profile == 3) {
        WriteBitUInt(uncompressed_header->color_config().reserved_zero(), 1);
      }
    }
  }

  void ComputeImageSize() {
    MiCols = (FrameWidth + 7) >> 3;
    MiRows = (FrameHeight + 7) >> 3;
    Sb64Cols = (MiCols + 7) >> 3;
    Sb64Rows = (MiRows + 7) >> 3;
    return;
  }

  void WriteVP9FrameSize(const UncompressedHeader *uncompressed_header) {
    // Write frame size
    // TODO: Maybe mutate this a bit more granularly
    WriteBitUInt(uncompressed_header->frame_size().frame_width_minus_1(), 16);
    WriteBitUInt(uncompressed_header->frame_size().frame_height_minus_1(), 16);

    FrameWidth = uncompressed_header->frame_size().frame_width_minus_1() + 1;
    FrameHeight = uncompressed_header->frame_size().frame_height_minus_1() + 1;

    ComputeImageSize();
  }

  void WriteVP9RenderSize(const UncompressedHeader *uncompressed_header) {
    // Write render and frame size difference bit
    // TODO: Maybe mutate this a bit more granularly
    WriteBitUInt(uncompressed_header->render_size().render_and_frame_size_different(), 1);
    if (uncompressed_header->render_size().render_and_frame_size_different() == 1) {
      WriteBitUInt(uncompressed_header->render_size().render_width_minus_1(), 16);
      WriteBitUInt(uncompressed_header->render_size().render_height_minus_1(), 16);
    }
  }

  void WriteVP9FrameSizeWithRefs(const UncompressedHeader *uncompressed_header) {
    // Write frame size with refs
    WriteBitUInt(uncompressed_header->frame_size_found_ref(), 3); // TODO: Check that this is 3 and not 1 bits
    if (uncompressed_header->frame_size_found_ref() == 0) {
      WriteVP9FrameSize(uncompressed_header);
    }
    WriteVP9RenderSize(uncompressed_header);
  }

  void WriteVP9ReadInterpolationFilter(const UncompressedHeader *uncompressed_header) {
    WriteBitUInt(uncompressed_header->read_interpolation_filter().is_filter_switchable(), 1);
    // Write interpolation filter if switchable
    if (uncompressed_header->read_interpolation_filter().is_filter_switchable() == 1) {
      interpolation_filter = UncompressedHeader_InterpolationFilter_SWITCHABLE;
    }
    else {
      interpolation_filter = uncompressed_header->read_interpolation_filter().raw_interpolation_filter();
      WriteBitUInt(interpolation_filter, 2);
    }
  }

  void WriteVP9ReadDeltaQ(const UncompressedHeader_QuantizationParams_ReadDeltaQ read_delta_q) {
    WriteBitUInt(read_delta_q.delta_coded(), 1);
    if (read_delta_q.delta_coded() == 1) {
      WriteVP9SignedInteger(&read_delta_q.delta_q(), 4);
    }
  }

  void WriteVP9QuantizationParams(const UncompressedHeader *uncompressed_header) {
    // Write quantization params
    WriteBitUInt(uncompressed_header->quantization_params().base_q_idx(), 8);
    WriteVP9ReadDeltaQ(uncompressed_header->quantization_params().delta_q_y_dc());
    WriteVP9ReadDeltaQ(uncompressed_header->quantization_params().delta_q_uv_dc());
    WriteVP9ReadDeltaQ(uncompressed_header->quantization_params().delta_q_uv_ac());
    // Set lossless parser state
    Lossless = (uncompressed_header->quantization_params().base_q_idx() == 0
                && uncompressed_header->quantization_params().delta_q_y_dc().delta_q().value().empty()
                && uncompressed_header->quantization_params().delta_q_uv_dc().delta_q().value().empty()
                && uncompressed_header->quantization_params().delta_q_uv_ac().delta_q().value().empty());
  }

  void WriteVP9RefDelta(VP9BitField update_ref_delta, const VP9SignedInteger* loop_filter_ref_delta) {
    WriteBitUInt(update_ref_delta, 1);
    // std::cout << "Update Ref Delta: " << update_ref_delta << std::endl;
    if (update_ref_delta == 1) {
      WriteVP9SignedInteger(loop_filter_ref_delta, 6);
    } 
  }

  void WriteVP9ModeDelta(VP9BitField update_mode_delta, const VP9SignedInteger* loop_filter_mode_delta) {
    WriteBitUInt(update_mode_delta, 1);
    if (update_mode_delta) {
      WriteVP9SignedInteger(loop_filter_mode_delta, 6);
    }
  }

  void WriteVP9LoopFilterParams(const UncompressedHeader *uncompressed_header) {
    WriteBitUInt(uncompressed_header->loop_filter_params().loop_filter_level(), 6);
    WriteBitUInt(uncompressed_header->loop_filter_params().loop_filter_sharpness(), 3);
    WriteBitUInt(uncompressed_header->loop_filter_params().loop_filter_delta_enabled(), 1);
    if (uncompressed_header->loop_filter_params().loop_filter_delta_enabled() == 1) {
      WriteBitUInt(uncompressed_header->loop_filter_params().loop_filter_delta_update(), 1);
      if (uncompressed_header->loop_filter_params().loop_filter_delta_update() == 1) {
        // Write ref deltas
        for (int i = 0; i < 4; i++) {
          if (i < uncompressed_header->loop_filter_params().ref_delta().size()) {
            auto ref_delta = uncompressed_header->loop_filter_params().ref_delta().at(i);
            WriteVP9RefDelta(ref_delta.update_ref_delta(), &ref_delta.loop_filter_ref_deltas());
          }
          else {
            const VP9SignedInteger temp = VP9SignedInteger();
            WriteVP9RefDelta((VP9BitField) 0, &temp);
          }
        }
        // Write mode deltas
        for (int i = 0; i < 2; i++) {
          if (i < uncompressed_header->loop_filter_params().mode_delta().size()) {
            auto mode_delta = uncompressed_header->loop_filter_params().mode_delta().at(i);
            WriteVP9ModeDelta(mode_delta.update_mode_delta(), &mode_delta.loop_filter_mode_deltas());
          }
          else {
            const VP9SignedInteger temp = VP9SignedInteger();
            WriteVP9ModeDelta((VP9BitField) 0, &temp);
          }
        }
      }
    }
  }

  void WriteVP9SegmentationParamsFeature(const UncompressedHeader_SegmentationParams_Feature* feature, uint32_t seg_lvl_max) {
    WriteBitUInt(feature->feature_enabled(), 1);

    if (feature->feature_enabled() == 1) {
      WriteBitString(feature->feature_value(), VP9Fuzzer::segmentation_feature_bits[seg_lvl_max]);
      if (VP9Fuzzer::segmentation_feature_signed[seg_lvl_max] == 1) {
        WriteBitUInt(feature->feature_sign(), 1);
      }
    }
  }

  void WriteVP9SegmentationParamsReadProb(const VP9BitField prob_coded, uint32_t prob) {
    WriteBitUInt(prob_coded, 1);
    if (prob_coded) {
      WriteBitUInt(prob, 8);
    }
  }

  void WriteVP9SegmentationParams(const UncompressedHeader *uncompressed_header) {
    WriteBitUInt(uncompressed_header->segmentation_params().segmentation_enabled(), 1);
    if (uncompressed_header->segmentation_params().segmentation_enabled() == 1) {
      WriteBitUInt(uncompressed_header->segmentation_params().segmentation_update_map(), 1);
      // Write Segmentation Probabilities
      if (uncompressed_header->segmentation_params().segmentation_update_map() == 1) {
        int32_t probs_read = 0;
        for (int i = 0; i < 7; i++) {
          if (probs_read < uncompressed_header->segmentation_params().prob().size()) {
            auto prob = uncompressed_header->segmentation_params().prob().at(probs_read++);
            WriteVP9SegmentationParamsReadProb(prob.prob_coded(), prob.prob());
          }
          else {
            WriteVP9SegmentationParamsReadProb((VP9BitField) 0, 0);
          }
        }
        WriteBitUInt(uncompressed_header->segmentation_params().segmentation_temporal_update(), 1);
        if (uncompressed_header->segmentation_params().segmentation_temporal_update()) {
          for (int i = 0; i < 3; i++) {
            if (probs_read < uncompressed_header->segmentation_params().prob().size()) {
              auto prob = uncompressed_header->segmentation_params().prob().at(probs_read++);
              WriteVP9SegmentationParamsReadProb(prob.prob_coded(), prob.prob());
            }
            else {
              WriteVP9SegmentationParamsReadProb((VP9BitField) 0, 0);
            }
          }
        }
      }
      // Write Segmentation Features
      WriteBitUInt(uncompressed_header->segmentation_params().segmentation_update_data(), 1);
      if (uncompressed_header->segmentation_params().segmentation_update_data() == 1) {
        WriteBitUInt(uncompressed_header->segmentation_params().segmentation_abs_or_delta_update(), 1);
        
        // Write features
        int32_t feature_index = 0;
        for (int i = 0; i < 8; i++) {
          for (int j = 0; j < SEG_LVL_MAX; j++) {
            if (feature_index < uncompressed_header->segmentation_params().features().size()) {
              WriteVP9SegmentationParamsFeature(&uncompressed_header->segmentation_params().features().at(feature_index++), j);
            }
            else {
              auto feature = UncompressedHeader_SegmentationParams_Feature();
              WriteVP9SegmentationParamsFeature(&feature, j);
            }
          }
        }
      }
    }
  }

  uint32_t CalcMinLog2TileCols() {
    int32_t minLog2 = 0;
    while ((MAX_TILE_WIDTH_B64 << minLog2) < Sb64Cols) {
      ++minLog2;
    }
    return minLog2;
  }

  uint32_t CalcMaxLog2TileCols() {
    int32_t maxLog2 = 1;
    while ((Sb64Cols >> maxLog2) >= MIN_TILE_WIDTH_B64 ) {
      ++maxLog2;
    }
    return maxLog2 - 1;
  }

  void WriteVP9TileInfo(const UncompressedHeader* uncompressed_header) {
    uint32_t minLog2TileCols = CalcMinLog2TileCols();  
    uint32_t maxLog2TileCols = CalcMaxLog2TileCols();
    uint32_t tile_cols_log2 = minLog2TileCols;

    int32_t inc_count = 0;

    while (tile_cols_log2 < maxLog2TileCols) {
      // Grab an increment_tile_cols_log2 element if we have one
      uint32_t increment_tile_cols_log2 = 0;
      if (inc_count < uncompressed_header->tile_info().increment_tile_cols_log2().size()) {
        increment_tile_cols_log2 = uncompressed_header->tile_info().increment_tile_cols_log2().at(inc_count++);
      }
      // Write it to the packet
      WriteBitUInt(increment_tile_cols_log2, 1);
      if (increment_tile_cols_log2) {
        ++tile_cols_log2;
      }
      else break;
    }
    // Then write tile_rows_log2
    WriteBitUInt(uncompressed_header->tile_info().tile_rows_log2(), 1);
    if (uncompressed_header->tile_info().tile_rows_log2() == 1) {
      WriteBitUInt(uncompressed_header->tile_info().increment_tile_rows_log2(), 1); 
    }
  }

  void WriteVP9UncompressedHeader(const UncompressedHeader *uncompressed_header) {
    // Write uncompressed header frame marker
    WriteBitUInt(2, 2);
    // Write profile bits
    profile = (uncompressed_header->profile_high_bit() << 1) + uncompressed_header->profile_low_bit();
    WriteBitUInt(uncompressed_header->profile_low_bit(), 1);
    WriteBitUInt(uncompressed_header->profile_high_bit(), 1);
    // Write zero bit if needed
    if (profile == 3) {
      WriteBitUInt(uncompressed_header->reserved_zero(), 1);
    }
    // Write show existing frame bit
    WriteBitUInt(uncompressed_header->show_existing_frame(), 1);
    // Write existing frame info if existing frame bit is set
    if (uncompressed_header->show_existing_frame() == 1) {
      WriteBitUInt(uncompressed_header->frame_to_show_map_idx(), 3);
      header_size_in_bytes = 0;
      return;
    }
    // Write frame type
    // std::cout << "Frame Type: " << uncompressed_header->frame_type() << std::endl;
    WriteBitUInt(uncompressed_header->frame_type(), 1);
    // Write show frame bit
    // std::cout << "Show Frame: " << uncompressed_header->show_frame() << std::endl;
    WriteBitUInt(uncompressed_header->show_frame(), 1);
    // Write error resilience mode bit
    WriteBitUInt(uncompressed_header->error_resilient_mode(), 1);
    // Write certain info if frame is not a key frame
    if (uncompressed_header->frame_type() == UncompressedHeader_FrameType_KEY_FRAME) {
      FrameIsIntra = true;
      WriteVP9FrameSyncCode(uncompressed_header);
      WriteVP9ColorConfig(uncompressed_header);
      WriteVP9FrameSize(uncompressed_header);
      WriteVP9RenderSize(uncompressed_header);
    }
    else {
      // Write intra_only byte if show_frame is not enabled
      uint32_t intra_only = 0;
      if (uncompressed_header->show_frame() == 0) {
        intra_only = uncompressed_header->intra_only();
        WriteBitUInt(intra_only, 1);
      }
      FrameIsIntra = intra_only;
      // Write reset_frame_context if error_resilient_mode
      if (uncompressed_header->error_resilient_mode() == 0) {
        WriteBitUInt(uncompressed_header->reset_frame_context(), 2);
      }
      // Write conditional intra frame data
      if (intra_only == 1) {
        WriteVP9FrameSyncCode(uncompressed_header);
        // Write conditional profile data
        if (profile > 0) {
          WriteVP9ColorConfig(uncompressed_header);
        }
        // Write intra frame info
        WriteBitUInt(uncompressed_header->refresh_frame_flags(), 8);
        WriteVP9FrameSize(uncompressed_header);
        WriteVP9RenderSize(uncompressed_header);
      }
      else {
        WriteBitUInt(uncompressed_header->refresh_frame_flags(), 8);
        for (int32_t i = 0; i < 3; i++) {
          // Write frame index
          if (i < uncompressed_header->ref_frame_idx().size()) {
            WriteBitUInt(uncompressed_header->ref_frame_idx().at(i), 3);
          }
          else {
            WriteBitUInt(0, 3);
          }
          // Write frame sign bias
          if (i < uncompressed_header->ref_frame_sign_bias().size()) {
            WriteBitUInt(uncompressed_header->ref_frame_sign_bias().at(i), 1);
            // Check if compound references are allowed
            if (i != 0 && uncompressed_header->ref_frame_sign_bias().at(i) != uncompressed_header->ref_frame_sign_bias().at(0)) {
              compoundReferenceAllowed = true;
            }
          }
          else {
            WriteBitUInt(0, 1);
            // Check if compound references are allowed
            if (i != 0 && uncompressed_header->ref_frame_sign_bias().size() > 0 && uncompressed_header->ref_frame_sign_bias().at(0) != 0) {
              compoundReferenceAllowed = true;
            }
          }
        }
        WriteVP9FrameSizeWithRefs(uncompressed_header);
        allow_high_precision_mv = uncompressed_header->allow_high_precision_mv();
        WriteBitUInt(allow_high_precision_mv, 1);
        WriteVP9ReadInterpolationFilter(uncompressed_header);
      }
    }
    // Write frame context data if error resilient mode is on
    if (uncompressed_header->error_resilient_mode() == 0) {
      WriteBitUInt(uncompressed_header->refresh_frame_flags(), 1);
      WriteBitUInt(uncompressed_header->frame_parallel_decoding_mode(), 1);
    }
    // Write frame_context_idx
    WriteBitUInt(uncompressed_header->frame_context_idx(), 2);
    // Write final header data
    WriteVP9LoopFilterParams(uncompressed_header);
    WriteVP9QuantizationParams(uncompressed_header);
    WriteVP9SegmentationParams(uncompressed_header);
    WriteVP9TileInfo(uncompressed_header);
    // Write header size
    // TODO: Maybe try something more interesting here
    // header_size_in_bytes = uncompressed_header->header_size_in_bytes();
    // WriteBitUInt(header_size_in_bytes, 16);
  }

  void WriteVP9ReadTxMode(const CompressedHeader *compressed_header) {
    if (Lossless == true) {
      tx_mode = CompressedHeader_TxMode_ONLY_4X4;
    }
    else {
      tx_mode = compressed_header->read_tx_mode().tx_mode();
      WriteLiteral(tx_mode, 2);
      if (tx_mode == CompressedHeader_TxMode_ALLOW_32X32) {
        WriteLiteral(compressed_header->read_tx_mode().tx_mode_select(), 1);
        tx_mode += (uint32_t) compressed_header->read_tx_mode().tx_mode_select();
      }
    }
  }

  void WriteVP9Uniform(uint32_t v) {
    // for v field in decode_term_subexp
    const int l = 8;
    const int m = (1 << l) - 191;
    if (v < m) {
      WriteLiteral(v, l - 1);
    } else {
      WriteLiteral(m + ((v - m) >> 1), l - 1);
      WriteLiteral((v - m) & 1, 1);
    }
  }

  void WriteVP9DecodeTermSubexp(const CompressedHeader_DecodeTermSubexp *decode_term_subexp) {
    WriteLiteral(decode_term_subexp->bit_1(), 1);
    if (decode_term_subexp->bit_1() == 0) {
      WriteLiteral(decode_term_subexp->sub_exp_val(), 4);
      return;
    }
    WriteLiteral(decode_term_subexp->bit_2(), 1);
    if (decode_term_subexp->bit_2() == 0) {
      WriteLiteral(decode_term_subexp->sub_exp_val_minus_16(), 4);
      return;
    }
    WriteLiteral(decode_term_subexp->bit_3(), 1);
    if (decode_term_subexp->bit_3() == 0) {
      WriteLiteral(decode_term_subexp->sub_exp_val_minus_32(), 5);
      return;
    }
    WriteVP9Uniform(decode_term_subexp->v());
    if (decode_term_subexp->v() < 65) {
      return;
    }
    WriteLiteral(decode_term_subexp->bit_4(), 1);
  }

  void WriteVP9DiffUpdateProb(CompressedHeader_DiffUpdateProb *diff_update_prob) {
    // Write Update Prob bit
    WriteBool(diff_update_prob->update_prob(), 252);
    // Write term subexp if we want to update probs
    if (diff_update_prob->update_prob() == 1) {
      WriteVP9DecodeTermSubexp(&diff_update_prob->decode_term_subexp());
    }
  }

  void WriteVP9DiffUpdateProbs(const google::protobuf::RepeatedPtrField<CompressedHeader_DiffUpdateProb> *diff_update_probs, int32_t max_writes) {
    // Writes (0 < n < max_writes) count of DiffUpdateProb objects to the VP9 frame
    for (int32_t i = 0; i < max_writes; i++) {
      // Check if we have a diff_update_prob object to write
      if (i < diff_update_probs->size()) {
        // Get diff update probability object at index
        CompressedHeader_DiffUpdateProb diff_update_prob = diff_update_probs->at(i);
        // Write the object
        WriteVP9DiffUpdateProb(&diff_update_prob);
        // // std::cout << "Diff Bool Bytes: " << BoolPos << std::endl;

      }
      // If we have no more diff_update_prob objects to write, just write empty objects
      else {
        WriteLiteral(0, 1);
      }
    }
  }

  void WriteVP9TxModeProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->tx_mode_probs().diff_update_prob(), 12);
  }

  void WriteVP9ReadCoefProbs(const CompressedHeader *compressed_header) {
    // Loop write count for tx_size max size
    for (int32_t txSz = VP9Fuzzer::TX_4X4; txSz <= VP9Fuzzer::tx_mode_to_biggest_tx_size[tx_mode]; txSz++) {
      // Check if we have a ReadCoefsProbsLoop object for this iteration
      if (txSz < compressed_header->read_coef_probs().read_coef_probs().size()) {
        // If so, grab the object
        auto loop_obj = compressed_header->read_coef_probs().read_coef_probs().at(txSz);
        // Write the update_probs indicator bit
        // std::cout << "Update Read Coef Probs: " << loop_obj.update_probs() << std::endl;
        WriteLiteral(loop_obj.update_probs(), 1);
        if (loop_obj.update_probs() == 1) {
          // Write diff_update_probs objects
          WriteVP9DiffUpdateProbs(&loop_obj.diff_update_prob(), 396);
        }
      }
      // Otherwise just write 1 0b0 bit to indicate we don't want to update probs
      else {
        WriteLiteral(0, 1);
      }
    }
  }

  void WriteVP9ReadSkipProb(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_skip_prob().diff_update_prob(), 3);
  }

  void WriteVP9ReadInterModeProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_inter_mode_probs().diff_update_prob(), 21);
  }

  void WriteVP9ReadInterpFilterProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_interp_filter_probs().diff_update_prob(), 14);
  }

  void WriteVP9ReadIsInterProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_is_inter_probs().diff_update_prob(), 4);
  }

  void WriteVP9FrameReferenceMode(const CompressedHeader *compressed_header) {
    if (compoundReferenceAllowed == 1) {
      WriteLiteral(compressed_header->frame_reference_mode().non_single_reference(), 1);
      // Set reference mode state variable
      if (compressed_header->frame_reference_mode().non_single_reference() == 0) {
        reference_mode = VP9Fuzzer::SINGLE_REFERENCE;
      }
      else {
        WriteLiteral(compressed_header->frame_reference_mode().reference_select(), 1);
        if (compressed_header->frame_reference_mode().reference_select() == 0) {
          reference_mode = VP9Fuzzer::COMPOUND_REFERENCE;
        }
        else {
          reference_mode = VP9Fuzzer::REFERENCE_MODE_SELECT;
        }
      }
    }
    else {
      reference_mode = VP9Fuzzer::SINGLE_REFERENCE;
    }
  }

  void WriteVP9FrameReferenceModeProbs(const CompressedHeader *compressed_header) {
    // TODO: Make this write sequential diff_update_prob objects instead of the same ones
    if (reference_mode == VP9Fuzzer::REFERENCE_MODE_SELECT) {
      WriteVP9DiffUpdateProbs(&compressed_header->frame_reference_mode_probs().diff_update_prob(), 5);
    }
    if (reference_mode != VP9Fuzzer::COMPOUND_REFERENCE) {
      WriteVP9DiffUpdateProbs(&compressed_header->frame_reference_mode_probs().diff_update_prob(), 5);
    }
    if (reference_mode != VP9Fuzzer::SINGLE_REFERENCE) {
      WriteVP9DiffUpdateProbs(&compressed_header->frame_reference_mode_probs().diff_update_prob(), 5);
    }
  }

  void WriteVP9ReadYModeProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_y_mode_probs().diff_update_prob(), 36);
  }

  void WriteVP9ReadPartitionProbs(const CompressedHeader *compressed_header) {
    WriteVP9DiffUpdateProbs(&compressed_header->read_partition_probs().diff_update_prob(), 48);
  }

  void WriteVP9MvProbsLoop(VP9BitField update_mv_prob, uint32_t mv_prob) {
    WriteBool(update_mv_prob, 252);
    if (update_mv_prob == 1) {
      WriteLiteral(mv_prob, 7);
    }
  }

  void WriteVP9MvProbs(const CompressedHeader *compressed_header) {
    // First 3 loop stacks
    std::size_t mv_probs_size = compressed_header->mv_probs().mv_probs().size();
    for (uint32_t i = 0; i < 45; i++) {
      // Write mv prob loop objects if we have one
      if (i < mv_probs_size) {
        CompressedHeader_MvProbs_MvProbsLoop mv_probs_loop = compressed_header->mv_probs().mv_probs().at(i);
        WriteVP9MvProbsLoop(mv_probs_loop.update_mv_prob(), mv_probs_loop.mv_prob());
      }
      // Otherwise write an empty one
      else {
        WriteVP9MvProbsLoop((VP9BitField) 0, 0);
      }
    }
    // Last conditional loop
    if (allow_high_precision_mv) {
      for (uint32_t i = 45; i < (45 + 4); i++) { 
        // Write mv prob loop objects if we have one
        if (i < mv_probs_size) {
          CompressedHeader_MvProbs_MvProbsLoop mv_probs_loop = compressed_header->mv_probs().mv_probs().at(i);
          WriteVP9MvProbsLoop(mv_probs_loop.update_mv_prob(), mv_probs_loop.mv_prob());
        }
        // Otherwise write an empty one
        else {
          WriteVP9MvProbsLoop((VP9BitField) 0, 0);
        }
      }
    }
  }

  void WriteVP9CompressedHeader(const CompressedHeader *compressed_header) {
    // Write read_tx_mode
    WriteVP9ReadTxMode(compressed_header);
    // std::cout << "Starting Bits: " << bit_buffer.size() << std::endl;
    // std::cout << "Bool Bytes: " << BoolPos << std::endl;
    // Write tx mode probability info if select tx mode is enabled
    // std::cout << "TXMODE: " << tx_mode << std::endl;
    if (tx_mode == CompressedHeader_TxMode_TX_MODE_SELECT) {
      WriteVP9TxModeProbs(compressed_header);
    }
    // std::cout << "Bool Bytes: " << BoolPos << std::endl;
    // Write read probabilities
    WriteVP9ReadCoefProbs(compressed_header);
    WriteVP9ReadSkipProb(compressed_header);

    // std::cout << "Bool Bytes: " << BoolPos << std::endl;

    // std::cout << "FrameIsIntra: "<< FrameIsIntra << std::endl;

    if (FrameIsIntra == false) {
      // std::cout << "FRAME IS INTRA!" << std::endl;
      WriteVP9ReadInterModeProbs(compressed_header);
      if (interpolation_filter == UncompressedHeader_InterpolationFilter_SWITCHABLE) {
        WriteVP9ReadInterpFilterProbs(compressed_header);
      }
      WriteVP9ReadIsInterProbs(compressed_header);
      WriteVP9FrameReferenceMode(compressed_header);
      WriteVP9FrameReferenceModeProbs(compressed_header);
      WriteVP9ReadYModeProbs(compressed_header);
      WriteVP9ReadPartitionProbs(compressed_header);
      WriteVP9MvProbs(compressed_header);
    }
  }

  void WriteVP9Tile(const Tile* tile, bool last_tile) {
    // WriteBitUInt(tile->tile_size(), 32);
    if (!last_tile) {
      WriteBitUInt(tile->partition().size(), 32);
    }
    WriteBitString(tile->partition(), (tile->partition().size() * 8));
  }

  std::string GetBitBufferAsBytes() {
    // Create std::string to return
    size_t bitbuffer_size = bit_buffer.size();
    std::string return_buffer;
    // Loop through all the bits and set the bytes on the return buffer
    uint64_t bytes_to_write = ceil(bitbuffer_size / 8.0);
    uint64_t bit_buffer_index = 0;
    for (uint64_t i = 0; i < bytes_to_write; i++) {
      // Setup temporary byte
      uint8_t temp_byte = 0;
      for (uint64_t j = 8; j --> 0;) {
        // Get bit
        bool bit;
        if (bit_buffer_index < bitbuffer_size) {
          bit = bit_buffer.at(bit_buffer_index) & 0b1;
        }
        else bit = 0;
        // // std::cout << bit;
        // Write bit to the temporary byte at the necessary position
        temp_byte |= (bit << j);
        // Increate bit index
        ++bit_buffer_index;
      }
      // // std::cout << std::hex << temp_byte << std::dec << std::endl;
      // Put temporary byte into the return buffer
      return_buffer.push_back(temp_byte);
    }
    // Return string
    // // std::cout << return_buffer;
    return return_buffer;
  }

  void WriteVP9TrailingBits() {
    while (bit_buffer.size() & 7) {
      WriteBitUInt(0, 1);
    }
  }

  void WriteVP9Frame(const VP9Frame *frame) {
    // Instantiate bitvector to store frame bits/bytes
    bit_buffer = std::vector<bool>();
    // Write VP9 uncompressed header
    WriteVP9UncompressedHeader(&frame->uncompressed_header());

    // std::cout << bit_buffer.size() << std::endl;

    // Write VP9 compressed header to boolean encoding buffer
    InitBool();
    WriteVP9CompressedHeader(&frame->compressed_header());
    ExitBool();

    // std::cout << bit_buffer.size() << std::endl;

    // Write header size once we have the final size
    header_size_in_bytes = BoolPos;
    WriteBitUInt(header_size_in_bytes, 16);

    // Write trailing_bits
    WriteVP9TrailingBits();
    // Return if header size == 0
    if (header_size_in_bytes == 0) {
      return;
    }
    
    // Write Compressed Header boolean encoded buffer bytes
    WriteBitString(BoolBuffer, BoolPos * 8);


    // Write video frame tiles
    int32_t tile_count = 0;
    for (const Tile &tile : frame->tile()) {
      WriteVP9Tile(&tile, (tile_count == MAX_TILES || tile_count == (frame->tile().size() - 1)));
      // Limit tiles
      ++tile_count;
      if (tile_count > MAX_TILES) {
        break;
      }
    }
  }

  void WriteVP9FrameWithIVFHeader(const VP9Frame* vp9_frame) {
    // Save initial position
    uint32_t start_pos = bit_buffer.size() - 1;
    // Allocate 12 bytes in buffer for size and timestamp
    WriteBitString("", 12*8);
    // Write VP9 frame
    WriteVP9Frame(vp9_frame);
    // Get ending position
    uint32_t end_pos = bit_buffer.size() - 1;
    // Edit IVF frame header with VP9 frame size
    uint32_t frame_size_bytes = ceil((end_pos - start_pos) / 8.0);
    WriteBitUIntPos(((uint8_t*)&frame_size_bytes)[0], 8, start_pos); // little endian :P
    WriteBitUIntPos(((uint8_t*)&frame_size_bytes)[1], 8, start_pos + 8);
    WriteBitUIntPos(((uint8_t*)&frame_size_bytes)[2], 8, start_pos + 16);
    WriteBitUIntPos(((uint8_t*)&frame_size_bytes)[3], 8, start_pos + 24);
  }

  void WriteVP9Frames(VP9IVF* vp9_ivf) {
    // Write IVF marker
    WriteBitString("DKIF", 32);
    // Write Version Number
    WriteBitString("\x00\x00", 16);
    // Write length of header
    WriteBitString("\x20\x00", 16);
    // Write VP9 FourCC
    WriteBitString("VP90", 32);
    // Write Pixel Width
    WriteBitString("\x00\x00", 16);
    // Write Pixel Height
    WriteBitString("\x00\x00", 16);
    // Write Time Denominator (1000)
    WriteBitString("\xE8\x03\x00\x00", 32);
    // Write Time Numerator (1)
    WriteBitString("\x01\x00\x00\x00", 32);
    // Write number of frames in tile
    uint32_t frame_count = vp9_ivf->has_vp9_frame_1() +
                           vp9_ivf->has_vp9_frame_2() +
                           vp9_ivf->has_vp9_frame_3();
    WriteBitUInt(((uint8_t*) &frame_count)[0], 8); // little endian :P
    WriteBitUInt(((uint8_t*) &frame_count)[1], 8);
    // Write unused bytes
    WriteBitString("MICH", 32);
    // Write frames
    if (vp9_ivf->has_vp9_frame_1()) WriteVP9FrameWithIVFHeader(&vp9_ivf->vp9_frame_1());
    if (vp9_ivf->has_vp9_frame_2()) WriteVP9FrameWithIVFHeader(&vp9_ivf->vp9_frame_2());
    if (vp9_ivf->has_vp9_frame_3()) WriteVP9FrameWithIVFHeader(&vp9_ivf->vp9_frame_3());
  }
};