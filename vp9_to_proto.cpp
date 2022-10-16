#include <cmath>
#include <string>
#include <fstream>
#include <vector>
#include <bitset>

#include "vp9.pb.h"
#include "vp9_constants.h"

std::ifstream file;
std::vector<bool> bit_buffer;
uint64_t bit_counter = 0;
VP9Frame* vp9_frame;

UncompressedHeader_FrameType frame_type = (UncompressedHeader_FrameType) 0;
uint32_t profile = 0;
bool FrameIsIntra = false;
bool Lossless = false;
bool allow_high_precision_mv = false;
bool compoundReferenceAllowed = false;
uint32_t reference_mode = 0;
uint32_t interpolation_filter = 0;
uint32_t tx_mode = 0;
uint32_t header_size_in_bytes = 0;

uint32_t FrameWidth = 0;
uint32_t FrameHeight = 0;
uint32_t MiCols = 0;
uint32_t MiRows = 0;
uint32_t Sb64Cols = 0;
uint32_t Sb64Rows = 0;

uint64_t BoolValue = 0;
uint64_t BoolRange = 0;
int64_t BoolMaxBits = 0;

uint64_t ReadBitUInt(int bits) {
  uint64_t return_num = 0;
  uint64_t bound_bits = std::min(bits, 64);
  for (uint32_t i = 0; i < bound_bits; i++) {
    return_num = (return_num << 1) + bit_buffer.at(bit_counter++);
  }
  return return_num;
}

std::string ReadBitString(uint32_t bits) {
  std::string return_string;
  uint64_t bits_written = 0;
  while (true) {
    uint64_t remaining_bits = bits - bits_written;
    uint64_t bytes_to_read = remaining_bits > 8 ? 8 : remaining_bits;
    // Read byte
    uint8_t current_byte = ReadBitUInt(bytes_to_read);
    return_string.push_back(current_byte);
    // Check if we need to break
    bits_written += bytes_to_read;
    if (bits_written == bits) {
      break; 
    }
  }
  return return_string;
}

uint32_t ReadBool(uint32_t p) {
    uint32_t split = 1 + (((BoolRange - 1) * p) >> 8);
    uint32_t bit = 0;
    if (BoolValue < split) {
      BoolRange = split;
      bit = 0;
    }
    else {
      BoolRange -= split;
      BoolValue -= split;
      bit = 1;
    }

    if (BoolRange < 128) {
      uint32_t newBit = 0;
      if (BoolMaxBits > 0) {
        newBit = ReadBitUInt(1);
        BoolMaxBits -= 1;
      }
      else {
        newBit = 0;
      }

      BoolRange *= 2;
      BoolValue = (BoolValue << 1) + newBit;
    }

    return bit;
}

void InitBool(uint32_t sz) {
  BoolValue = ReadBitUInt(8);
  BoolRange = 255;
  BoolMaxBits = (8 * sz) - 8;
  ReadBool(128);
}

void ExitBool() {
  uint32_t padding_value = ReadBitUInt(BoolMaxBits);
}

int64_t ReadLiteral(int32_t bits) {
  int64_t return_num = 0;
  for (int i = 0; i < bits; i++) {
    return_num = (return_num << 1) + ReadBool(128);
  }
  return return_num;
}

VP9SignedInteger* ReadVP9SignedInteger(uint32_t number_bits) {
  auto signed_int = new VP9SignedInteger();

  signed_int->set_value(ReadBitString(number_bits));
  signed_int->set_sign((VP9BitField) ReadBitUInt(1));

  return signed_int;
}

UncompressedHeader_ColorConfig* ReadVP9ColorConfig() {
  auto color_config = new UncompressedHeader_ColorConfig();
  if (profile >= 2) {
    color_config->set_ten_or_twelve_bit((VP9BitField) ReadBitUInt(1));
  }
  uint32_t color_space = ReadBitUInt(3);
  color_config->set_color_space(color_space);
  if (color_space != UncompressedHeader_ColorConfig::CS_RGB) {
    color_config->set_color_range((VP9BitField) ReadBitUInt(1));
    if (profile == 1 || profile == 3) {
      color_config->set_subsampling_x((VP9BitField) ReadBitUInt(1));
      color_config->set_subsampling_y((VP9BitField) ReadBitUInt(1));
      color_config->set_reserved_zero((VP9BitField) ReadBitUInt(1));
    }
  }
  else {
    if (profile == 1 || profile == 3) {
      color_config->set_reserved_zero((VP9BitField) ReadBitUInt(1));
    }
  }
  return color_config;
}

