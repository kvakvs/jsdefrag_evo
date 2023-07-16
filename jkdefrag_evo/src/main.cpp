#include "../include/precompiled_header.h"
#include "../include/defrag.h"

void set_locale();

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
    set_locale();

    Defrag *defrag = Defrag::get_instance();
    WPARAM ret_value = 0;

    if (defrag != nullptr) {
        ret_value = defrag->start_program(instance, prev_instance, cmd_line, cmd_show);

        Defrag::release_instance();
    }

    return (int) ret_value;
}

void set_locale() {
    std::locale::global(std::locale("en_US.UTF-8"));
//    std::setlocale(LC_ALL, "en_US.UTF-8");
//    auto ploc = std::localeconv();
//    strcpy_s(ploc->decimal_point, 16, ".");
//    strcpy_s(ploc->thousands_sep, 16, "'");
}
