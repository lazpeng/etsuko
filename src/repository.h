/**
 * repository.h - Handles the loading and provisioning of files necessary for the program, like fonts, audio and image
 * files and the lyrics' definitions themselves
 */

#pragma once

#include <string>

namespace etsuko {
    namespace repository {
        struct LoadJob {
            enum Status {
                NOT_STARTED = 0,
                LOADING,
                DONE,
                NONE,
            };

            std::string result_path;
            std::string error;
            Status status = NOT_STARTED;
        };
    }

    class Repository {
    public:
        static void get_resource(const std::string& path, repository::LoadJob *job);
    };
} // namespace etsuko
