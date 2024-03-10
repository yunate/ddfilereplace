#include "ddfilereplace/stdafx.h"

#include "ddbase/dddef.h"
#include "ddbase/ddassert.h"
#include "ddbase/ddcmd_line_utils.h"
#include "ddbase/ddio.h"
#include "ddbase/ddlocale.h"
#include "ddbase/ddtimer.h"
#include "ddbase/ddmini_include.h"

#include <process.h>

#pragma comment(lib, "ddbase.lib")


namespace NSP_DD {
    static ddtimer s_timer;
    static std::wstring s_src_path;
    static bool s_force_utf8 = false;
    static std::vector<std::wstring> s_finders;
    static std::vector<std::wstring> s_replaces;
    static s32 s_result = 1;


    static void log(ddconsole_color color, const std::wstring& log_str)
    {
        ddcout(color) << log_str;
    }

    static void help()
    {
        log(ddconsole_color::gray, L"ddfilereplace.exe src_path [-utf8] [-p key1 value1] [-p key2 value2] ... [-p keyn valuen] [-help] [-p] \r\n");
        log(ddconsole_color::gray, L"The program can automatically recognize file encodings: UTF-16 LE, UTF-16 BE, and UTF-8 with BOM.\r\n");
        log(ddconsole_color::gray, L"But, ANSI and UTF-8 are both multi-byte encoding methods and neither of them has a file header, so it cannot automatically analyze whether the file is ANSI or UTF-8.\r\n");
        log(ddconsole_color::gray, L"The program defaults to assuming the file is encoded in ANSI, and you can use '-utf8' to force UTF-8 encoding.\r\n");
    }

    static void replace_file()
    {
        ddfile_type type = ddfile::get_file_type(s_src_path);
        if (type == ddfile_type::unknown) {
            log(ddconsole_color::red, ddstr::format(L"%s can not open as a file\r\n", s_src_path.c_str()));
            help();
            return;
        }

        std::vector<ddbuff> finder_buffs;
        std::vector<ddbuff> replace_buffs;
        finder_buffs.reserve(s_finders.size());
        replace_buffs.reserve(s_finders.size());
        if (type == ddfile_type::utf8_or_ansi) {
            if (s_force_utf8) {
                type = ddfile_type::utf8bom;
            } else {
                type = ddfile_type::ansi;
            }
        }

        if (type == ddfile_type::utf8bom) {
            for (size_t i = 0; i < s_finders.size(); ++i) {
                const std::string& finder = ddstr::utf16_8(s_finders[i]);
                finder_buffs.push_back(ddbuff(finder.begin(), finder.end()));

                const std::string& replace = ddstr::utf16_8(s_replaces[i]);
                replace_buffs.push_back(ddbuff(replace.begin(), replace.end()));
            }
        } else if (type == ddfile_type::ansi) {
            // use ansi
            for (size_t i = 0; i < s_finders.size(); ++i) {
                const std::string& finder = ddstr::utf16_ansi(s_finders[i]);
                finder_buffs.push_back(ddbuff(finder.begin(), finder.end()));

                const std::string& replace = ddstr::utf16_ansi(s_replaces[i]);
                replace_buffs.push_back(ddbuff(replace.begin(), replace.end()));
            }
        } else if (type == ddfile_type::utf16le) {
            for (size_t i = 0; i < s_finders.size(); ++i) {
                // finder
                const std::wstring& finder = s_finders[i];
                ddbuff finder_buff;
                finder_buff.reserve(finder.size() * 2);
                for (size_t j = 0; j < finder.size(); ++j) {
                    finder_buff.push_back(u8(finder[j]));
                    finder_buff.push_back(u8(finder[j] >> 8));
                }
                finder_buffs.push_back(finder_buff);

                // replace
                const std::wstring& replace = s_replaces[i];
                ddbuff replace_buff;
                replace_buff.reserve(finder.size() * 2);
                for (size_t j = 0; j < replace.size(); ++j) {
                    replace_buff.push_back(u8(replace[j]));
                    replace_buff.push_back(u8(replace[j] >> 8));
                }
                replace_buffs.push_back(replace_buff);
            }
        } else if (type == ddfile_type::utf16be) {
            for (size_t i = 0; i < s_finders.size(); ++i) {
                // finder
                const std::wstring& finder = s_finders[i];
                ddbuff finder_buff;
                finder_buff.reserve(finder.size() * 2);
                for (size_t j = 0; j < finder.size(); ++j) {
                    finder_buff.push_back(u8(finder[j] >> 8));
                    finder_buff.push_back(u8(finder[j]));
                }
                finder_buffs.push_back(finder_buff);

                // replace
                const std::wstring& replace = s_replaces[i];
                ddbuff replace_buff;
                replace_buff.reserve(finder.size() * 2);
                for (size_t j = 0; j < replace.size(); ++j) {
                    replace_buff.push_back(u8(replace[j] >> 8));
                    replace_buff.push_back(u8(replace[j]));
                }
                replace_buffs.push_back(replace_buff);
            }
        }

        std::shared_ptr<ddfile> src_file(ddfile::create_utf8_file(s_src_path));
        if (src_file == nullptr) {
            help();
            return;
        }

        ddbuff src_buff(src_file->file_size());
        src_file->read(src_buff.data(), (s32)src_buff.size());
        ddbuff new_buff;
        ddstr::buff_replace_ex(src_buff, finder_buffs, replace_buffs, new_buff);
        src_file->resize(new_buff.size());
        src_file->seek(0);
        src_file->write(new_buff.data(), (s32)new_buff.size());
        s_result = 0;
        log(ddconsole_color::green, ddstr::format(L"Operation successful, took a total of %d milliseconds.\r\n", s_timer.get_time_pass() / 1000000));
    }

    static void process_cmds(const std::vector<std::wstring>& cmds)
    {
        if (cmds.size() < 2) {
            help();
            return;
        }

        s_src_path = cmds[1];
        std::vector<std::wstring> includes;
        std::vector<std::wstring> excludes;

        for (size_t i = 2; i < cmds.size(); ++i) {
            if (cmds[i] == L"-help") {
                help();
                return;
            }

            if (cmds[i] == L"-utf8") {
                s_force_utf8 = true;
                continue;
            }

            if (cmds[i] == L"-p") {
                ++i; // finder
                ++i; // replace
                if (i >= cmds.size()) {
                    help();
                    return;
                }
                s_finders.push_back(cmds[i - 1]);
                s_replaces.push_back(cmds[i]);
                continue;
            }
        }

        replace_file();
    }

    int ddmain()
    {
        s_timer.reset();
        ddlocale::set_utf8_locale_and_io_codepage();
        s_timer.reset();
        std::vector<std::wstring> cmds;
        ddcmd_line_utils::get_cmds(cmds);
        process_cmds(cmds);
        return s_result;
    }
} // namespace NSP_DD

void main()
{
    // ::_CrtSetBreakAlloc(918);
    int result = NSP_DD::ddmain();

#ifdef _DEBUG
    _cexit();
    DDASSERT_FMT(!::_CrtDumpMemoryLeaks(), L"Memory leak!!! Check the output to find the log.");
    ::system("pause");
    ::_exit(result);
#else
    ::system("pause");
    ::exit(result);
#endif
}