void ComputeImageSize() {
  MiCols = (FrameWidth + 7) >> 3;
  MiRows = (FrameHeight + 7) >> 3;
  Sb64Cols = (MiCols + 7) >> 3;
  Sb64Rows = (MiRows + 7) >> 3;
  return;
}

UncompressedHeader_FrameSize* ReadVP9FrameSize() {
  auto frame_size = new UncompressedHeader_FrameSize();

  uint32_t frame_width_minus_1 = ReadBitUInt(16);
  uint32_t frame_height_minus_1 = ReadBitUInt(16);

  frame_size->set_frame_width_minus_1(frame_width_minus_1);
  frame_size->set_frame_height_minus_1(frame_height_minus_1);

  FrameWidth = frame_width_minus_1 + 1;
  FrameHeight = frame_height_minus_1 + 1;

  ComputeImageSize();

  return frame_size;
}

UncompressedHeader_RenderSize* ReadVP9RenderSize() {
  auto render_size = new UncompressedHeader_RenderSize();

  VP9BitField render_and_frame_size_different = (VP9BitField) ReadBitUInt(1);
  render_size->set_render_and_frame_size_different(render_and_frame_size_different);
  if (render_and_frame_size_different == 1) {
    render_size->set_render_width_minus_1(ReadBitUInt(16));
    render_size->set_render_height_minus_1(ReadBitUInt(16));
  }
  return render_size;
}

UncompressedHeader_LoopFilterParams* ReadVP9LoopFilterParams() {
  auto loop_filter_params = new UncompressedHeader_LoopFilterParams();

  loop_filter_params->set_loop_filter_level(ReadBitUInt(6));
  loop_filter_params->set_loop_filter_sharpness(ReadBitUInt(3));

  VP9BitField loop_filter_delta_enabled = (VP9BitField) ReadBitUInt(1);
  loop_filter_params->set_loop_filter_delta_enabled(loop_filter_delta_enabled);

  std::cout << "Loop Filter Delta Enabled: " << loop_filter_delta_enabled << std::endl;

  if (loop_filter_delta_enabled == 1) {
    VP9BitField loop_filter_delta_update = (VP9BitField) ReadBitUInt(1);
    loop_filter_params->set_loop_filter_delta_update(loop_filter_delta_update);

    std::cout << "Loop Filter Delta Update: " << loop_filter_delta_update << std::endl;

    if (loop_filter_delta_update == 1) {
      for (int i = 0; i < 4; i++) {
        loop_filter_params->add_ref_delta();
        bool update_ref_delta = ReadBitUInt(1);
        loop_filter_params->mutable_ref_delta(i)->set_update_ref_delta((VP9BitField) update_ref_delta);
        if (update_ref_delta == 1) {
          loop_filter_params->mutable_ref_delta(i)->set_allocated_loop_filter_ref_deltas(ReadVP9SignedInteger(6));
        }
        std::cout << "Bits Read: " << bit_counter << " / " << bit_buffer.size() << std::endl;
      }
      for (int i = 0; i < 2; i++) {
        loop_filter_params->add_mode_delta();
        bool update_mode_delta = ReadBitUInt(1);
        loop_filter_params->mutable_mode_delta(i)->set_update_mode_delta((VP9BitField) update_mode_delta);
        if (update_mode_delta == 1) {
          loop_filter_params->mutable_mode_delta(i)->set_allocated_loop_filter_mode_deltas(ReadVP9SignedInteger(6));
        }
      }
    }
  }

  return loop_filter_params;
}

UncompressedHeader_QuantizationParams_ReadDeltaQ* ReadVP9ReadDeltaQ() {
  auto read_delta_q = new UncompressedHeader_QuantizationParams_ReadDeltaQ();

  VP9BitField delta_coded = (VP9BitField) ReadBitUInt(1);
  read_delta_q->set_delta_coded(delta_coded);
  std::cout << "Delta Coded: " << delta_coded << std::endl;
  if (delta_coded) {
    read_delta_q->set_allocated_delta_q(ReadVP9SignedInteger(4));
  }
  return read_delta_q;
}

