#if defined(ARDUINO_ARCH_ESP32)

#include "Fs.hpp"

namespace Lightnet {
    bool Fs::begin()
    {
        return LittleFS.begin(true);
    }                                                                                        // format on mount failure

    bool Fs::exists(const char *path)
    {
        return LittleFS.exists(path);
    }

    File Fs::open(const char *path, const char *mode)
    {
        return LittleFS.open(path, mode);
    }

    bool Fs::remove(const char *path)
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

    FsDir::FsDir(const char *path) : _dir(LittleFS.open(path))
    {
    }

    bool FsDir::next()
    {
        _entry = _dir.openNextFile();

        return (bool)_entry;
    }

    String FsDir::fileName() const
    {
        return String(_entry.name());
    }

    size_t FsDir::fileSize() const
    {
        return _entry.size();
    }
}  // namespace Lightnet

#endif
