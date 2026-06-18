#include "LittleFsSceneRepository.hpp"
#include "../../Utils/Fs/Fs.hpp"
#include "../../Utils/EntryId.hpp"
#include "../Store/IContentReader.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Lightnet {
    namespace {
        void contentPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/scenes/%s.json", id);
        }

        void metaPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/scenes/%s.meta.json", id);
        }

        void contentTmpPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/scenes/%s.json.tmp", id);
        }

        void metaTmpPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/scenes/%s.meta.json.tmp", id);
        }

        class FsSceneContentReader : public IContentReader
        {
            public:
                explicit FsSceneContentReader(File file) : _file(file)
                {
                }

                ~FsSceneContentReader() override
                {
                    if (_file) _file.close();
                }

                int read(uint8_t *buf, size_t cap) override
                {
                    if (!_file) return -1;

                    return _file.read(buf, cap);
                }

            private:
                File _file;
        };
    } // anonymous namespace

    LittleFsSceneRepository::LittleFsSceneRepository()
    {
    }

    bool LittleFsSceneRepository::metaExistsCb(const char *id, void *ctx)
    {
        auto *self = static_cast<const LittleFsSceneRepository *>(ctx);

        return self->metaExists(id);
    }

    bool LittleFsSceneRepository::allocateId(char *out, size_t outLen) const
    {
        StoreLock::Guard g(_lock);

        return generateRandomId(out, outLen, metaExistsCb, (void *)this);
    }

    bool LittleFsSceneRepository::metaExists(const char *id) const
    {
        char path[48];

        metaPath(id, path, sizeof(path));

        return Fs::exists(path);
    }

    bool LittleFsSceneRepository::contentExists(const char *id) const
    {
        char path[48];

        contentPath(id, path, sizeof(path));

        return Fs::exists(path);
    }

    bool LittleFsSceneRepository::exists(const char *id) const
    {
        if (!id) return false;

        StoreLock::Guard g(_lock);

        if (isHiddenId(id)) return contentExists(id);

        return metaExists(id) && contentExists(id);
    }

    bool LittleFsSceneRepository::loadMeta(const char *id, SceneMeta& out) const
    {
        if (!id) return false;

        StoreLock::Guard g(_lock);

        char path[48];

        metaPath(id, path, sizeof(path));

        if (!Fs::exists(path)) return false;

        File f = Fs::open(path, "r");

        if (!f) return false;

        char buf[256];
        size_t n = f.readBytes(buf, sizeof(buf) - 1);

        f.close();
        buf[n] = '\0';

        return parseSceneMeta(buf, n, out);
    }

    void LittleFsSceneRepository::listMetas(MetaFn fn, void *ctx) const
    {
        StoreLock::Guard g(_lock);

        listMetasUnlocked(fn, ctx);
    }

    void LittleFsSceneRepository::listMetasUnlocked(MetaFn fn, void *ctx) const
    {
        if (!fn) return;

        const char *hidden = oneShotId();
        FsDir d("/scenes/");

        while (d.next()) {
            String fnName = d.fileName();
            const char *base = fnName.c_str();

            if (strncmp(base, "/scenes/", 8) == 0) base += 8;

            size_t blen = strlen(base);

            if (blen <= 10 || strcmp(base + blen - 10, ".meta.json") != 0) continue;

            char id[ENTRY_ID_MAX + 1] = { 0 };
            size_t idLen = blen - 10;

            if (idLen >= sizeof(id)) continue;

            memcpy(id, base, idLen);

            if (strcmp(id, hidden) == 0) continue;

            if (!contentExists(id)) continue;

            char path[48];

            metaPath(id, path, sizeof(path));

            File f = Fs::open(path, "r");

            if (!f) continue;

            char buf[256];
            size_t n = f.readBytes(buf, sizeof(buf) - 1);

            f.close();
            buf[n] = '\0';

            SceneMeta meta = {};

            if (!parseSceneMeta(buf, n, meta)) continue;

            fn(meta, ctx);
        }
    }

    bool LittleFsSceneRepository::writePair(
        const char *     id,
        const char *     content,
        size_t           len,
        const SceneMeta& meta
    ) const
    {
        char contentFinal[48];
        char contentTmp[52];
        char metaFinal[48];
        char metaTmp[52];

        contentPath(id, contentFinal, sizeof(contentFinal));
        contentTmpPath(id, contentTmp, sizeof(contentTmp));
        metaPath(id, metaFinal, sizeof(metaFinal));
        metaTmpPath(id, metaTmp, sizeof(metaTmp));

        File cf = Fs::open(contentTmp, "w");

        if (!cf) return false;

        cf.write((const uint8_t *)content, len);
        cf.close();

        File mf = Fs::open(metaTmp, "w");

        if (!mf) {
            Fs::deleteFile(contentTmp);

            return false;
        }

        char mbuf[192];
        int mn = serializeSceneMeta(meta, mbuf, sizeof(mbuf));

        if (mn <= 0) {
            mf.close();
            Fs::deleteFile(contentTmp);
            Fs::deleteFile(metaTmp);

            return false;
        }

        mf.write((const uint8_t *)mbuf, (size_t)mn);
        mf.close();

        Fs::deleteFile(contentFinal);
        Fs::rename(contentTmp, contentFinal);
        Fs::deleteFile(metaFinal);
        Fs::rename(metaTmp, metaFinal);

        return true;
    }

    bool LittleFsSceneRepository::save(const char *id, const char *content, size_t len, const SceneMeta& meta)
    {
        if (!id || !content || len == 0 || len > MAX_SCENE_BYTES) return false;

        StoreLock::Guard g(_lock);

        Fs::mkdir("/scenes");

        if (isHiddenId(id)) {
            char contentFinal[48];
            char contentTmp[52];

            contentPath(id, contentFinal, sizeof(contentFinal));
            contentTmpPath(id, contentTmp, sizeof(contentTmp));

            File cf = Fs::open(contentTmp, "w");

            if (!cf) return false;

            cf.write((const uint8_t *)content, len);
            cf.close();
            Fs::deleteFile(contentFinal);
            Fs::rename(contentTmp, contentFinal);

            return true;
        }

        return writePair(id, content, len, meta);
    }

    bool LittleFsSceneRepository::deleteEntry(const char *id)
    {
        if (!id || isHiddenId(id)) return false;

        StoreLock::Guard g(_lock);

        char content[48];
        char meta[48];

        contentPath(id, content, sizeof(content));
        metaPath(id, meta, sizeof(meta));

        if (!metaExists(id)) return false;

        if (Fs::exists(content)) Fs::deleteFile(content);

        return Fs::deleteFile(meta);
    }

    char * LittleFsSceneRepository::loadContent(const char *id, size_t& outLen) const
    {
        outLen = 0;

        if (!id) return nullptr;

        StoreLock::Guard g(_lock);

        char path[48];

        contentPath(id, path, sizeof(path));

        if (!Fs::exists(path)) return nullptr;

        File f = Fs::open(path, "r");

        if (!f) return nullptr;

        size_t fileSize = f.size();

        if (fileSize > MAX_SCENE_BYTES) {
            f.close();

            return nullptr;
        }

        char *buf = (char *)malloc(fileSize + 1);

        if (!buf) {
            f.close();

            return nullptr;
        }

        outLen = f.readBytes(buf, fileSize);
        f.close();
        buf[outLen] = '\0';

        return buf;
    }

    IContentReader * LittleFsSceneRepository::openContent(const char *id)
    {
        if (!id) return nullptr;

        char path[48];

        contentPath(id, path, sizeof(path));

        if (!Fs::exists(path)) return nullptr;

        File f = Fs::open(path, "r");

        if (!f) return nullptr;

        return new FsSceneContentReader(f);
    }

    const char * LittleFsSceneRepository::oneShotId() const
    {
        return Lightnet::oneShotId();
    }

    bool LittleFsSceneRepository::isHiddenId(const char *id) const
    {
        return id && (strcmp(id, oneShotId()) == 0);
    }
}  // namespace Lightnet
