#include "LittleFsPaletteRepository.hpp"
#include "PaletteJson.hpp"
#include "../../Utils/Fs/Fs.hpp"
#include "../../Utils/EntryId.hpp"
#include "../Store/IContentReader.hpp"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace Lightnet {
    namespace {
        struct BuiltInPalette {
            const char *       seed;
            const char *       displayName;
            const GradientStop stops[PALETTE_STOPS];
            uint8_t            count;
        };

        const BuiltInPalette BUILTINS[] = {
            {
                "rainbow", "Rainbow",
                { { 0, 0xFF, 0, 0 }, { 42, 0xFF, 0xFF, 0 }, { 85, 0, 0xFF, 0 },
                    { 128, 0, 0xFF, 0xFF }, { 170, 0, 0, 0xFF }, { 213, 0xFF, 0, 0xFF },
                    { 255, 0xFF, 0, 0 } },
                7
            },
            {
                "lava", "Lava",
                { { 0, 0, 0, 0 }, { 46, 0x24, 0, 0 }, { 96, 0x71, 0x11, 0 },
                    { 148, 0x8E, 0x03, 0x01 }, { 204, 0xFF, 0x47, 0x02 }, { 255, 0xFF, 0xFF, 0xFF } },
                6
            },
            {
                "ocean", "Ocean",
                { { 0, 0, 0, 0x10 }, { 64, 0, 0x20, 0x60 }, { 128, 0, 0x60, 0xA0 },
                    { 192, 0x10, 0xC0, 0xE0 }, { 255, 0xFF, 0xFF, 0xFF } },
                5
            },
            {
                "forest", "Forest",
                { { 0, 0, 0x10, 0 }, { 64, 0, 0x40, 0x10 }, { 128, 0x10, 0x80, 0x20 },
                    { 192, 0x60, 0xC0, 0x40 }, { 255, 0xC0, 0xFF, 0x80 } },
                5
            },
            {
                "party", "Party",
                { { 0, 0x55, 0, 0xFF }, { 64, 0xFF, 0, 0x80 }, { 128, 0xFF, 0x80, 0 },
                    { 192, 0xFF, 0xFF, 0 }, { 255, 0, 0xFF, 0xFF } },
                5
            },
            {
                "sunset", "Sunset",
                { { 0, 0x10, 0, 0x40 }, { 64, 0x80, 0x10, 0x40 }, { 128, 0xFF, 0x40, 0x10 },
                    { 192, 0xFF, 0xA0, 0x10 }, { 255, 0xFF, 0xE0, 0x40 } },
                5
            },
            {
                "aurora", "Aurora",
                { { 0, 0, 0x10, 0x20 }, { 64, 0, 0x80, 0x60 }, { 128, 0x20, 0xFF, 0xA0 },
                    { 192, 0x80, 0x40, 0xC0 }, { 255, 0xFF, 0x40, 0xFF } },
                5
            },
            {
                "embers", "Embers",
                { { 0, 0, 0, 0 }, { 64, 0x20, 0, 0 }, { 128, 0x80, 0x10, 0 },
                    { 192, 0xFF, 0x40, 0 }, { 255, 0xFF, 0xC0, 0x40 } },
                5
            },
        };

        const uint8_t BUILTINS_COUNT = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

        void builtinSeed(const char *seed, char *out, size_t outLen)
        {
            char buf[32];

            snprintf(buf, sizeof(buf), "builtin:%s", seed);
            deterministicId(buf, out, outLen);
        }

        const BuiltInPalette * findBuiltinById(const char *id)
        {
            if (!id) return nullptr;

            for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
                char bid[ENTRY_ID_MAX + 1];

                builtinSeed(BUILTINS[i].seed, bid, sizeof(bid));

                if (strcmp(bid, id) == 0) return &BUILTINS[i];
            }

            return nullptr;
        }

        void contentPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/palettes/%s.json", id);
        }

        void metaPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/palettes/%s.meta.json", id);
        }

        void contentTmpPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/palettes/%s.json.tmp", id);
        }

        void metaTmpPath(const char *id, char *out, size_t outLen)
        {
            snprintf(out, outLen, "/palettes/%s.meta.json.tmp", id);
        }

        class FsPaletteContentReader : public IContentReader
        {
            public:
                explicit FsPaletteContentReader(File file) : _file(file)
                {
                }

                ~FsPaletteContentReader() override
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

        bool writeContentFile(
            const char *        tmpPath,
            const char *        id,
            const char *        name,
            const GradientStop *stops,
            uint8_t             count
        )
        {
            File f = Fs::open(tmpPath, "w");

            if (!f) return false;

            f.print("{\"schemaVersion\":1,\"id\":\"");
            f.print(id);
            f.print("\",\"name\":\"");
            f.print(name);
            f.print("\",\"stops\":[");

            for (uint8_t i = 0; i < count; i++) {
                if (i) f.print(",");

                char hex[8];

                snprintf(hex, sizeof(hex), "#%02X%02X%02X", stops[i].r, stops[i].g, stops[i].b);
                f.print("[");
                f.print((int)stops[i].pos);
                f.print(",\"");
                f.print(hex);
                f.print("\"]");
            }

            f.print("]}");
            f.close();

            return true;
        }

        bool writeMetaFile(const char *tmpPath, const PaletteMeta& meta)
        {
            File f = Fs::open(tmpPath, "w");

            if (!f) return false;

            char buf[160];
            int n = serializePaletteMeta(meta, buf, sizeof(buf));

            if (n <= 0) {
                f.close();

                return false;
            }

            f.write((const uint8_t *)buf, (size_t)n);
            f.close();

            return true;
        }
    } // anonymous namespace

    LittleFsPaletteRepository::LittleFsPaletteRepository()
    {
    }

    bool LittleFsPaletteRepository::metaExistsCb(const char *id, void *ctx)
    {
        auto *self = static_cast<const LittleFsPaletteRepository *>(ctx);

        return self->metaExists(id);
    }

    bool LittleFsPaletteRepository::metaExists(const char *id) const
    {
        char path[48];

        metaPath(id, path, sizeof(path));

        return Fs::exists(path);
    }

    void LittleFsPaletteRepository::ensureSeeded()
    {
        StoreLock::Guard g(_lock);

        Fs::mkdir("/palettes");

        for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
            char id[ENTRY_ID_MAX + 1];

            builtinSeed(BUILTINS[i].seed, id, sizeof(id));

            if (metaExists(id)) continue;

            writePair(id, BUILTINS[i].displayName, BUILTINS[i].stops, BUILTINS[i].count, true);
        }

        const char *ucId = userColorsId();

        if (!metaExists(ucId)) {
            PaletteMeta meta = {};

            strncpy(meta.id, ucId, sizeof(meta.id) - 1);
            strncpy(meta.name, "Base colors", sizeof(meta.name) - 1);
            meta.builtin = true;
            writeMetaOnly(meta);
        }
    }

    bool LittleFsPaletteRepository::writeMetaOnly(const PaletteMeta& meta) const
    {
        char finalPath[48];
        char tmpPath[52];

        metaPath(meta.id, finalPath, sizeof(finalPath));
        metaTmpPath(meta.id, tmpPath, sizeof(tmpPath));

        if (!writeMetaFile(tmpPath, meta)) return false;

        Fs::deleteFile(finalPath);
        Fs::rename(tmpPath, finalPath);

        return true;
    }

    bool LittleFsPaletteRepository::writePair(
        const char *        id,
        const char *        name,
        const GradientStop *stops,
        uint8_t             count,
        bool                builtin
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

        if (!writeContentFile(contentTmp, id, name, stops, count)) return false;

        PaletteMeta meta = {};

        strncpy(meta.id, id, sizeof(meta.id) - 1);
        strncpy(meta.name, name, sizeof(meta.name) - 1);
        meta.builtin = builtin;

        if (!writeMetaFile(metaTmp, meta)) {
            Fs::deleteFile(contentTmp);

            return false;
        }

        Fs::deleteFile(contentFinal);
        Fs::rename(contentTmp, contentFinal);
        Fs::deleteFile(metaFinal);
        Fs::rename(metaTmp, metaFinal);

        return true;
    }

    bool LittleFsPaletteRepository::resolve(const char *id, GradientStop *outStops, uint8_t& outCount) const
    {
        if (!id || isUserColors(id)) return false;

        StoreLock::Guard g(_lock);

        char path[48];

        contentPath(id, path, sizeof(path));

        if (Fs::exists(path)) {
            File f = Fs::open(path, "r");

            if (!f) return false;

            char buf[512];
            size_t n = f.readBytes(buf, sizeof(buf) - 1);

            f.close();
            buf[n] = '\0';

            return Lightnet::parsePaletteJson(buf, n, outStops, outCount);
        }

        const BuiltInPalette *builtin = findBuiltinById(id);

        if (builtin) {
            outCount = builtin->count;

            for (uint8_t j = 0; j < outCount; j++) {
                outStops[j] = builtin->stops[j];
            }

            return true;
        }

        return false;
    }

    bool LittleFsPaletteRepository::exists(const char *id) const
    {
        if (!id) return false;

        StoreLock::Guard g(_lock);

        return metaExists(id);
    }

    bool LittleFsPaletteRepository::loadMeta(const char *id, PaletteMeta& out) const
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

        return parsePaletteMeta(buf, n, out);
    }

    void LittleFsPaletteRepository::listMetas(MetaFn fn, void *ctx) const
    {
        StoreLock::Guard g(_lock);

        listMetasUnlocked(fn, ctx);
    }

    void LittleFsPaletteRepository::listMetasUnlocked(MetaFn fn, void *ctx) const
    {
        if (!fn) return;

        FsDir d("/palettes/");

        while (d.next()) {
            String fnName = d.fileName();
            const char *base = fnName.c_str();

            if (strncmp(base, "/palettes/", 10) == 0) base += 10;

            size_t blen = strlen(base);

            if (blen <= 10 || strcmp(base + blen - 10, ".meta.json") != 0) continue;

            char id[ENTRY_ID_MAX + 1] = { 0 };
            size_t idLen = blen - 10;

            if (idLen >= sizeof(id)) continue;

            memcpy(id, base, idLen);

            char path[48];

            metaPath(id, path, sizeof(path));

            File f = Fs::open(path, "r");

            if (!f) continue;

            char buf[256];
            size_t n = f.readBytes(buf, sizeof(buf) - 1);

            f.close();
            buf[n] = '\0';

            PaletteMeta meta = {};

            if (!parsePaletteMeta(buf, n, meta)) continue;

            fn(meta, ctx);
        }
    }

    bool LittleFsPaletteRepository::saveNew(
        const char *        name,
        const GradientStop *stops,
        uint8_t             count,
        char *              idOut,
        size_t              idOutLen
    )
    {
        if (!name || !stops || count == 0 || count > PALETTE_STOPS || !idOut || idOutLen < ENTRY_ID_LEN + 1) {
            return false;
        }

        StoreLock::Guard g(_lock);

        if (!generateRandomId(idOut, idOutLen, metaExistsCb, (void *)this)) return false;

        return writePair(idOut, name, stops, count, false);
    }

    bool LittleFsPaletteRepository::update(
        const char *        id,
        const char *        name,
        const GradientStop *stops,
        uint8_t             count
    )
    {
        if (!id || !name || !stops || count == 0 || count > PALETTE_STOPS) return false;

        if (isBuiltIn(id) || isUserColors(id)) return false;

        StoreLock::Guard g(_lock);

        if (!metaExists(id)) return false;

        return writePair(id, name, stops, count, false);
    }

    bool LittleFsPaletteRepository::deleteEntry(const char *id)
    {
        if (!id || isBuiltIn(id) || isUserColors(id)) return false;

        StoreLock::Guard g(_lock);

        char content[48];
        char meta[48];

        contentPath(id, content, sizeof(content));
        metaPath(id, meta, sizeof(meta));

        if (!Fs::exists(meta)) return false;

        if (Fs::exists(content)) Fs::deleteFile(content);

        return Fs::deleteFile(meta);
    }

    bool LittleFsPaletteRepository::isBuiltIn(const char *id) const
    {
        return findBuiltinById(id) != nullptr;
    }

    bool LittleFsPaletteRepository::isUserColors(const char *id) const
    {
        return id && (strcmp(id, userColorsId()) == 0);
    }

    const char * LittleFsPaletteRepository::userColorsId() const
    {
        return Lightnet::userColorsId();
    }

    IContentReader * LittleFsPaletteRepository::openContent(const char *id)
    {
        if (!id || isUserColors(id)) return nullptr;

        char path[48];

        contentPath(id, path, sizeof(path));

        if (!Fs::exists(path)) return nullptr;

        File f = Fs::open(path, "r");

        if (!f) return nullptr;

        return new FsPaletteContentReader(f);
    }
}  // namespace Lightnet
