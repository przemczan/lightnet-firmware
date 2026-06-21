#include <Arduino.h>
#include "TopologyConfigStore.hpp"
#include "../../Utils/SimpleJson.hpp"
#include "../../Utils/Debug.hpp"
#include "../../Utils/Fs/Fs.hpp"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

namespace Lightnet {
    namespace {
        const char *TOPO_PATH     = "/config/topology.json";
        const char *TOPO_TMP_PATH = "/config/topology.json.tmp";
        const uint8_t TOPO_SCHEMA = 1;
        const size_t TOPO_MAX_BYTES = 2048;
    } // anonymous namespace

    // Bounded formatted append: advances `pos`, never writing past bufLen-1.
    static size_t appendf(char *buf, size_t bufLen, size_t pos, const char *fmt, ...)
    {
        if (pos >= bufLen) return pos;

        va_list ap;

        va_start(ap, fmt);

        int n = vsnprintf(buf + pos, bufLen - pos, fmt, ap);

        va_end(ap);

        if (n < 0) return pos;

        pos += (size_t)n;

        return (pos > bufLen) ? bufLen : pos;
    }

    TopologyConfigStore::TopologyConfigStore()
        : _logicalRoot(1), count(0)
    {
    }

    void TopologyConfigStore::load()
    {
        if (!readFile()) {
            D_PRINTLN("[TOPO] no valid file; writing defaults");
            writeFile();
        }
    }

    bool TopologyConfigStore::setLogicalRoot(uint8_t panelIndex)
    {
        uint8_t v = panelIndex ? panelIndex : 1;

        if (v == _logicalRoot) return false;

        _logicalRoot = v;
        writeFile();

        return true;
    }

    bool TopologyConfigStore::replaceTags(const char *body, size_t len, char *errMsg, size_t errLen)
    {
        const char *p   = body;
        const char *end = body + len;

        return replaceTagsAt(p, end, errMsg, errLen);
    }

    bool TopologyConfigStore::replaceTagsAt(const char *& p, const char *end, char *errMsg, size_t errLen)
    {
        Entry scratch[MAX_ENTRIES];
        uint8_t scratchCount = 0;

        if (!parseTags(p, end, scratch, scratchCount, errMsg, errLen)) return false;

        memcpy(entries, scratch, scratchCount * sizeof(Entry));
        count = scratchCount;
        writeFile();

        return true;
    }

