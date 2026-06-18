#pragma once

#include "IPaletteRepository.hpp"
#include "../../Utils/StoreLock.hpp"

namespace Lightnet {
    class LittleFsPaletteRepository : public IPaletteRepository
    {
        public:
            LittleFsPaletteRepository();

            void ensureSeeded() override;

            bool resolve(const char *id, GradientStop *outStops, uint8_t& outCount) const override;

            bool exists(const char *id) const override;
            bool loadMeta(const char *id, PaletteMeta& out) const override;
            void listMetas(MetaFn fn, void *ctx) const override;
            void listMetasUnlocked(MetaFn fn, void *ctx) const;

            bool saveNew(
                const char *        name,
                const GradientStop *stops,
                uint8_t             count,
                char *              idOut,
                size_t              idOutLen
            ) override;

            bool update(
                const char *        id,
                const char *        name,
                const GradientStop *stops,
                uint8_t             count
            ) override;

            bool deleteEntry(const char *id) override;

            bool isBuiltIn(const char *id) const override;
            bool isUserColors(const char *id) const override;
            const char * userColorsId() const override;

            IContentReader * openContent(const char *id) override;

            StoreLock& lock()
            {
                return _lock;
            }

        private:
            mutable StoreLock _lock;

            bool writePair(
                const char *        id,
                const char *        name,
                const GradientStop *stops,
                uint8_t             count,
                bool                builtin
            ) const;

            bool writeMetaOnly(const PaletteMeta& meta) const;
            bool metaExists(const char *id) const;
            static bool metaExistsCb(const char *id, void *ctx);
    };
}  // namespace Lightnet
