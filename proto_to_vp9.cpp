#include <cmath>
#include <string>
#include <vector>

#include "vp9.pb.h"
#include "vp9_constants.h"

// Constants
#define MAX_TILES 2

// Parser State Variables
std::vector<bool> bit_buffer;
static bool Lossless = false;
static uint32_t tx_mode;
static bool FrameIsIntra = false;
static uint32_t interpolation_filter = 0;
bool compoundReferenceAllowed = false;
uint32_t reference_mode = 0;
uint32_t header_size_in_bytes = 0;
bool allow_high_precision_mv = 0;

// uint64_t BindBitUInt(uint64_t number, uint32_t bits) {
//   // Calculate max number for bit count
//   uint64_t max_number = pow(2, bits) - 1;
//   // Limit number to max bit size
//   if (number > max_number) {
//     return max_number;
//   }
//   return number;
// }

void WriteBitUInt(uint64_t number, uint32_t bits) {
  // Writes integer bits in big endian format to the bit buffer
  number = __builtin_bswap64(number);
  // Write number to bit buffer
  for (uint32_t i = 0; i < bits; i++) {
    bool number_bit = (number << i) & 0b1;
    bit_buffer.push_back(number_bit);
  }
}

void WriteBitString(std::string string, uint32_t bits) {
  // Convert string to c bytes
  const char* bytes = string.c_str();
  // Write string to bit buffer
  for (uint32_t i = 0; i < bits; i++) {
    int byte_index = floor(i / 8);
    bool string_bit = false;
    if (byte_index < string.size()) {
      bool string_bit = (bytes[byte_index] << i) & 0b1;
    }
    bit_buffer.push_back(string_bit);
  }
}

void WriteByteUInt(uint64_t number, uint32_t bytes) {
  // Writes integer bytes in big endian format to the bit buffer
  WriteBitUInt(number, bytes * 8);
}

void WriteVP9SignedInteger(const VP9SignedInteger *number, uint32_t number_bits) {
  // Writes signed number bits with the signed bit at the end
  WriteBitString(number->value(), number_bits);
  WriteBitUInt(number->sign(), 1);
}

void WriteVP9FrameSyncCode(const UncompressedHeader *uncompressed_header) {
  // Write frame sync codes
  WriteByteUInt(uncompressed_header->frame_sync_code(), 3);
}

void WriteVP9ColorConfig(const UncompressedHeader *uncompressed_header) {
  // Write the ten_or_twelve bit for certain profiles
  if (uncompressed_header->profile() >= 2) {
    WriteBitUInt(uncompressed_header->color_config().ten_or_twelve_bit(), 1);
  } 
  // Write color space info
  WriteBitUInt(uncompressed_header->color_config().color_space(), 3);
  // Write conditional color space info
  if (uncompressed_header->color_config().color_space() != UncompressedHeader_ColorConfig::CS_RGB) {
    WriteBitUInt(uncompressed_header->color_config().color_range(), 1);
    if (uncompressed_header->profile() == 1 || uncompressed_header->profile() == 3) {
      WriteBitUInt(uncompressed_header->color_config().subsampling_x(), 1);
      WriteBitUInt(uncompressed_header->color_config().subsampling_y(), 1);
      WriteBitUInt(uncompressed_header->color_config().reserved_zero(), 1);
    }
  }
  else {
    if (uncompressed_header->profile() == 1 || uncompressed_header->profile() == 3) {
      WriteBitUInt(uncompressed_header->color_config().reserved_zero(), 1);
    }
  }
}

void WriteVP9FrameSize(const UncompressedHeader *uncompressed_header) {
  // Write frame size
  // TODO: Maybe mutate this a bit more granularly
  WriteBitUInt(uncompressed_header->frame_size().frame_width_minus_1(), 16);
  WriteBitUInt(uncompressed_header->frame_size().frame_height_minus_1(), 16);
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
  WriteVP9SignedInteger(loop_filter_ref_delta, 6);
}

void WriteVP9ModeDelta(VP9BitField update_mode_delta, const VP9SignedInteger* loop_filter_mode_delta) {
  WriteBitUInt(update_mode_delta, 1);
  WriteVP9SignedInteger(loop_filter_mode_delta, 6);
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
    WriteBitString(feature->feature_value(), segmentation_feature_bits[seg_lvl_max]);
    if (segmentation_feature_signed[seg_lvl_max] == 1) {
      WriteBitUInt(feature->feature_sign(), 1);
    }
  }
}

