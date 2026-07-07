#include "DiscoveryTreeBuilder.hpp"

namespace Lightnet {
    DiscoveryTreeBuilder::DiscoveryTreeBuilder(uint8_t edgeCountPerPanel)
        : edgeCountPerPanel(edgeCountPerPanel), panelCount_(0), linkCount_(0)
    {
    }

    void DiscoveryTreeBuilder::reset()
    {
        this->panelCount_ = 0;
        this->linkCount_  = 0;
    }

    void DiscoveryTreeBuilder::recordPanel(uint16_t panelIndex)
    {
        if (this->panelCount_ >= MAX_PANELS) {
            return;
        }

        this->indices_[this->panelCount_]    = (uint8_t)panelIndex;
        this->edgeCounts_[this->panelCount_] = this->edgeCountPerPanel;
        this->panelCount_++;
    }

    void DiscoveryTreeBuilder::addRoot(uint16_t panelIndex)
    {
        this->recordPanel(panelIndex);
    }

    void DiscoveryTreeBuilder::addLink(
        uint16_t parentIndex,
        uint16_t parentEdge,
        uint16_t childIndex,
        uint16_t childEdge
    )
    {
        this->recordPanel(childIndex);

        if (this->linkCount_ >= MAX_PANELS) {
            return;
        }

        this->links_[this->linkCount_].panelA = (uint8_t)parentIndex;
        this->links_[this->linkCount_].edgeA  = (uint8_t)parentEdge;
        this->links_[this->linkCount_].panelB = (uint8_t)childIndex;
        this->links_[this->linkCount_].edgeB  = (uint8_t)childEdge;
        this->linkCount_++;
    }

    uint8_t DiscoveryTreeBuilder::panelCount() const
    {
        return this->panelCount_;
    }

    const uint8_t *DiscoveryTreeBuilder::indices() const
    {
        return this->indices_;
    }

    const uint8_t *DiscoveryTreeBuilder::edgeCounts() const
    {
        return this->edgeCounts_;
    }

    const TopoLink *DiscoveryTreeBuilder::links() const
    {
        return this->links_;
    }

    uint8_t DiscoveryTreeBuilder::linkCount() const
    {
        return this->linkCount_;
    }
}  // namespace Lightnet
