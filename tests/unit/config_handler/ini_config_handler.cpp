#include "../../../include/config_handler/ini_config_handler.hpp"

int main(int argc, char const* argv[]) {
    using namespace config_handler;
    IniConfigHandler ini_config_handler;

    fmt::println("read test");
    bool ret = ini_config_handler.ReadIniFile("./configs/log_config.ini");
    if (ret) {
        Val  val;
        bool ret = ini_config_handler.GetVal("LOG_GLOBAL", "max_file_size_kb", val);
        fmt::println("ret:{},max_file_size_kb:{};", ret, val.GetInt());

        ret = ini_config_handler.GetVal("LOG_GLOBAL", "print_line", val);
        fmt::println("ret:{},print_line:{};", ret, val.GetBool());

        ret = ini_config_handler.GetVal("LOG_GLOBAL", "log_directory", val);
        fmt::println("ret:{},log_directory:{};", ret, val.GetString());
    }

    fmt::println("write test");
    ini_config_handler.SetVal("LOG_GLOBAL", "max_file_size_kb", Val("512"));
    ret = ini_config_handler.Save();
    fmt::println("save ret:{}", ret);
    return 0;
}
