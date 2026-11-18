#pragma once

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <cstdint>

struct FileInfo {
    std::string name;
    std::string fullPath;
    std::string extension;
    bool isDirectory = false;
    uintmax_t sizeInBytes = 0;
};

class SLIPENGINE_API SlipFileSystem {
public:
    // Lectura de archivos (compatible con APK Assets en Android y sistema de archivos de PC)
    static bool ReadAllText(const std::string& path, std::string& outText);
    static bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& outBytes);

    // Escritura de archivos
    static bool WriteAllText(const std::string& path, const std::string& text);
    static bool WriteAllBytes(const std::string& path, const void* data, size_t size);

    // Verificación de archivos y directorios
    static bool Exists(const std::string& path);
    static bool IsDirectory(const std::string& path);
    static uintmax_t GetFileSize(const std::string& path);

    // Operaciones de directorio
    static std::vector<FileInfo> ListDirectory(const std::string& dirPath, const std::string& extensionFilter = "");
    static std::vector<FileInfo> ListDirectoryRecursive(const std::string& dirPath, const std::string& extensionFilter = "");
    static bool CreateDirectoryPath(const std::string& path);
    static bool DeleteFileOrDir(const std::string& path);

    // Rutas y utilidades de paths
    static std::string NormalizePath(const std::string& path);
    static std::string GetParentPath(const std::string& path);
    static std::string GetFileName(const std::string& path);
    static std::string GetFileNameWithoutExtension(const std::string& path);
    static std::string GetFileExtension(const std::string& path);
    static std::string CombinePath(const std::string& base, const std::string& sub);

    // Gestión de UIDs y metadatos (.meta) de Assets
    static std::string GetGuidForAsset(const std::string& assetPath);
    static std::string FindAssetByGuid(const std::string& guid);

    // Rutas específicas del sistema (PC / Android)
    static void SetProjectPaths(const std::string& rootPath, const std::string& assetsPath);
    static std::string GetProjectAssetsPath();
    static std::string GetProjectRootPath();
    static std::string GetInternalStoragePath();
    static std::string GetExternalStoragePath();
    static std::string GetAssetsPath();
    static std::string GetDefaultWorkingPath();

    // Permisos de almacenamiento en tiempo de ejecución (Android)
    static bool HasStoragePermission();
    static void RequestStoragePermissions();
};

