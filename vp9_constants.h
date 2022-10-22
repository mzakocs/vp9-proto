namespace VP9Fuzzer {

#define MAX_TILES 3 // max tiles the vp9 converter can read/write from/to an ivf file

#define TX_MODES 5
#define SEG_LVL_MAX 4

typedef size_t BD_VALUE;
#define BD_VALUE_SIZE ((int)sizeof(BD_VALUE) * CHAR_BIT)
#define LOTS_OF_BITS 0x40000000

#define MIN_TILE_WIDTH_B64 4
#define MAX_TILE_WIDTH_B64 64

enum TxSize {
  TX_4X4,
  TX_8X8,
  TX_16X16,
  TX_32X32
};

enum ReferenceMode {
  SINGLE_REFERENCE,
  COMPOUND_REFERENCE,
  REFERENCE_MODE_SELECT
};

int tx_mode_to_biggest_tx_size[ TX_MODES ] = {
 TX_4X4,
 TX_8X8,
 TX_16X16,
 TX_32X32,
 TX_32X32
};

unsigned int segmentation_feature_bits[ SEG_LVL_MAX ] = { 8, 6, 2, 0 };
unsigned int segmentation_feature_signed[ SEG_LVL_MAX ] = { 1, 1, 0, 0 };


const uint8_t vpx_norm[256] = {
  0, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

}