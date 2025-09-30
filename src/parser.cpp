#include "parser.h"

#include <fstream>
#include <string>
#include <filesystem>
#include <print>

using namespace etsuko::parser;

enum BlockType {
    LINE_HEADER = 0,
    LINE_LYRICS,
    LINE_LEGACY_TIMINGS,
};

std::string_view trim(const std::string_view &str) {
    size_t start = 0, end = str.size() - 1;
    for ( ; start < str.size() && std::isspace(str[start]); ++start ) {
    }
    for ( ; end < str.size() && std::isspace(str[end]); --end ) {
    }
    return str.substr(start, end - start + 1);
}

static void read_header(const std::string_view &line, Song &song) {
    const auto equals_pos = line.find('=');
    if ( equals_pos == std::string_view::npos ) {
        throw std::runtime_error("Invalid option");
    }

    const auto property = line.substr(0, equals_pos);
    const auto value = line.substr(equals_pos + 1);

    if ( property == "name" ) {
        song.name = value;
    } else if ( property == "translatedName" ) {
        song.translated_name = value;
    } else if ( property == "artist" ) {
        song.artist = value;
    } else if ( property == "album" ) {
        song.album = value;
    } else if ( property == "year" ) {
        song.year = static_cast<int32_t>(std::strtol(value.data(), nullptr, 10));
    } else if ( property == "filePath" ) {
        song.file_path = value;
    } else if ( property == "karaoke" ) {
        song.karaoke_type = value;
    } else if ( property == "karaokeVariant" ) {
        song.karaoke_v2_variant = value;
    } else if ( property == "language" ) {
        song.language = value;
    } else if ( property == "albumArt" ) {
        song.album_art_path = value;
    }
}

static void read_lyrics(const std::string_view &line, Song &song) {
    song.raw_lyrics.emplace_back(line);
}

static void read_timings(const std::string_view &line, Song &song) {
    const auto colon = line.find(':');
    if ( colon == std::string_view::npos ) {
        throw std::runtime_error("Invalid timings line");
    }
    const auto minutes = std::strtod(line.substr(0, colon).data(), nullptr);
    const auto seconds = std::strtod(line.substr(colon + 1).data(), nullptr);

    const auto start = minutes * 60 + seconds + 0.5; // TODO: Remove padding
    if ( !song.lyrics.empty() ) {
        auto &last = song.lyrics.back();
        last.base_duration = std::max(0.0, start - last.base_start_time);
    }

    std::string full_line;
    if ( !song.raw_lyrics.empty() ) {
        const auto idx = song.lyrics.size();
        full_line = song.raw_lyrics[idx];
    }

    song.lyrics.push_back({.full_line = full_line, .base_start_time = start, .base_duration = 100});
}

Song etsuko::Parser::parse(const std::string &file_path) {
    const auto path = std::filesystem::path(file_path);

    const auto id = path.filename().replace_extension("").string();
    Song song = {
        .id = id,
    };

    std::ifstream file(file_path);

    bool fix_lyrics = false;

    BlockType current_type = LINE_HEADER;
    std::string l;
    while ( std::getline(file, l) ) {
        const auto line = trim(l);
        if ( !line.empty() && line[0] == '#' ) {
            if ( line == "#lyrics" ) {
                current_type = LINE_LYRICS;

                if ( !song.lyrics.empty() )
                    fix_lyrics = true;
            } else if ( line == "#timings" ) {
                current_type = LINE_LEGACY_TIMINGS;
            } else {
                throw std::runtime_error("Unknown block type");
            }
            // Continue into the block
            continue;
        }

        switch ( current_type ) {
        case LINE_HEADER:
            read_header(line, song);
            break;
        case LINE_LYRICS:
            read_lyrics(line, song);
            break;
        case LINE_LEGACY_TIMINGS:
            read_timings(line, song);
            break;
        }
    }

    if ( fix_lyrics ) {
        size_t idx = 0;
        if ( song.lyrics.size() != song.raw_lyrics.size() ) {
            throw std::runtime_error("Lyrics and timings do not match");
        }

        for ( ; idx < song.lyrics.size(); ++idx ) {
            song.lyrics[idx].full_line = song.raw_lyrics[idx];
        }
    }

    return song;
}