UncompressedHeader_QuantizationParams* ReadVP9QuantizationParams() {
  auto quantization_params = new UncompressedHeader_QuantizationParams();

  uint32_t base_q_idx = ReadBitUInt(8);
  auto delta_q_y_dc = ReadVP9ReadDeltaQ();
  auto delta_q_uv_dc = ReadVP9ReadDeltaQ();
  auto delta_q_uv_ac = ReadVP9ReadDeltaQ();

  quantization_params->set_base_q_idx(base_q_idx);
  quantization_params->set_allocated_delta_q_y_dc(delta_q_y_dc);
  quantization_params->set_allocated_delta_q_uv_dc(delta_q_uv_dc);
  quantization_params->set_allocated_delta_q_uv_ac(delta_q_uv_ac);

  Lossless = (base_q_idx == 0 
              && delta_q_y_dc->delta_q().value().empty() 
              && delta_q_uv_dc->delta_q().value().empty()
              && delta_q_uv_ac->delta_q().value().empty());

  return quantization_params;
}

UncompressedHeader_SegmentationParams* ReadVP9SegmentationParams() {
  auto segmentation_params = new UncompressedHeader_SegmentationParams();

  VP9BitField segmentation_enabled = (VP9BitField) ReadBitUInt(1);
  std::cout << "Segmentation Enabled: " << segmentation_enabled << std::endl;
  segmentation_params->set_segmentation_enabled(segmentation_enabled);

  if (segmentation_enabled == 1) {
    VP9BitField segmentation_update_map = (VP9BitField) ReadBitUInt(1);
    segmentation_params->set_segmentation_update_map(segmentation_update_map);

    if (segmentation_update_map == 1) {
      // Read probabilities
      uint32_t probs_read = 0;
      for (int i = 0; i < 7; i++) {
        segmentation_params->add_prob();
        auto prob = segmentation_params->mutable_prob(probs_read++);

        bool prob_coded = ReadBitUInt(1);
        prob->set_prob_coded((VP9BitField) prob_coded);
        if (prob_coded) {
          prob->set_prob(ReadBitUInt(8));
        }
      }
      bool segmentation_temporal_update = ReadBitUInt(1);
      segmentation_params->set_segmentation_temporal_update((VP9BitField) segmentation_temporal_update);
      if (segmentation_temporal_update) {
        segmentation_params->add_prob();
        auto prob = segmentation_params->mutable_prob(probs_read++);

        bool prob_coded = ReadBitUInt(1);
        prob->set_prob_coded((VP9BitField) prob_coded);
        if (prob_coded) {
          prob->set_prob(ReadBitUInt(8));
        }
      }
    }

    VP9BitField segmentation_update_data = (VP9BitField) ReadBitUInt(1);
    segmentation_params->set_segmentation_update_data(segmentation_update_data);

    // Read segmentation features
    uint32_t feature_index = 0;
    if (segmentation_update_data == 1) {
      segmentation_params->set_segmentation_abs_or_delta_update((VP9BitField) ReadBitUInt(1));
      for (int i = 0; i < 8; i++) {
        for (int j = 0; j < SEG_LVL_MAX; j++) {
          segmentation_params->add_features();

          auto feature = segmentation_params->mutable_features(feature_index++);

          VP9BitField feature_enabled = (VP9BitField) ReadBitUInt(1);
          feature->set_feature_enabled(feature_enabled);

          if (feature_enabled == 1) {
            feature->set_feature_value(ReadBitString(segmentation_feature_bits[j]));
            if (segmentation_feature_signed[j] == 1) {
              feature->set_feature_sign((VP9BitField) ReadBitUInt(1));
            }
          }
        }
      }
    }
  }
  return segmentation_params;
}

uint32_t CalcMinLog2TileCols() {
  uint32_t minLog2 = 0;
  while ((MAX_TILE_WIDTH_B64 << minLog2) < Sb64Cols) {
    ++minLog2;
  }
  return minLog2;
}

uint32_t CalcMaxLog2TileCols() {
  uint32_t maxLog2 = 1;
  while ((Sb64Cols >> maxLog2) >= MIN_TILE_WIDTH_B64 ) {
    ++maxLog2;
  }
  return maxLog2 - 1;
}

