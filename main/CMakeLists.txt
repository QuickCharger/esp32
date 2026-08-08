# 注册组件：声明本组件的源文件和头文件目录
# - SRCS: 本组件包含的所有 .c 源文件
# - INCLUDE_DIRS: 头文件搜索路径（当前目录）
idf_component_register(SRCS "ble_hidd_demo_main.c"
                            "esp_hidd_prf_api.c"
                            "hid_dev.c"
                            "hid_device_le_prf.c"
                            "web_ble_config.c"
                    INCLUDE_DIRS ".")

# 编译选项：忽略"未使用的常量变量"警告（HID Report Map 中有些常量可能未在代码中引用）
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
