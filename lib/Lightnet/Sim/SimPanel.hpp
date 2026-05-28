#pragma once
#ifdef SIM_MODE

    #include "../Panel/AnimationPlayer.hpp"
    #include "SimRGBController.hpp"
    #include "../Common/Protocol.hpp"
    #include "../Common/Palette.hpp"

class SimPanel
{
    public:
        SimPanel()
        {
        }

        void setIndex(uint8_t idx)
        {
            panelIndex = idx;
            rgb.setPanelIndex(idx);
            player.setRGBController(&rgb);
        }

        uint8_t getIndex() const
        {
            return panelIndex;
        }

        void handlePacket(const void *data, uint8_t size);
        void tick();

    private:
        uint8_t panelIndex = 0;
        Lightnet::AnimationPlayer player;
        SimRGBController rgb;
};

#endif  // SIM_MODE