UncompressedHeader_TileInfo* ReadVP9TileInfo() {
  // TODO: Revise this, although a full ref tracking system would be needed to make 100% accurate
  auto tile_info = new UncompressedHeader_TileInfo();
  uint32_t minLog2TileCols = CalcMinLog2TileCols();  
  uint32_t maxLog2TileCols = CalcMaxLog2TileCols();
  uint32_t tile_cols_log2 = minLog2TileCols;
  while (tile_cols_log2 < maxLog2TileCols) {
    VP9BitField increment_tile_cols_log2 = (VP9BitField) ReadBitUInt(1);
    tile_info->add_increment_tile_cols_log2(increment_tile_cols_log2);
    if (increment_tile_cols_log2 == 1) {
      ++tile_cols_log2;
    }
    else break;
  } 
  // Read tile_rows_log2
  VP9BitField tile_rows_log2 = (VP9BitField) ReadBitUInt(1);
  tile_info->set_tile_rows_log2(tile_rows_log2);
  // Read increment bit if tile_rows_log2 is set
  if (tile_rows_log2 == 1) {
    VP9BitField increment_tile_rows_log2 = (VP9BitField) ReadBitUInt(1);
    tile_info->set_increment_tile_rows_log2(increment_tile_rows_log2);
  }
  return tile_info;
}

UncompressedHeader_ReadInterpolationFilter* ReadVP9ReadInterpolationFilter() {
  auto read_interpolation_filter = new UncompressedHeader_ReadInterpolationFilter();

  VP9BitField is_filter_switchable = (VP9BitField) ReadBitUInt(1);
  read_interpolation_filter->set_is_filter_switchable(is_filter_switchable);

  if (is_filter_switchable == 1) {
    interpolation_filter = UncompressedHeader_InterpolationFilter_SWITCHABLE;
  }
  else {
    read_interpolation_filter->set_raw_interpolation_filter((UncompressedHeader_InterpolationFilter) ReadBitUInt(2));
  }

  return read_interpolation_filter;
}

UncompressedHeader* ReadVP9UncompressedHeader() {
  auto uncompressed_header = new UncompressedHeader();

  // Read marker
  ReadBitUInt(2);

  uint32_t profile_low_bit = ReadBitUInt(1);
  uint32_t profile_high_bit = ReadBitUInt(1);
  profile = (profile_high_bit << 1) + profile_low_bit;

  std::cout << "Profile: " << profile << std::endl;

  uncompressed_header->set_profile_low_bit((VP9BitField) profile_low_bit);
  uncompressed_header->set_profile_high_bit((VP9BitField) profile_high_bit);
  if (profile == 3) {
    uncompressed_header->set_reserved_zero(ReadBitUInt(1));
  }

  VP9BitField show_existing_frame = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_show_existing_frame(show_existing_frame);

  if (show_existing_frame == 1) {
    uncompressed_header->set_frame_to_show_map_idx(ReadBitUInt(3));
    header_size_in_bytes = 0;
    return uncompressed_header;
  }
  frame_type = (UncompressedHeader_FrameType) ReadBitUInt(1);
  uncompressed_header->set_frame_type(frame_type);
  
  VP9BitField show_frame = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_show_frame(show_frame);

  VP9BitField error_resilient_mode = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_error_resilient_mode(error_resilient_mode);

  std::cout << "Frame Type: " << frame_type << std::endl;
  
  if (frame_type == UncompressedHeader_FrameType_KEY_FRAME) {
    FrameIsIntra = true;
    uncompressed_header->set_frame_sync_code(ReadBitUInt(24));
    uncompressed_header->set_allocated_color_config(ReadVP9ColorConfig());
    uncompressed_header->set_allocated_frame_size(ReadVP9FrameSize());
    uncompressed_header->set_allocated_render_size(ReadVP9RenderSize());
  }
  else {
    uint32_t intra_only = 0;

    if (show_frame == 0) {
      intra_only = ReadBitUInt(1);
      uncompressed_header->set_intra_only((VP9BitField) intra_only);
    }
    FrameIsIntra = intra_only;

    if (error_resilient_mode == 0) {
      uncompressed_header->set_reset_frame_context(ReadBitUInt(2));
    }

    if (intra_only == 1) {
      uncompressed_header->set_frame_sync_code(ReadBitUInt(3));
      if (profile > 0) {
        uncompressed_header->set_allocated_color_config(ReadVP9ColorConfig());
      }
      uncompressed_header->set_refresh_frame_flags(ReadBitUInt(8));
      uncompressed_header->set_allocated_frame_size(ReadVP9FrameSize());
      uncompressed_header->set_allocated_render_size(ReadVP9RenderSize());
    }
    else {
      // refresh_frame_flags
      uncompressed_header->set_refresh_frame_flags(ReadBitUInt(8));
      // ref_frame_idx and ref_frame_sign_bias
      VP9BitField first_ref_frame_sign_bias = (VP9BitField) 0;
      for (uint32_t i = 0; i < 3; i++) {
        uncompressed_header->add_ref_frame_idx(ReadBitUInt(3));

        VP9BitField ref_frame_sign_bias = (VP9BitField) ReadBitUInt(1);
        uncompressed_header->add_ref_frame_sign_bias(ref_frame_sign_bias);

        if (i == 0) {
          first_ref_frame_sign_bias = ref_frame_sign_bias;
        }
        else if (ref_frame_sign_bias != first_ref_frame_sign_bias) {
          compoundReferenceAllowed = true;
        }
      }
      // frame_size_with_refs
      uint32_t frame_size_found_ref = ReadBitUInt(3);
      uncompressed_header->set_frame_size_found_ref(frame_size_found_ref);
      if (frame_size_found_ref == 0) {
        uncompressed_header->set_allocated_frame_size(ReadVP9FrameSize());
      }
      uncompressed_header->set_allocated_render_size(ReadVP9RenderSize());
      // allow_high_precision_mv
      allow_high_precision_mv = ReadBitUInt(1);
      uncompressed_header->set_allow_high_precision_mv((VP9BitField) allow_high_precision_mv);
      uncompressed_header->set_allocated_read_interpolation_filter(ReadVP9ReadInterpolationFilter());
    }

  }

  std::cout << "Error Resilient Mode: " << error_resilient_mode << std::endl;

  if (error_resilient_mode == 0) {
    uncompressed_header->set_refresh_frame_flags(ReadBitUInt(1));
    uncompressed_header->set_frame_parallel_decoding_mode((VP9BitField)ReadBitUInt(1));
  }
  uncompressed_header->set_frame_context_idx(ReadBitUInt(2));

  uncompressed_header->set_allocated_loop_filter_params(ReadVP9LoopFilterParams());
  uncompressed_header->set_allocated_quantization_params(ReadVP9QuantizationParams());
  uncompressed_header->set_allocated_segmentation_params(ReadVP9SegmentationParams());
  uncompressed_header->set_allocated_tile_info(ReadVP9TileInfo());

  header_size_in_bytes = ReadBitUInt(16);
  std::cout << "Compressed Header Size: " << header_size_in_bytes << std::endl;
  uncompressed_header->set_header_size_in_bytes(header_size_in_bytes);

  return uncompressed_header;
}

