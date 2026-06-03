#include "SceneStore.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Utils/Fs/Fs.hpp"

namespace Lightnet {
    namespace {
        void scenePath(const char *name, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/scenes/%s.json", name);
        }
    } // anonymous namespace

    bool SceneStore::save(const char *name, const char *json, size_t len) const
    {
        char path[40];

        scenePath(name, path, sizeof(path));

        File f = Fs::open(path, "w");

        if (!f) return false;

        f.write((const uint8_t *)json, len);
        f.close();

        return true;
    }

    char * SceneStore::load(const char *name, size_t& outLen) const
    {
        char path[40];

        scenePath(name, path, sizeof(path));

        if (!Fs::exists(path)) {
            outLen = 0;

            return nullptr;
        }

        File f = Fs::open(path, "r");

        if (!f) {
            outLen = 0;

            return nullptr;
        }

        size_t fileSize = f.size();

        if (fileSize > MAX_SCENE_BYTES) {
            f.close();
            outLen = 0;

            return nullptr;
        }

        char *buf = (char *)malloc(fileSize + 1);

        if (!buf) {
            f.close();
            outLen = 0;

            return nullptr;
        }

        outLen = f.readBytes(buf, fileSize);
        f.close();
        buf[outLen] = '\0';

        return buf;
    }

    bool SceneStore::exists(const char *name) const
    {
        char path[40];

        scenePath(name, path, sizeof(path));

        return Fs::exists(path);
    }

    bool SceneStore::del(const char *name) const
    {
        char path[40];

        scenePath(name, path, sizeof(path));

        if (!Fs::exists(path)) return false;

        return Fs::remove(path);
    }

    void SceneStore::listJson(char *buf, size_t bufLen) const
    {
        int n = snprintf(buf, bufLen, "[");
        bool first = true;
        FsDir d("/scenes/");

        while (d.next() && n + 64 < (int)bufLen) {
            String fn = d.fileName();   // e.g. "/scenes/sunset.json"
            const char *base = fn.c_str();

            if (strncmp(base, "/scenes/", 8) == 0) base += 8;

            size_t blen = strlen(base);

            // Only list *.json files; skip the shared write temp file.
            if (blen <= 5 || strcmp(base + blen - 5, ".json") != 0) continue;

            char name[24] = { 0 };
            size_t nlen = blen - 5;

            if (nlen >= sizeof(name)) continue;

            memcpy(name, base, nlen);
            n += snprintf(buf + n, bufLen - n, "%s{\"name\":\"%s\",\"size\":%u}",
                          first ? "" : ",", name, (unsigned)d.fileSize());
            first = false;
        }

        if (n < (int)bufLen) {
            buf[n++] = ']';
            buf[n] = '\0';
        }
    }
}  // namespace Lightnet
