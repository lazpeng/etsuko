#include "repository.h"

#include <filesystem>
#include <format>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>

constexpr auto CDN_BASE_PATH = "https://cdn.wooby.moe/songs/";

static void on_fetch_success(emscripten_fetch_t *fetch) {
    const auto job = static_cast<etsuko::repository::LoadJob*>(fetch->userData);
    const auto output_file = job->result_path;

    std::printf("writing file to: %s\n", output_file.c_str());
    FILE *out = fopen(output_file.c_str(), "wb");
    if (out == nullptr) {
        std::puts("Failed to open file");
    }
    std::fwrite(fetch->data, 1, fetch->numBytes, out);
    std::fflush(out);
    std::fclose(out);

    job->status = etsuko::repository::LoadJob::DONE;
    emscripten_fetch_close(fetch);
}

static void on_fetch_failure(emscripten_fetch_t *fetch) {
    std::puts("Failed to fetch resource");
    std::puts(fetch->statusText);
    emscripten_fetch_close(fetch);
}

#endif

void etsuko::Repository::get_resource(const std::string &path, repository::LoadJob *job) {
    if ( job == nullptr ) {
        throw std::runtime_error("Invalid job pointer");
    }

    std::filesystem::create_directory("assets");
    const std::filesystem::path p(path);
    job->result_path = std::format("assets/{}", p.filename().string());
    job->status = repository::LoadJob::LOADING;
#ifdef __EMSCRIPTEN__
    const auto full_path = CDN_BASE_PATH + path;
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = on_fetch_success;
    attr.onerror = on_fetch_failure;
    attr.userData = job;

    emscripten_fetch(&attr, full_path.c_str());
#else
    job->status = repository::LoadJob::DONE;
#endif
}