CompressedHeader_ReadTxMode* ReadVP9ReadTxMode() {
  auto read_tx_mode = new CompressedHeader_ReadTxMode();

  if (Lossless == true) {
    tx_mode = CompressedHeader_TxMode_ONLY_4X4;
  }
  else {
    tx_mode = ReadLiteral(2);
    read_tx_mode->set_tx_mode((CompressedHeader_TxMode) tx_mode);
    if (tx_mode == CompressedHeader_TxMode_ALLOW_32X32) {
      VP9BitField tx_mode_select = (VP9BitField) ReadLiteral(1);
      read_tx_mode->set_tx_mode_select(tx_mode_select);
      tx_mode += tx_mode_select;
    }
  }
  
  return read_tx_mode;
}

uint32_t ReadVP9Uniform() {
  const int l = 8;
  const int m = (1 << l) - 191;
  const int v = ReadLiteral(l - 1);
  return v < m ? v : (v << 1) - m + ReadBool(128);
}

CompressedHeader_DecodeTermSubexp* ReadVP9DecodeTermSubexp() {
  auto decode_term_subexp = new CompressedHeader_DecodeTermSubexp();

  VP9BitField bit_1 = (VP9BitField) ReadLiteral(1);
  decode_term_subexp->set_bit_1(bit_1);
  if (bit_1 == 0) {
    decode_term_subexp->set_sub_exp_val(ReadLiteral(4));
    return decode_term_subexp;
  }

  VP9BitField bit_2 = (VP9BitField) ReadLiteral(1);
  decode_term_subexp->set_bit_2(bit_2);
  if (bit_2 == 0) {
    decode_term_subexp->set_sub_exp_val_minus_16(ReadLiteral(4));
    return decode_term_subexp;
  }

  VP9BitField bit_3 = (VP9BitField) ReadLiteral(1);
  decode_term_subexp->set_bit_3(bit_3);
  if (bit_3 == 0) {
    decode_term_subexp->set_sub_exp_val_minus_32(ReadLiteral(5));
    return decode_term_subexp;
  }

  uint32_t v = ReadVP9Uniform();
  decode_term_subexp->set_v(v);
  if (v < 65) {
    return decode_term_subexp;
  }

  decode_term_subexp->set_bit_4((VP9BitField) ReadLiteral(1));

  return decode_term_subexp;
}

