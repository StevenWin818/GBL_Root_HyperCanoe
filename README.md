build with CLAND/LLD version 20

此项目适用于 国行小米17 使用 EEA 3.0.12.0 系统
其他机型或系统可自行修改 tools\patchlib.h 中 `if (patch_fixed_adrl_target(buffer, size, 0x1DC64, load_base, 0x106da0, value_off,"HwCountry getvar") != 0)` 挂载点参数