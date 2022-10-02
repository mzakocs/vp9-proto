#include <cmath>
#include <string>
#include <fstream>
#include <vector>

#include "vp9.pb.h"
#include "vp9_constants.h"

std::ifstream file;
std::vector<bool> bit_buffer;
uint64_t bit_counter = 0;
VP9Frame* vp9_frame;

UncompressedHeader_FrameType frame_type;
UncompressedHeader_Profile profile;
bool FrameIsIntra = false;
bool Lossless = false;
bool allow_high_precision_mv = false;
bool compoundReferenceAllowed = false;
uint32_t reference_mode = 0;
uint32_t interpolation_filter = 0;
uint32_t tx_mode = 0;
uint32_t header_size_in_bytes = 0;

uint64_t ReadBitUInt(uint32_t bits) {
  uint64_t return_num = 0;
  for (uint32_t i = 0; i < bits; i++) {
    return_num |= bit_buffer.at(bit_counter++) << i;
  }
  return __builtin_bswap64(return_num);
}

std::string ReadBitString(uint32_t bits) {
  std::string return_string;
  uint64_t bits_written = 0;
  while (true) {
    char current_byte = NULL;
    for (uint64_t i = 0; i < 8; i++) {
      // Check if we're at bit cap
      if (bits == bits_written) {
        return return_string;
      }
      // Otherwise write bit to string
      current_byte |= bit_buffer.at(bit_counter++) << i;
    } 
    return_string.push_back(current_byte);
  }
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

UncompressedHeader_FrameSize* ReadVP9FrameSize() {
  auto frame_size = new UncompressedHeader_FrameSize();

  frame_size->set_frame_width_minus_1(ReadBitUInt(16));
  frame_size->set_frame_height_minus_1(ReadBitUInt(16));

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

  if (loop_filter_delta_enabled == 1) {
    VP9BitField loop_filter_delta_update = (VP9BitField) ReadBitUInt(1);
    loop_filter_params->set_loop_filter_delta_update(loop_filter_delta_update);

    if (loop_filter_delta_update == 1) {
      for (int i = 0; i < 4; i++) {
        loop_filter_params->add_ref_delta();
        loop_filter_params->mutable_ref_delta(i)->set_update_ref_delta((VP9BitField) ReadBitUInt(1));
        loop_filter_params->mutable_ref_delta(i)->set_allocated_loop_filter_ref_deltas(ReadVP9SignedInteger(6));
      }
      for (int i = 0; i < 2; i++) {
        loop_filter_params->add_mode_delta();
        loop_filter_params->mutable_mode_delta(i)->set_update_mode_delta((VP9BitField) ReadBitUInt(1));
        loop_filter_params->mutable_mode_delta(i)->set_allocated_loop_filter_mode_deltas(ReadVP9SignedInteger(6));
      }
    }
  }

  return loop_filter_params;
}

UncompressedHeader_QuantizationParams_ReadDeltaQ* ReadVP9ReadDeltaQ() {
  auto read_delta_q = new UncompressedHeader_QuantizationParams_ReadDeltaQ();

  VP9BitField delta_coded = (VP9BitField) ReadBitUInt(1);
  read_delta_q->set_delta_coded(delta_coded);
  if (delta_coded == 1) {
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

  Lossless = (base_q_idx == 0 
              && delta_q_y_dc->delta_q().value().empty() 
              && delta_q_uv_dc->delta_q().value().empty()
              && delta_q_uv_ac->delta_q().value().empty());

  return quantization_params;
}

UncompressedHeader_SegmentationParams* ReadVP9SegmentationParams() {
  auto segmentation_params = new UncompressedHeader_SegmentationParams();

  VP9BitField segmentation_enabled = (VP9BitField) ReadBitUInt(1);
  segmentation_params->set_segmentation_enabled(segmentation_enabled);

  if (segmentation_enabled == 1) {
    VP9BitField segmentation_update_map = (VP9BitField) ReadBitUInt(1);
    segmentation_params->set_segmentation_update_map(segmentation_update_map);

    if (segmentation_update_map == 1) {
      segmentation_params->set_segmentation_temporal_update((VP9BitField) ReadBitUInt(1));
    }

    VP9BitField segmentation_update_data = (VP9BitField) ReadBitUInt(1);
    segmentation_params->set_segmentation_update_data(segmentation_update_data);

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

UncompressedHeader_TileInfo* ReadVP9TileInfo() {
  auto tile_info = new UncompressedHeader_TileInfo();
  // Read all 1 bits for increment_tile_cols_log2 (unsupported in protobuf so we ignore)
  // We would have to calculate tile cols/rows to get right 100% of the time but a max of 6 is good enough for now
  uint32_t count = 0;
  while ((ReadBitUInt(1) == 1) && (count++ < 6));
  // Read tile_rows_log2
  VP9BitField tile_rows_log2 = (VP9BitField) ReadBitUInt(1);
  tile_info->set_tile_rows_log2(tile_rows_log2);
  // Read increment bit if tile_rows_log2 is set
  if (tile_rows_log2 == 1) {
    tile_info->set_increment_tile_rows_log2((VP9BitField) ReadBitUInt(1));
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

  uncompressed_header->set_marker(0b10);

  profile = (UncompressedHeader_Profile) ReadBitUInt(2);
  uncompressed_header->set_profile(profile);
  if (profile == 3) {
    uncompressed_header->set_reserved_zero(ReadBitUInt(1));
  }

  VP9BitField show_existing_frame = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_show_existing_frame(show_existing_frame);

  if (show_existing_frame == 1) {
    uncompressed_header->set_frame_to_show_map_idx(ReadBitUInt(3));
    header_size_in_bytes = 0;
    return;
  }
  frame_type = (UncompressedHeader_FrameType) ReadBitUInt(1);
  uncompressed_header->set_frame_type(frame_type);
  
  VP9BitField show_frame = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_show_frame(show_frame);

  VP9BitField error_resilient_mode = (VP9BitField) ReadBitUInt(1);
  uncompressed_header->set_error_resilient_mode(error_resilient_mode);

  if (frame_type == UncompressedHeader_FrameType_KEY_FRAME) {
    FrameIsIntra = true;
    uncompressed_header->set_frame_sync_code(ReadBitUInt(3));
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
      uncompressed_header->set_reset_frame_context(ReadBitUInt(1));
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
  uncompressed_header->set_header_size_in_bytes(header_size_in_bytes);

  return uncompressed_header;
}

CompressedHeader_ReadTxMode* ReadVP9ReadTxMode() {
  auto read_tx_mode = new CompressedHeader_ReadTxMode();

  if (Lossless == true) {
    tx_mode = CompressedHeader_TxMode_ONLY_4X4;
  }
  else {
    tx_mode = ReadBitUInt(2);
    read_tx_mode->set_tx_mode((CompressedHeader_TxMode) tx_mode);
    if (tx_mode == CompressedHeader_TxMode_ALLOW_32X32) {
      VP9BitField tx_mode_select = (VP9BitField) ReadBitUInt(1);
      read_tx_mode->set_tx_mode_select(tx_mode_select);
      tx_mode += tx_mode_select;
    }
  }
  
  return read_tx_mode;
}

CompressedHeader_DecodeTermSubexp* ReadVP9DecodeTermSubexp() {
  auto decode_term_subexp = new CompressedHeader_DecodeTermSubexp();

  VP9BitField bit_1 = (VP9BitField) ReadBitUInt(1);
  decode_term_subexp->set_bit_1(bit_1);
  if (bit_1 == 0) {
    decode_term_subexp->set_sub_exp_val(ReadBitUInt(4));
    return decode_term_subexp;
  }

  VP9BitField bit_2 = (VP9BitField) ReadBitUInt(1);
  decode_term_subexp->set_bit_2(bit_2);
  if (bit_2 == 0) {
    decode_term_subexp->set_sub_exp_val_minus_16(ReadBitUInt(4));
    return decode_term_subexp;
  }

  VP9BitField bit_3 = (VP9BitField) ReadBitUInt(1);
  decode_term_subexp->set_bit_3(bit_3);
  if (bit_3 == 0) {
    decode_term_subexp->set_sub_exp_val_minus_32(ReadBitUInt(5));
    return decode_term_subexp;
  }

  uint32_t v = ReadBitUInt(7);
  if (v < 65) {
    return decode_term_subexp;
  }

  decode_term_subexp->set_bit_4((VP9BitField) ReadBitUInt(1));

  return decode_term_subexp;
}

void ReadVP9DiffUpdateProb(CompressedHeader_DiffUpdateProb* diff_update_prob) {
  // Reads values into ptr arg because of how repeated ptr fields work in protobuf lib
  VP9BitField update_prob = (VP9BitField) ReadBitUInt(1);
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
    VP9BitField update_probs = (VP9BitField) ReadBitUInt(1);
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

  for (uint32_t i = 0; i < 8; i++) {
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

  if (compoundReferenceAllowed == 1) {
    VP9BitField non_single_reference = (VP9BitField) ReadBitUInt(1);
    frame_reference_mode->set_non_single_reference(non_single_reference);

    if (non_single_reference == 0) {
      reference_mode = SINGLE_REFERENCE;
    }
    else {
      VP9BitField reference_select = (VP9BitField) ReadBitUInt(1);
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
  VP9BitField update_mv_prob = (VP9BitField) ReadBitUInt(1);
  mv_probs_loop->set_update_mv_prob(update_mv_prob);
  if (update_mv_prob == 1) {
    mv_probs_loop->set_mv_prob(ReadBitUInt(7));
  }
}

CompressedHeader_MvProbs* ReadVP9MvProbs() {
  auto mv_probs = new CompressedHeader_MvProbs();

  for (uint32_t i = 0; i < 45; i++) {
    mv_probs->add_mv_probs();
    ReadVP9MvProbsLoop(mv_probs->mutable_mv_probs(i));
  }

  if (allow_high_precision_mv) {
    for (uint32_t i = 45; i < (45 + 4); i++) {
      mv_probs->add_mv_probs();
      ReadVP9MvProbsLoop(mv_probs->mutable_mv_probs(i);
    }
  }

  return mv_probs;
}

CompressedHeader* ReadVP9CompressedHeader() {
  auto compressed_header = new CompressedHeader();

  compressed_header->set_allocated_read_tx_mode(ReadVP9ReadTxMode());

  if (tx_mode == CompressedHeader_TxMode_TX_MODE_SELECT) {
    compressed_header->set_allocated_tx_mode_probs(ReadVP9TxModeProbs());
  }

  compressed_header->set_allocated_read_coef_probs(ReadVP9ReadCoefProbs());
  compressed_header->set_allocated_read_skip_prob(ReadVP9ReadSkipProb());

  if (FrameIsIntra == false) {
    compressed_header->set_allocated_read_inter_mode_probs(ReadVP9ReadInterModeProbs());
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

void ReadVP9Tile(Tile* tile) {
  uint32_t tile_size = ReadBitUInt(32);
  tile->set_tile_size(tile_size);
  tile->set_partition(ReadBitString(tile_size * 8));
}

VP9Frame* VP9ToProto(std::string vp9_frame_file_path) {
  // Create VP9 Protobuf Object
  vp9_frame = new VP9Frame();
  // Open file
  file = std::ifstream(vp9_frame_file_path, std::ios::binary);
  // Read into bit vector
  bit_buffer = std::vector<bool>();
  char data;
  while (file.read(&data, 1)) {
    for (int i = 0; i < 8; i++) {
      bit_buffer.push_back(((data >> i) & 1) != 0);
    }
  }
  // Read Headers
  vp9_frame->set_allocated_uncompressed_header(ReadVP9UncompressedHeader());
  ReadVP9TrailingBits();
  if (header_size_in_bytes == 0) {
    return vp9_frame;
  }
  vp9_frame->set_allocated_compressed_header(ReadVP9CompressedHeader());
  
  // Read Tiles
  uint32_t tile_count = 0;
  while (bit_counter <= bit_buffer.size()) {
    vp9_frame->add_tiles();
    ReadVP9Tile(vp9_frame->mutable_tiles(tile_count++));
  }
  
  return vp9_frame;
}

int main(int argc, char** argv) {
  VP9ToProto("../frames/test_frame.vp9");
  return 0;
}