void ReadVP9DiffUpdateProb(CompressedHeader_DiffUpdateProb* diff_update_prob) {
  // Reads values into ptr arg because of how repeated ptr fields work in protobuf lib
  VP9BitField update_prob = (VP9BitField) ReadBool(252);
  diff_update_prob->set_update_prob(update_prob);
  if (update_prob == 1) {
    diff_update_prob->set_allocated_decode_term_subexp(ReadVP9DecodeTermSubexp());
  }
}

CompressedHeader_TxModeProbs* ReadVP9TxModeProbs() {
  auto tx_mode_probs = new CompressedHeader_TxModeProbs();

  for (uint32_t i = 0; i < 12; i++) {
    tx_mode_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(tx_mode_probs->mutable_diff_update_prob(i));
  }

  return tx_mode_probs;
}

CompressedHeader_ReadCoefProbs* ReadVP9ReadCoefProbs() {
  auto read_coef_probs = new CompressedHeader_ReadCoefProbs();

  for (uint32_t txSz = TX_4X4; txSz < tx_mode_to_biggest_tx_size[tx_mode]; txSz++) {
    read_coef_probs->add_read_coef_probs();
    auto loop_obj = read_coef_probs->mutable_read_coef_probs(txSz);

    // Write the update_probs indicator bit
    VP9BitField update_probs = (VP9BitField) ReadLiteral(1);
    std::cout << "Update Read Coef Probs: " << update_probs << std::endl;
    loop_obj->set_update_probs(update_probs);
    if (update_probs == 1) {
      for (uint32_t i = 0; i < 396; i ++) {
        loop_obj->add_diff_update_prob();
        ReadVP9DiffUpdateProb(loop_obj->mutable_diff_update_prob(i));
      }
    }
  }
  return read_coef_probs; 
}

CompressedHeader_ReadSkipProb* ReadVP9ReadSkipProb() {
  auto read_skip_prob = new CompressedHeader_ReadSkipProb();

  for (uint32_t i = 0; i < 3; i++) {
    read_skip_prob->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_skip_prob->mutable_diff_update_prob(i));
  }

  return read_skip_prob;
}

CompressedHeader_ReadInterModeProbs* ReadVP9ReadInterModeProbs() {
  auto read_inter_mode_probs = new CompressedHeader_ReadInterModeProbs();

  for (uint32_t i = 0; i < 21; i++) {
    read_inter_mode_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_inter_mode_probs->mutable_diff_update_prob(i));
  }

  return read_inter_mode_probs;
}

CompressedHeader_ReadInterpFilterProbs* ReadVP9ReadInterpFilterProbs() {
  auto read_interp_filter_probs = new CompressedHeader_ReadInterpFilterProbs();

  for (uint32_t i = 0; i < 14; i++) {
    read_interp_filter_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_interp_filter_probs->mutable_diff_update_prob(i));
  } 

  return read_interp_filter_probs;
}

CompressedHeader_ReadIsInterProbs* ReadVP9ReadIsInterProbs() {
  auto read_is_inter_probs = new CompressedHeader_ReadIsInterProbs();

  for (uint32_t i = 0; i < 4; i++) {
    read_is_inter_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_is_inter_probs->mutable_diff_update_prob(i));
  }

  return read_is_inter_probs;
}

CompressedHeader_FrameReferenceMode* ReadVP9FrameReferenceMode() {
  auto frame_reference_mode = new CompressedHeader_FrameReferenceMode();

  std::cout << "Compound Reference Allowed: " << compoundReferenceAllowed << std::endl;

  if (compoundReferenceAllowed == 1) {
    VP9BitField non_single_reference = (VP9BitField) ReadLiteral(1);
    frame_reference_mode->set_non_single_reference(non_single_reference);

    if (non_single_reference == 0) {
      reference_mode = SINGLE_REFERENCE;
    }
    else {
      VP9BitField reference_select = (VP9BitField) ReadLiteral(1);
      frame_reference_mode->set_reference_select(reference_select);

      if (reference_select == 0) {
        reference_mode = COMPOUND_REFERENCE;
      }
      else {
        reference_mode = REFERENCE_MODE_SELECT;
      }
    }
  }
  else {
    reference_mode = SINGLE_REFERENCE;
  }

  return frame_reference_mode;
}

