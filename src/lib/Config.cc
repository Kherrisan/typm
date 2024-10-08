#include"Config.h"

int ENABLE_MLTA = 0;
int ENABLE_TYDM = 1;
int MAX_PHASE_CG = 2;

string SRC_ROOT = "";

// Optional pointer to output file stream
std::unique_ptr<std::ofstream> OUTPUT_FILE;
