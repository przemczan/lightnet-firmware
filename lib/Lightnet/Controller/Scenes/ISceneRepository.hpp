#pragma once

#include <stddef.h>
#include <stdint.h>
#include "SceneMeta.hpp"
#include "../Store/IContentReader.hpp"

namespace Lightnet {
    class ISceneRepository
    {
        public:
            static constexpr size_t MAX_SCENE_BYTES = 4096;

            virtual ~ISceneRepository()
            {
            }

            virtual bool exists(const char *id) const = 0;
            virtual bool loadMeta(const char *id, SceneMeta& out) const = 0;

            typedef void (*MetaFn)(const SceneMeta& meta, void *ctx);
            virtual void listMetas(MetaFn fn, void *ctx) const = 0;

            virtual bool save(const char *id, const char *content, size_t len, const SceneMeta& meta) = 0;
            virtual bool deleteEntry(const char *id) = 0;
            virtual bool allocateId(char *out, size_t outLen) const = 0;

            virtual char * loadContent(const char *id, size_t& outLen) const = 0;
            virtual IContentReader * openContent(const char *id) = 0;

            virtual const char * oneShotId() const = 0;
            virtual bool isHiddenId(const char *id) const = 0;
    };
}  // namespace Lightnet