CompressedHeader_FrameReferenceModeProbs* ReadVP9FrameReferenceModeProbs() {
  auto frame_reference_mode_probs = new CompressedHeader_FrameReferenceModeProbs();
  
  uint32_t written_count = 0;
  if (reference_mode == REFERENCE_MODE_SELECT) {
    for (uint32_t i = 0; i < 5; i++) {
      frame_reference_mode_probs->add_diff_update_prob();
      ReadVP9DiffUpdateProb(frame_reference_mode_probs->mutable_diff_update_prob(written_count++));
    }
  }
  if (reference_mode != COMPOUND_REFERENCE) {
    for (uint32_t i = 0; i < 5; i++) {
      frame_reference_mode_probs->add_diff_update_prob();
      ReadVP9DiffUpdateProb(frame_reference_mode_probs->mutable_diff_update_prob(written_count++));
    }
  }
  if (reference_mode != SINGLE_REFERENCE) {
    for (uint32_t i = 0; i < 5; i++) {
      frame_reference_mode_probs->add_diff_update_prob();
      ReadVP9DiffUpdateProb(frame_reference_mode_probs->mutable_diff_update_prob(written_count++));
    }
  }

  return frame_reference_mode_probs;
}

CompressedHeader_ReadYModeProbs* ReadVP9ReadYModeProbs() {
  auto read_y_mode_probs = new CompressedHeader_ReadYModeProbs();

  for (uint32_t i = 0; i < 36; i++) {
    read_y_mode_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_y_mode_probs->mutable_diff_update_prob(i));
  }

  return read_y_mode_probs;
}

CompressedHeader_ReadPartitionProbs* ReadVP9ReadPartitionProbs() {
  auto read_partition_probs = new CompressedHeader_ReadPartitionProbs();

  for (uint32_t i = 0; i < 48; i++) {
    read_partition_probs->add_diff_update_prob();
    ReadVP9DiffUpdateProb(read_partition_probs->mutable_diff_update_prob(i));
  }

  return read_partition_probs;
}

void ReadVP9MvProbsLoop(CompressedHeader_MvProbs_MvProbsLoop* mv_probs_loop) {
  VP9BitField update_mv_prob = (VP9BitField) ReadBool(252);
  mv_probs_loop->set_update_mv_prob(update_mv_prob);
  if (update_mv_prob == 1) {
    mv_probs_loop->set_mv_prob(ReadLiteral(7));
  }
}

CompressedHeader_MvProbs* ReadVP9MvProbs() {
  auto mv_probs = new CompressedHeader_MvProbs();

  for (uint32_t i = 0; i < 65; i++) {
    mv_probs->add_mv_probs();
    ReadVP9MvProbsLoop(mv_probs->mutable_mv_probs(i));
  }

  if (allow_high_precision_mv) {
    for (uint32_t i = 65; i < (65 + 4); i++) {
      mv_probs->add_mv_probs();
      ReadVP9MvProbsLoop(mv_probs->mutable_mv_probs(i));
    }
  }

  return mv_probs;
}

CompressedHeader* ReadVP9CompressedHeader() {
  auto compressed_header = new CompressedHeader();

  compressed_header->set_allocated_read_tx_mode(ReadVP9ReadTxMode());
  
  std::cout << "Compressed Header TxMode: " << tx_mode << std::endl;

  if (tx_mode == CompressedHeader_TxMode_TX_MODE_SELECT) {
    compressed_header->set_allocated_tx_mode_probs(ReadVP9TxModeProbs());
  }

  compressed_header->set_allocated_read_coef_probs(ReadVP9ReadCoefProbs());
  compressed_header->set_allocated_read_skip_prob(ReadVP9ReadSkipProb());

  std::cout << "FrameIsIntra: " << FrameIsIntra << std::endl;

  if (FrameIsIntra == 0) {
    compressed_header->set_allocated_read_inter_mode_probs(ReadVP9ReadInterModeProbs());

    std::cout << "Interpolation Filter: " << interpolation_filter << std::endl;

    if (interpolation_filter == UncompressedHeader_InterpolationFilter_SWITCHABLE) {
      compressed_header->set_allocated_read_interp_filter_probs(ReadVP9ReadInterpFilterProbs());
    }
    compressed_header->set_allocated_read_is_inter_probs(ReadVP9ReadIsInterProbs());
    compressed_header->set_allocated_frame_reference_mode(ReadVP9FrameReferenceMode());
    compressed_header->set_allocated_frame_reference_mode_probs(ReadVP9FrameReferenceModeProbs());
    compressed_header->set_allocated_read_y_mode_probs(ReadVP9ReadYModeProbs());
    compressed_header->set_allocated_read_partition_probs(ReadVP9ReadPartitionProbs());
    compressed_header->set_allocated_mv_probs(ReadVP9MvProbs());
  }

  return compressed_header;
}

