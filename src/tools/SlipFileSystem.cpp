#include "SlipEngine/tools/SlipFileSystem.h"
#include <SDL2/SDL.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
#include <SDL2/SDL_system.h>
#include <jni.h>
#endif

namespace fs = std::filesystem;

bool SlipFileSystem::ReadAllText(const std::string& path, std::string& outText) {
    if (path.empty()) return false;

    // 1. Intentar abrir con SDL_RWops (funciona transparente para APK assets en Android y archivos en PC)
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw != nullptr) {
        Sint64 fileSize = SDL_RWsize(rw);
        if (fileSize > 0) {
            outText.resize(static_cast<size_t>(fileSize));
            size_t readCount = SDL_RWread(rw, &outText[0], 1, static_cast<size_t>(fileSize));
            SDL_RWclose(rw);
            if (readCount > 0) {
                outText.resize(readCount);
                return true;
            }
        } else {
            // Tamaño dinámico o no reportado de inmediato por asset
            char buffer[4096];
            size_t bytesRead = 0;
            outText.clear();
            while ((bytesRead = SDL_RWread(rw, buffer, 1, sizeof(buffer))) > 0) {
                outText.append(buffer, bytesRead);
            }
            SDL_RWclose(rw);
            return !outText.empty();
        }
    }

    // 2. Fallback estándar fstream
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (in.is_open()) {
        std::ostringstream ss;
        ss << in.rdbuf();
        outText = ss.str();
        return true;
    }

    return false;
}

bool SlipFileSystem::ReadAllBytes(const std::string& path, std::vector<uint8_t>& outBytes) {
    if (path.empty()) return false;

    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw != nullptr) {
        Sint64 fileSize = SDL_RWsize(rw);
        if (fileSize > 0) {
            outBytes.resize(static_cast<size_t>(fileSize));
            size_t readCount = SDL_RWread(rw, outBytes.data(), 1, static_cast<size_t>(fileSize));
            SDL_RWclose(rw);
            if (readCount > 0) {
                outBytes.resize(readCount);
                return true;
            }
        } else {
            uint8_t buffer[4096];
            size_t bytesRead = 0;
            outBytes.clear();
            while ((bytesRead = SDL_RWread(rw, buffer, 1, sizeof(buffer))) > 0) {
                outBytes.insert(outBytes.end(), buffer, buffer + bytesRead);
            }
            SDL_RWclose(rw);
            return !outBytes.empty();
        }
    }

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (in.is_open()) {
        in.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        outBytes.resize(size);
        in.read(reinterpret_cast<char*>(outBytes.data()), size);
        return true;
    }

    return false;
}

bool SlipFileSystem::WriteAllText(const std::string& path, const std::string& text) {
    if (path.empty()) return false;

    // Crear directorio padre si no existe
    std::string parent = GetParentPath(path);
    if (!parent.empty()) {
        CreateDirectoryPath(parent);
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(text.c_str(), text.size());
    return out.good();
}

bool SlipFileSystem::WriteAllBytes(const std::string& path, const void* data, size_t size) {
    if (path.empty() || !data) return false;

    std::string parent = GetParentPath(path);
    if (!parent.empty()) {
        CreateDirectoryPath(parent);
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(data), size);
    return out.good();
}

bool SlipFileSystem::Exists(const std::string& path) {
    if (path.empty()) return false;

    std::error_code ec;
    if (fs::exists(path, ec)) {
        return true;
    }

    // Verificar si es un asset dentro de la APK en Android
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw != nullptr) {
        SDL_RWclose(rw);
        return true;
    }

    return false;
}

bool SlipFileSystem::IsDirectory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

uintmax_t SlipFileSystem::GetFileSize(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec) && !fs::is_directory(path, ec)) {
        return fs::file_size(path, ec);
    }

    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw != nullptr) {
        Sint64 size = SDL_RWsize(rw);
        SDL_RWclose(rw);
        return size > 0 ? static_cast<uintmax_t>(size) : 0;
    }

    return 0;
}

