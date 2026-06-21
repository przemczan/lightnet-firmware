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
        const size_t TOPO_MAX_BYTES = 256;
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
        : _logicalRoot(1)
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

        char buf[64];
        size_t pos = appendf(buf, sizeof(buf), 0, "{\"schemaVersion\":%u,\"logicalRoot\":%u}\n",
                             (unsigned)TOPO_SCHEMA, (unsigned)_logicalRoot);

        f.write((const uint8_t *)buf, pos);
        f.close();

        Fs::deleteFile(TOPO_PATH);
        Fs::rename(TOPO_TMP_PATH, TOPO_PATH);
    }
}  // namespace Lightnet