void ReadVP9TrailingBits() {
  while (bit_counter & 7) {
    ReadBitUInt(1);
  }
}

// void ReadVP9Tile(Tile* tile) {
//   uint32_t remaining_bytes = floor((bit_buffer.size() - bit_counter) / 8);
//   // Check if this is the last tile
//   //  32 bytes for tile_size and 12 for at least 1 bool-coded tile
//   uint32_t tile_size;
//   if (remaining_bytes < 44) {
//     tile_size = remaining_bytes;
//   }
//   else {
//     tile_size = ReadBitUInt(32);
//   }
//   // Decode tile
//   std::cout << "Tile Size: " << tile_size << std::endl;
//   tile->set_tile_size(tile_size);

//   InitBool(tile_size);

//   std::string bool_buffer;
//   while (BoolMaxBits != 0) {
//     uint8_t bool_data = 0;
//     for (uint32_t i = 0; i < 8; i++) {
//       if (BoolMaxBits != 0) {
//         bool_data = (bool_data << 1) + ReadBool(1);
//         BoolMaxBits -= 1;
//       }
//       else {
//         bool_data <<= 1;
//       }
//     }
//     bool_buffer.push_back(bool_data);
//   }
//   uint8_t bool_data = 0;
//   tile->set_partition(bool_buffer);

//   ExitBool();
// }

VP9Frame* VP9ToProto(std::string vp9_frame_file_path) {
  // Create VP9 Protobuf Object
  vp9_frame = new VP9Frame();
  // Open file
  file = std::ifstream(vp9_frame_file_path, std::ios::binary);
  // Read into bit vector
  bit_buffer = std::vector<bool>();
  uint8_t data;
  while (file.read((char *)&data, 1)) {
    for (int i = 0; i < 8; i++) {
      bit_buffer.push_back(((data >> (7 - i)) & 0b1));
    }
  }
  // Check that we have data to parse
  if (bit_buffer.size() < 5) {
    std::cerr << "Failed to read file: " << vp9_frame_file_path << std::endl; 
    exit(0);
  }
  // Read Headers
  vp9_frame->set_allocated_uncompressed_header(ReadVP9UncompressedHeader());
  std::cout << "Wrote Uncompressed Header" << std::endl;
  std::cout << "Bits Read: " << bit_counter << " / " << bit_buffer.size() << std::endl;
  
  ReadVP9TrailingBits();

  if (header_size_in_bytes == 0) {
    std::cout << "Repeat Frame, " << bit_counter << " / " << bit_buffer.size() << std::endl;
    return vp9_frame;
  }

  InitBool(header_size_in_bytes);
  vp9_frame->set_allocated_compressed_header(ReadVP9CompressedHeader());
  ExitBool();

  std::cout << "Wrote Compressed Header" << std::endl;
  std::cout << "Bits Read: " << bit_counter << " / " << bit_buffer.size() << std::endl;
  // Read Tiles

  vp9_frame->set_tiles(ReadBitString(bit_buffer.size() - bit_counter));
  // uint32_t tile_count = 0;
  // while (bit_counter < bit_buffer.size()) {
  //   vp9_frame->add_tiles();
  //   ReadVP9Tile(vp9_frame->mutable_tiles(tile_count++));
  //   std::cout << "Bits Read: " << bit_counter << " / " << bit_buffer.size() << std::endl;
  // }
  std::cout << "Bits Read: " << bit_counter << " / " << bit_buffer.size() << std::endl;

  return vp9_frame;
}

int main(int argc, char** argv) {
  // Convert vp9 binary frame to protobuf
  VP9Frame* vp9_frame = VP9ToProto("./test_frame_in");

  // Serialize protobuf and store to file
  std::ofstream ofs("./test_frame_protobuf", std::ios_base::out | std::ios_base::binary);
  vp9_frame->SerializeToOstream(&ofs);
  return 0;
}