std::vector<FileInfo> SlipFileSystem::ListDirectory(const std::string& dirPath, const std::string& extensionFilter) {
    std::vector<FileInfo> results;
    std::error_code ec;

    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return results;
    }

    for (const auto& entry : fs::directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec)) {
        FileInfo info;
        info.name = entry.path().filename().string();
        info.fullPath = entry.path().lexically_normal().string();
        info.isDirectory = entry.is_directory(ec);

        if (info.isDirectory) {
            info.extension = "";
            info.sizeInBytes = 0;
            results.push_back(info);
        } else {
            info.extension = entry.path().extension().string();
            info.sizeInBytes = entry.file_size(ec);

            if (extensionFilter.empty() || extensionFilter == "*.*" || extensionFilter == "*") {
                results.push_back(info);
            } else {
                // Verificar extensión simple
                std::string extLower = info.extension;
                std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
                std::string filterLower = extensionFilter;
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

                if (filterLower.find(extLower) != std::string::npos) {
                    results.push_back(info);
                }
            }
        }
    }

    // Ordenar: primero carpetas alfabéticamente, luego archivos alfabéticamente
    std::sort(results.begin(), results.end(), [](const FileInfo& a, const FileInfo& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return a.name < b.name;
    });

    return results;
}

std::vector<FileInfo> SlipFileSystem::ListDirectoryRecursive(const std::string& dirPath, const std::string& extensionFilter) {
    std::vector<FileInfo> results;
    if (dirPath.empty()) return results;

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return results;

    std::vector<std::string> filterList;
    if (!extensionFilter.empty()) {
        std::stringstream ss(extensionFilter);
        std::string item;
        while (std::getline(ss, item, ',')) {
            size_t f = item.find_first_not_of(" \t\r\n");
            size_t l = item.find_last_not_of(" \t\r\n");
            if (f != std::string::npos) item = item.substr(f, l - f + 1);
            std::transform(item.begin(), item.end(), item.begin(), ::tolower);
            if (!item.empty()) filterList.push_back(item);
        }
    }

    auto iter = fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec);
    auto endIter = fs::recursive_directory_iterator();

    while (iter != endIter) {
        if (ec) {
            ec.clear();
            iter.increment(ec);
            continue;
        }

        std::error_code statusEc;
        bool isDir = iter->is_directory(statusEc);
        if (!statusEc && !isDir) {
            std::string ext = iter->path().extension().string();
            std::string extLower = ext;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);

            bool matches = filterList.empty();
            for (const auto& flt : filterList) {
                if (extLower == flt || flt == "*" || flt == "*.*") {
                    matches = true;
                    break;
                }
            }

            if (matches) {
                FileInfo info;
                info.name = iter->path().filename().string();
                info.fullPath = iter->path().lexically_normal().string();
                std::replace(info.fullPath.begin(), info.fullPath.end(), '\\', '/');
                info.extension = ext;
                info.isDirectory = false;
                info.sizeInBytes = iter->file_size(statusEc);
                results.push_back(info);
            }
        }

        iter.increment(ec);
    }

    std::sort(results.begin(), results.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.name < b.name;
    });

    return results;
}

std::string SlipFileSystem::GetGuidForAsset(const std::string& assetPath) {
    if (assetPath.empty()) return "";
    if (assetPath.rfind("primitive:", 0) == 0) return "";

    std::string metaPath = assetPath + ".meta";
    std::string content;
    if (ReadAllText(metaPath, content)) {
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string k = line.substr(0, colon);
                size_t kf = k.find_first_not_of(" \t\r\n");
                size_t kl = k.find_last_not_of(" \t\r\n");
                if (kf != std::string::npos) k = k.substr(kf, kl - kf + 1);
                if (k == "guid") {
                    std::string v = line.substr(colon + 1);
                    size_t vf = v.find_first_not_of(" \t\r\n");
                    size_t vl = v.find_last_not_of(" \t\r\n");
                    if (vf != std::string::npos) return v.substr(vf, vl - vf + 1);
                }
            }
        }
    }
    return "";
}

