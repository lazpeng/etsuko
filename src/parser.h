/**
 * parser.h - Handles the parsing and definition of what lyrics are to be shown and at what time, using which effects
 */

#pragma once
#include <optional>
#include <string>
#include <vector>

namespace etsuko {
    namespace parser {
        struct TimedLyricSpan {
            size_t start_idx, end_idx;
            double start_time, duration;
        };

        struct TimedLyric {
            std::string full_line;
            double base_start_time, base_duration;
            std::vector<TimedLyricSpan> spans;
        };

        struct Song {
            std::string id;
            std::string name;
            std::string translated_name;
            std::string artist;
            std::string album;
            std::string language;
            int year;
            std::string karaoke_type;
            std::string karaoke_v2_variant;
            std::string file_path;
            std::string album_art_path;
            std::vector<std::string> raw_lyrics;
            std::vector<std::string> raw_original;
            std::vector<TimedLyric> lyrics;
            std::vector<TimedLyric> original;
            double time_offset = 0;
            std::string bg_color;
            double translate_duration_override = 0;
        };
    }

    class Parser {
    public:
        static parser::Song parse(const std::string& file_path);
    };
}
