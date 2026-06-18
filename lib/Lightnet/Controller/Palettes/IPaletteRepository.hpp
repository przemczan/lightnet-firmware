#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Common/UserColors.hpp"
#include "../../Core/Controller/IPaletteResolver.hpp"
#include "PaletteMeta.hpp"
#include "../Store/IContentReader.hpp"

namespace Lightnet {
    class IPaletteRepository : public IPaletteResolver
    {
        public:
            virtual ~IPaletteRepository()
            {
            }

            virtual void ensureSeeded() = 0;

            virtual bool exists(const char *id) const = 0;
            virtual bool loadMeta(const char *id, PaletteMeta& out) const = 0;

            typedef void (*MetaFn)(const PaletteMeta& meta, void *ctx);
            virtual void listMetas(MetaFn fn, void *ctx) const = 0;

            virtual bool saveNew(
                const char *        name,
                const GradientStop *stops,
                uint8_t             count,
                char *              idOut,
                size_t              idOutLen
            ) = 0;

            virtual bool update(
                const char *        id,
                const char *        name,
                const GradientStop *stops,
                uint8_t             count
            ) = 0;

            virtual bool deleteEntry(const char *id) = 0;

            virtual bool isBuiltIn(const char *id) const = 0;
            virtual bool isUserColors(const char *id) const = 0;
            virtual const char * userColorsId() const = 0;

            virtual IContentReader * openContent(const char *id) = 0;

            static void buildUserColors(
            const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
            GradientStop *           outStops,
            uint8_t&                 outCount
            )
            {
                Lightnet::buildUserColors(baseColors, outStops, outCount);
            }
    };
}  // namespace Lightnet