std::string SlipFileSystem::FindAssetByGuid(const std::string& guid) {
    if (guid.empty()) return "";
    std::string root = GetProjectAssetsPath();
    if (root.empty()) root = GetProjectRootPath();
    if (root.empty()) return "";

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string p = entry.path().string();
        if (p.size() > 5 && p.substr(p.size() - 5) == ".meta") {
            std::string content;
            if (ReadAllText(p, content)) {
                std::stringstream ss(content);
                std::string line;
                while (std::getline(ss, line)) {
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string k = line.substr(0, colon);
                        size_t kf = k.find_first_not_of(" \t\r\n");
                        size_t kl = k.find_last_not_of(" \t\r\n");
                        if (kf != std::string::npos) k = k.substr(kf, kl - kf + 1);
                        if (k == "guid") {
                            std::string v = line.substr(colon + 1);
                            size_t vf = v.find_first_not_of(" \t\r\n");
                            size_t vl = v.find_last_not_of(" \t\r\n");
                            if (vf != std::string::npos && v.substr(vf, vl - vf + 1) == guid) {
                                std::string assetPath = p.substr(0, p.size() - 5);
                                std::replace(assetPath.begin(), assetPath.end(), '\\', '/');
                                if (Exists(assetPath)) {
                                    return assetPath;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return "";
}

bool SlipFileSystem::CreateDirectoryPath(const std::string& path) {
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool SlipFileSystem::DeleteFileOrDir(const std::string& path) {
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

std::string SlipFileSystem::NormalizePath(const std::string& path) {
    std::string norm = fs::path(path).lexically_normal().string();
    std::replace(norm.begin(), norm.end(), '\\', '/');
    return norm;
}

std::string SlipFileSystem::GetParentPath(const std::string& path) {
    std::string parent = fs::path(path).parent_path().string();
    std::replace(parent.begin(), parent.end(), '\\', '/');
    return parent;
}

std::string SlipFileSystem::GetFileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string SlipFileSystem::GetFileNameWithoutExtension(const std::string& path) {
    return fs::path(path).stem().string();
}

std::string SlipFileSystem::GetFileExtension(const std::string& path) {
    return fs::path(path).extension().string();
}

std::string SlipFileSystem::CombinePath(const std::string& base, const std::string& sub) {
    fs::path combined = fs::path(base) / fs::path(sub);
    std::string res = combined.lexically_normal().string();
    std::replace(res.begin(), res.end(), '\\', '/');
    return res;
}

static std::string s_ProjectRootPath = "";
static std::string s_ProjectAssetsPath = "";

void SlipFileSystem::SetProjectPaths(const std::string& rootPath, const std::string& assetsPath) {
    s_ProjectRootPath = NormalizePath(rootPath);
    s_ProjectAssetsPath = NormalizePath(assetsPath);
}

std::string SlipFileSystem::GetProjectAssetsPath() {
    return s_ProjectAssetsPath;
}

std::string SlipFileSystem::GetProjectRootPath() {
    return s_ProjectRootPath;
}

std::string SlipFileSystem::GetInternalStoragePath() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* path = SDL_AndroidGetInternalStoragePath();
    if (path) {
        return NormalizePath(path);
    }
    return "/data/data/com.charlystudios.slipengine/files";
#else
    std::error_code ec;
    return NormalizePath(fs::current_path(ec).string());
#endif
}

std::string SlipFileSystem::GetExternalStoragePath() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    std::error_code ec;
    if (fs::exists("/storage/emulated/0", ec)) {
        return "/storage/emulated/0";
    }
    if (fs::exists("/sdcard", ec)) {
        return "/sdcard";
    }
    const char* path = SDL_AndroidGetExternalStoragePath();
    if (path) {
        return NormalizePath(path);
    }
    return "/storage/emulated/0";
#else
    std::error_code ec;
    return NormalizePath(fs::current_path(ec).string());
#endif
}

std::string SlipFileSystem::GetAssetsPath() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    return "assets";
#else
    return "assets";
#endif
}

std::string SlipFileSystem::GetDefaultWorkingPath() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    std::error_code ec;
    // En Android, abrir por defecto la carpeta de Descargas donde el usuario coloca modelos/texturas
    if (fs::exists("/storage/emulated/0/Download", ec)) {
        return "/storage/emulated/0/Download";
    }
    if (fs::exists("/storage/emulated/0", ec)) {
        return "/storage/emulated/0";
    }
    std::string ext = GetExternalStoragePath();
    if (fs::exists(ext, ec)) return ext;
    return GetInternalStoragePath();
#else
    std::error_code ec;
    return NormalizePath(fs::current_path(ec).string());
#endif
}

bool SlipFileSystem::HasStoragePermission() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return true;
    jclass activityClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (!activityClass) return true;
    jmethodID method = env->GetStaticMethodID(activityClass, "hasStoragePermission", "()Z");
    if (!method) {
        env->DeleteLocalRef(activityClass);
        return true;
    }
    jboolean res = env->CallStaticBooleanMethod(activityClass, method);
    env->DeleteLocalRef(activityClass);
    return res != JNI_FALSE;
#else
    return true;
#endif
}

void SlipFileSystem::RequestStoragePermissions() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return;
    jclass activityClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (!activityClass) return;
    jmethodID method = env->GetStaticMethodID(activityClass, "requestAllStoragePermissions", "()V");
    if (method) {
        env->CallStaticVoidMethod(activityClass, method);
    }
    env->DeleteLocalRef(activityClass);
#endif
}

