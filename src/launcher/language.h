#pragma once
#include <string>
#include <string_view>
namespace nova::launcher {
enum class Language { english, italian, french, german, spanish, portuguese };
enum class Text {
    preparing, checking, once, select_apk, select_data, cancel, retry, busy,
    waiting_apk, waiting_data, welcome, data_title, apk_help, data_help,
    error_title, error_help, stopping, stopping_help, log_title, privacy,
    first_setup, window_title, ready_start, already_running, game_failed,
    step_progress, ready_files, verify, importing, preparing_file,
    apk_verified, verify_apk, extracting, auto_start, close, minimize,
    library_verified, data_verified, resources_preparing,
    step_of, step_apk, step_data, step_prepare, step_data_hint, step_prepare_hint,
    drop_apk, drop_data, or_word, other_file, count
};
inline constexpr int language_count=6;
// Two-letter display code (EN, IT, ...) used by the launcher's language picker.
const wchar_t *language_code(Language);
Language language_for_locale(std::wstring_view);
Language system_language();
const wchar_t *tr(Language,Text);
std::wstring localized_log(Language,std::string_view);
std::wstring display_log(Language,std::string_view);
}
