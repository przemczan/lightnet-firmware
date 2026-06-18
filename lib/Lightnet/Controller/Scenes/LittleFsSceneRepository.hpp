#pragma once

#include "ISceneRepository.hpp"
#include "../../Utils/StoreLock.hpp"

namespace Lightnet {
    class LittleFsSceneRepository : public ISceneRepository
    {
        public:
            static constexpr size_t MAX_SCENE_BYTES = 4096;

            LittleFsSceneRepository();

            bool exists(const char *id) const override;
            bool loadMeta(const char *id, SceneMeta& out) const override;
            void listMetas(MetaFn fn, void *ctx) const override;
            void listMetasUnlocked(MetaFn fn, void *ctx) const;

            bool save(const char *id, const char *content, size_t len, const SceneMeta& meta) override;
            bool deleteEntry(const char *id) override;

            char * loadContent(const char *id, size_t& outLen) const override;
            IContentReader * openContent(const char *id) override;

            const char * oneShotId() const override;
            bool isHiddenId(const char *id) const override;

            bool allocateId(char *out, size_t outLen) const override;

            StoreLock& lock()
            {
                return _lock;
            }

        private:
            mutable StoreLock _lock;

            static bool metaExistsCb(const char *id, void *ctx);
            bool metaExists(const char *id) const;
            bool contentExists(const char *id) const;
            bool writePair(const char *id, const char *content, size_t len, const SceneMeta& meta) const;
    };
}  // namespace Lightnet
