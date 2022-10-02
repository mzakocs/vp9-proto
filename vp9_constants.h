#define TX_MODES 5
#define SEG_LVL_MAX 4

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

int segmentation_feature_bits[ SEG_LVL_MAX ] = { 8, 6, 2, 0 };
int segmentation_feature_signed[ SEG_LVL_MAX ] = { 1, 1, 0, 0 };