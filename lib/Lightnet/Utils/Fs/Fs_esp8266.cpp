#if defined(ARDUINO_ARCH_ESP8266)

#include "Fs.hpp"

namespace Lightnet {
    bool Fs::begin()
    {
        return LittleFS.begin();
    }

    bool Fs::exists(const char *path)
    {
        return LittleFS.exists(path);
    }

    File Fs::open(const char *path, const char *mode)
    {
        return LittleFS.open(path, mode);
    }

    bool Fs::deleteFile(const char *path)
    {
        return LittleFS.remove(path);
    }

    bool Fs::rename(const char *from, const char *to)
    {
        return LittleFS.rename(from, to);
    }

    bool Fs::mkdir(const char *path)
    {
        return LittleFS.mkdir(path);
    }

    fs::FS &Fs::raw()
    {
        return LittleFS;
    }

    FsDir::FsDir(const char *path) : _dir(LittleFS.openDir(path))
    {
    }

    bool FsDir::next()
    {
        return _dir.next();
    }

    String FsDir::fileName() const
    {
        return _dir.fileName();
    }

    size_t FsDir::fileSize() const
    {
        return _dir.fileSize();
    }
}  // namespace Lightnet

#endif
