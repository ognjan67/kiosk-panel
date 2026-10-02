# Payten ECR protokol — kopija common/payten/payten_ecr.c iz CWS repoa (grana payten-pos,
# commit 554eefb); testovi: common/payten/test/test_pecr.py u CWS repou.
idf_component_register(SRCS "payten_ecr.c"
                       INCLUDE_DIRS "include")
target_compile_definitions(${COMPONENT_LIB} PUBLIC PECR_RX_MAX=640u)
# tx_no je max 6 cifara (0..999999) — GCC to ne zna; izvor ostaje identican CWS kopiji
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