void WriteVP9SegmentationParams(const UncompressedHeader *uncompressed_header) {
  WriteBitUInt(uncompressed_header->segmentation_params().segmentation_enabled(), 1);
  if (uncompressed_header->segmentation_params().segmentation_enabled() == 1) {
    WriteBitUInt(uncompressed_header->segmentation_params().segmentation_update_map(), 1);
    if (uncompressed_header->segmentation_params().segmentation_update_map() == 1) {
      WriteBitUInt(uncompressed_header->segmentation_params().segmentation_temporal_update(), 1);
    }
    WriteBitUInt(uncompressed_header->segmentation_params().segmentation_update_data(), 1);
    if (uncompressed_header->segmentation_params().segmentation_update_data() == 1) {
      WriteBitUInt(uncompressed_header->segmentation_params().segmentation_abs_or_delta_update(), 1);
      
      // Write features
      uint32_t feature_index = 0;
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

void WriteVP9TileInfo(const UncompressedHeader* uncompressed_header) {
  // Always write 1 bit of 0b1 since we don't know min or max cols
  WriteBitUInt(1, 1);
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
  WriteBitUInt(uncompressed_header->profile(), 2);
  // Write zero bit if needed
  if (uncompressed_header->profile() == 3) {
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
  WriteBitUInt(uncompressed_header->frame_type(), 1);
  // Write show frame bit
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
      WriteBitUInt(uncompressed_header->intra_only(), 1);
    }
    FrameIsIntra = intra_only;
    // Write reset_frame_context if error_resilient_mode
    if (uncompressed_header->error_resilient_mode() == 0) {
      WriteBitUInt(uncompressed_header->reset_frame_context(), 1);
    }
    // Write conditional intra frame data
    if (intra_only == 1) {
      WriteVP9FrameSyncCode(uncompressed_header);
      // Write conditional profile data
      if (uncompressed_header->profile() > 0) {
        WriteVP9ColorConfig(uncompressed_header);
      }
      // Write intra frame info
      WriteBitUInt(uncompressed_header->refresh_frame_flags(), 8);
      WriteVP9FrameSize(uncompressed_header);
      WriteVP9RenderSize(uncompressed_header);
    }
    else {
      WriteBitUInt(uncompressed_header->refresh_frame_flags(), 8);
      for (uint32_t i = 0; i < 3; i++) {
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
          if (i != 0 && uncompressed_header->ref_frame_sign_bias().at(0) != 0) {
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
  header_size_in_bytes = ceil(bit_buffer.size() / 8);
  WriteBitUInt(header_size_in_bytes, 16);
}

void WriteVP9ReadTxMode(const CompressedHeader *compressed_header) {
  if (Lossless == true) {
    tx_mode = CompressedHeader_TxMode_ONLY_4X4;
  }
  else {
    tx_mode = compressed_header->read_tx_mode().tx_mode();
    WriteBitUInt(tx_mode, 2);
    if (tx_mode == CompressedHeader_TxMode_ALLOW_32X32) {
      WriteBitUInt(compressed_header->read_tx_mode().tx_mode_select(), 1);
      tx_mode += (uint32_t) compressed_header->read_tx_mode().tx_mode_select();
    }
  }
}

void WriteVP9DecodeTermSubexp(const CompressedHeader_DecodeTermSubexp *decode_term_subexp) {
  WriteBitUInt(decode_term_subexp->bit_1(), 1);
  if (decode_term_subexp->bit_1() == 0) {
    WriteBitUInt(decode_term_subexp->sub_exp_val(), 4);
    return;
  }
  WriteBitUInt(decode_term_subexp->bit_2(), 1);
  if (decode_term_subexp->bit_2() == 0) {
    WriteBitUInt(decode_term_subexp->sub_exp_val_minus_16(), 4);
    return;
  }
  WriteBitUInt(decode_term_subexp->bit_3(), 1);
  if (decode_term_subexp->bit_3() == 0) {
    WriteBitUInt(decode_term_subexp->sub_exp_val_minus_32(), 5);
    return;
  }
  WriteBitUInt(decode_term_subexp->v(), 7);
  if (decode_term_subexp->v() < 65) {
    return;
  }
  WriteBitUInt(decode_term_subexp->bit_4(), 1);
}

void WriteVP9DiffUpdateProb(CompressedHeader_DiffUpdateProb *diff_update_prob) {
  // Write Update Prob bit
  WriteBitUInt(diff_update_prob->update_prob(), 1);
  // Write term subexp if we want to update probs
  if (diff_update_prob->update_prob() == 1) {
    WriteVP9DecodeTermSubexp(&diff_update_prob->decode_term_subexp());
  }
}

void WriteVP9DiffUpdateProbs(const google::protobuf::RepeatedPtrField<CompressedHeader_DiffUpdateProb> *diff_update_probs, uint32_t max_writes) {
  // Writes (0 < n < max_writes) count of DiffUpdateProb objects to the VP9 frame
  for (uint32_t i = 0; i < max_writes; i++) {
    // Check if we have a diff_update_prob object to write
    if (i < diff_update_probs->size()) {
      // Get diff update probability object at index
      CompressedHeader_DiffUpdateProb diff_update_prob = diff_update_probs->at(i);
      // Write the object
      WriteVP9DiffUpdateProb(&diff_update_prob);
    }
    // If we have no more diff_update_prob objects to write, just write empty objects
    else {
      WriteBitUInt(0, 1);
    }
  }
}

void WriteVP9TxModeProbs(const CompressedHeader *compressed_header) {
  WriteVP9DiffUpdateProbs(&compressed_header->tx_mode_probs().diff_update_prob(), 12);
}

void WriteVP9ReadCoefProbs(const CompressedHeader *compressed_header) {
  // Loop write count for tx_size max size
  for (uint32_t txSz = TX_4X4; txSz < tx_mode_to_biggest_tx_size[tx_mode]; txSz++) {
    // Check if we have a ReadCoefsProbsLoop object for this iteration
    if (txSz < compressed_header->read_coef_probs().read_coef_probs().size()) {
      // If so, grab the object
      auto loop_obj = compressed_header->read_coef_probs().read_coef_probs().at(txSz);
      // Write the update_probs indicator bit
      WriteBitUInt(loop_obj.update_probs(), 1);
      if (loop_obj.update_probs() == 1) {
        // Write diff_update_probs objects
        WriteVP9DiffUpdateProbs(&loop_obj.diff_update_prob(), 396);
      }
    }
    // Otherwise just write 1 0b0 bit to indicate we don't want to update probs
    else {
      WriteBitUInt(0, 1);
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
  WriteVP9DiffUpdateProbs(&compressed_header->read_interp_filter_probs().diff_update_prob(), 8);
}

void WriteVP9ReadIsInterProbs(const CompressedHeader *compressed_header) {
  WriteVP9DiffUpdateProbs(&compressed_header->read_is_inter_probs().diff_update_prob(), 4);
}

void WriteVP9FrameReferenceMode(const CompressedHeader *compressed_header) {
  if (compoundReferenceAllowed == 1) {
    WriteBitUInt(compressed_header->frame_reference_mode().non_single_reference(), 1);
    // Set reference mode state variable
    if (compressed_header->frame_reference_mode().non_single_reference() == 0) {
      reference_mode = SINGLE_REFERENCE;
    }
    else {
      WriteBitUInt(compressed_header->frame_reference_mode().reference_select(), 1);
      if (compressed_header->frame_reference_mode().reference_select() == 0) {
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
}

void WriteVP9FrameReferenceModeProbs(const CompressedHeader *compressed_header) {
  // TODO: Make this write sequential diff_update_prob objects instead of the same ones
  if (reference_mode == REFERENCE_MODE_SELECT) {
    WriteVP9DiffUpdateProbs(&compressed_header->frame_reference_mode_probs().diff_update_prob(), 5);
  }
  if (reference_mode != COMPOUND_REFERENCE) {
    WriteVP9DiffUpdateProbs(&compressed_header->frame_reference_mode_probs().diff_update_prob(), 5);
  }
  if (reference_mode != SINGLE_REFERENCE) {
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
  WriteBitUInt(update_mv_prob, 1);
  if (update_mv_prob == 1) {
    WriteBitUInt(mv_prob, 7);
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
  // Write tx mode probability info if select tx mode is enabled
  if (tx_mode == CompressedHeader_TxMode_TX_MODE_SELECT) {
    WriteVP9TxModeProbs(compressed_header);
  }
  // Write read probabilities
  WriteVP9ReadCoefProbs(compressed_header);
  WriteVP9ReadSkipProb(compressed_header);
  if (FrameIsIntra == false) {
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

void WriteVP9Tile(const Tile* tile) {
  // WriteBitUInt(tile->tile_size(), 32);
  WriteBitUInt(tile->partition().size(), 32);
  WriteBitString(tile->partition(), (tile->partition().size() * 8));
}

std::string BitVectorToBytes(std::vector<bool> &bit_buffer) {
  // Create std::string to return
  std::string return_buffer = std::string(ceil(bit_buffer.size() / 8), 0);
  // Loop through all the bits and set the bytes on the return buffer
  auto current_byte = return_buffer.begin();
  for (uint64_t i = 0; i < bit_buffer.size(); i++) {
    // Get bit
    bool bit = bit_buffer.at(i);
    // Get bit position in current string byte
    uint16_t bit_index = i % 8;
    // Write bit to the byte at specified position
    *current_byte |= bit << bit_index;
    // Update byte position if we've gone through 8 bits already
    if (i != 0 && bit_index == 7) {
      ++current_byte;
    }
  }
  // Return string
  return return_buffer;
}

void WriteVP9TrailingBits() {
 while (bit_buffer.size() & 7) {
  WriteBitUInt(0, 1);
 }
}

std::string ProtoToVP9(const VP9Frame *frame) {
  // Instantiate bitvector to store frame bits/bytes
  bit_buffer = std::vector<bool>();
  // Write VP9 uncompressed header
  WriteVP9UncompressedHeader(&frame->uncompressed_header());
  // Write trailing_bits
  WriteVP9TrailingBits();
  // Return if header size == 0
  if (header_size_in_bytes == 0) {
    return BitVectorToBytes(bit_buffer);
  }
  // Write VP9 compressed header
  WriteVP9CompressedHeader(&frame->compressed_header());
  // Write video frame tiles
  uint32_t tile_count = 0;
  for (const Tile tile : frame->tiles()) {
    WriteVP9Tile(&tile);
    // Limit tiles to 
    ++tile_count;
    if (tile_count > MAX_TILES) {
      break;
    }
  }
  
  // Convert bitvector to string (may need to byte align)
  return BitVectorToBytes(bit_buffer);
}

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  const VP9Frame vp9_frame;
  std::string binary = ProtoToVP9(&vp9_frame); 
  
  return 0;
}