    void TopologyConfigStore::panelsForTag(const char *name, const TopologyIndex& topo, PanelSet& out) const
    {
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(entries[i].tag, name) == 0) {
                uint8_t sl;

                if (topo.slotOf(entries[i].panel, sl)) out.set(sl);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Parsing
    // -------------------------------------------------------------------------

    bool TopologyConfigStore::parseTags(
        const char *& p,
        const char *  end,
        Entry *       out,
        uint8_t&      outCount,
        char *        errMsg,
        size_t        errLen
    )
    {
        outCount = 0;

        if (!jsonEnterObject(p, end)) {
            strncpy(errMsg, "tags: expected object", errLen);

            return false;
        }

        char key[8];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            long panel = atol(key);

            if (panel < 1 || panel > 255) {
                strncpy(errMsg, "tags: panel index out of range (1-255)", errLen);

                return false;
            }

            if (!jsonEnterArray(p, end)) {
                strncpy(errMsg, "tags: expected array of tag names", errLen);

                return false;
            }

            while (jsonNextElement(p, end)) {
                char tag[TAG_NAME_MAX + 2];

                if (!jsonReadString(p, end, tag, sizeof(tag))) {
                    strncpy(errMsg, "tags: bad tag string", errLen);

                    return false;
                }

                if (!isValidTagName(tag)) {
                    strncpy(errMsg, "tags: invalid name ([a-zA-Z0-9_-], 1-15)", errLen);

                    return false;
                }

                if (outCount >= MAX_ENTRIES) {
                    strncpy(errMsg, "tags: too many entries", errLen);

                    return false;
                }

                strncpy(out[outCount].tag, tag, TAG_NAME_MAX);
                out[outCount].tag[TAG_NAME_MAX] = '\0';
                out[outCount].panel             = (uint8_t)panel;
                outCount++;
            }
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Serialization
    // -------------------------------------------------------------------------

    size_t TopologyConfigStore::appendTagsMap(char *buf, size_t bufLen, size_t pos) const
    {
        pos = appendf(buf, bufLen, pos, "{");

        bool firstPanel = true;
        uint8_t done[MAX_ENTRIES];
        uint8_t doneCount = 0;

        for (uint8_t i = 0; i < count; i++) {
            uint8_t panel   = entries[i].panel;
            bool already = false;

            for (uint8_t d = 0; d < doneCount; d++) if (done[d] == panel) {
                    already = true;
                    break;
                }

            if (already) continue;

            done[doneCount++] = panel;

            pos = appendf(buf, bufLen, pos, "%s\"%u\":[", firstPanel ? "" : ",", (unsigned)panel);
            firstPanel = false;

            bool firstTag = true;

            for (uint8_t j = i; j < count; j++) {
                if (entries[j].panel != panel) continue;

                pos      = appendf(buf, bufLen, pos, "%s\"%s\"", firstTag ? "" : ",", entries[j].tag);
                firstTag = false;
            }

            pos = appendf(buf, bufLen, pos, "]");
        }

        return appendf(buf, bufLen, pos, "}");
    }

    void TopologyConfigStore::writeJson(char *buf, size_t bufLen) const
    {
        size_t pos = appendf(buf, bufLen, 0, "{\"logicalRoot\":%u,\"tags\":", (unsigned)_logicalRoot);

        pos = appendTagsMap(buf, bufLen, pos);
        appendf(buf, bufLen, pos, "}");
    }

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    bool TopologyConfigStore::readFile()
    {
        if (!Fs::exists(TOPO_PATH)) return false;

        File f = Fs::open(TOPO_PATH, "r");

        if (!f) return false;

        char *buf = (char *)malloc(TOPO_MAX_BYTES);

        if (!buf) {
            f.close();

            return false;
        }

        size_t n = f.readBytes(buf, TOPO_MAX_BYTES - 1);

        f.close();
        buf[n] = '\0';

        SimpleJson j(buf, n);
        long schema = j.getInt("schemaVersion");

        if (schema > 0 && schema != TOPO_SCHEMA) {
            D_PRINTLN("[TOPO] schema mismatch — using defaults");
            free(buf);

            return false;
        }

        long root = j.getInt("logicalRoot");

        _logicalRoot = ((root >= 1) && (root <= 255)) ? (uint8_t)root : 1;

        const char *tagsVal = j.rawValue("tags");

        count = 0;

        if (tagsVal) {
            const char *p   = tagsVal;
            const char *end = buf + n;
            char err[48];

            if (!parseTags(p, end, entries, count, err, sizeof(err))) {
                D_PRINTLN("[TOPO] tags parse failed — ignoring tag map");
                count = 0;
            }
        }

        free(buf);

        return true;
    }

    void TopologyConfigStore::writeFile()
    {
        File f = Fs::open(TOPO_TMP_PATH, "w");

        if (!f) {
            D_PRINTLN("[TOPO] failed to open tmp file");

            return;
        }

        char *buf = (char *)malloc(TOPO_MAX_BYTES);

        if (!buf) {
            f.close();

            return;
        }

        size_t pos = appendf(buf, TOPO_MAX_BYTES, 0, "{\"schemaVersion\":%u,\"logicalRoot\":%u,\"tags\":",
                             (unsigned)TOPO_SCHEMA, (unsigned)_logicalRoot);

        pos = appendTagsMap(buf, TOPO_MAX_BYTES, pos);
        pos = appendf(buf, TOPO_MAX_BYTES, pos, "}\n");

        f.write((const uint8_t *)buf, pos);
        f.close();
        free(buf);

        Fs::deleteFile(TOPO_PATH);
        Fs::rename(TOPO_TMP_PATH, TOPO_PATH);
    }
}  // namespace Lightnet
